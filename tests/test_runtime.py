"""Foreground lifecycle tests: real FIFO, poll and processes, no transport."""
import os
from pathlib import Path
import select
import signal
import shlex
import time
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent
BIN = REPO / '.build/gin'


class RuntimeTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get('TMPDIR', str(REPO / '.build/test-scratch'))).resolve()
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='gin-loop-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'root'
        self.args = ['-d', str(self.root), '-n', 'net', '-c', '#one']
        self.fifo = self.root / 'net/one/in'

    def start(self, consumer=False):
        command = [str(REPO / '.build/runtime_consumer'), str(self.root)] if consumer else [str(BIN), 'run', *self.args]
        command = shlex.split(os.environ.get('GIN_RUNTIME_PREFIX', '')) + command
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        stream = child.stdout if consumer else child.stderr
        assert stream is not None
        self.assertTrue(select.select([stream], [], [], 5)[0], 'readiness timeout')
        self.assertEqual(stream.readline(), b'ready\n' if consumer else b'gin: starting local input; transport unavailable; sending unsupported\n')
        if not consumer:
            # The announcement is intent, not readiness. A real nonblocking
            # writer connection proves that the locked runtime has a reader.
            deadline = time.monotonic() + 5
            while True:
                try:
                    fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
                    os.close(fd)
                    break
                except OSError as error:
                    import errno
                    if error.errno not in (errno.ENOENT, errno.ENXIO):
                        raise
                    self.assertIsNone(child.poll(), 'runtime exited before FIFO readiness')
                    self.assertLess(time.monotonic(), deadline, 'FIFO readiness timeout')
                    select.select([], [], [], 0.01)
        return child

    def line(self, child):
        assert child.stdout is not None
        self.assertTrue(select.select([child.stdout], [], [], 5)[0], 'event timeout')
        return child.stdout.readline().rstrip(b'\n')

    def test_buffered_framing_reconnect_and_signal(self):
        child = self.start(consumer=True)
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            self.assertGreaterEqual(os.fpathconf(fd, 'PC_PIPE_BUF'), 512)
            os.write(fd, b'x\n' * 200 + b'\n\0bad\n\xff\n')
            for _ in range(200):
                self.assertEqual(self.line(child), b'1:78')
            self.assertEqual(self.line(child), b'2:')
            self.assertEqual(self.line(child), b'3:')
            self.assertEqual(self.line(child), b'1:ff')
            os.write(fd, b'partial')
        finally:
            os.close(fd)
        self.assertEqual(self.line(child), b'3:')  # observed EOF handshake
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, b'new\n')
            self.assertEqual(self.line(child), b'1:6e6577')
        finally:
            os.close(fd)
        child.send_signal(signal.SIGTERM)
        _, error = child.communicate(timeout=5)
        self.assertEqual(child.returncode, 0, error)

    def test_foreground_signal_and_lock_lifecycle(self):
        for sig in (signal.SIGINT, signal.SIGTERM):
            child = self.start()
            contender = subprocess.run([str(BIN), 'init', *self.args], capture_output=True, timeout=5)
            self.assertEqual(contender.returncode, 1)
            self.assertEqual(subprocess.run([str(BIN), 'check', *self.args], capture_output=True, timeout=5).returncode, 0)
            child.send_signal(sig)
            out, err = child.communicate(timeout=5)
            self.assertEqual((child.returncode, out, err), (0, b'', b''))
            self.assertEqual(subprocess.run([str(BIN), 'init', *self.args], capture_output=True, timeout=5).returncode, 0)

    def test_no_fake_outbound_echo_and_invalid_policy(self):
        for payload in (b'PRIVATE_SECRET\n', b'bad\x00SECRET\n', b'x' * 4097 + b'\n'):
            child = self.start()
            log = self.fifo.with_name('out')
            log.write_bytes(b'preserved\x00\n')
            before = {p: p.stat().st_ino for p in (log, self.fifo)}
            fd = os.open(self.fifo, os.O_WRONLY)
            try:
                os.write(fd, payload)
            finally:
                os.close(fd)
            out, error = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 1)
            self.assertEqual(out, b'')
            self.assertNotIn(b'SECRET', error)
            self.assertEqual(log.read_bytes(), b'preserved\x00\n')
            self.assertEqual({p: p.stat().st_ino for p in before}, before)

    def test_idle_and_hup_cpu_and_shutdown(self):
        # wait4 reports this exact child's actual user+system CPU, not /proc
        # or aggregate sibling usage. select timeout is the measured interval,
        # never a guessed readiness sleep. A generous threshold catches spins.
        for hup in (False, True):
            child = self.start(consumer=True)
            assert child.stdout is not None
            if hup:
                fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
                os.write(fd, b'partial')
                os.close(fd)
                self.assertEqual(self.line(child), b'3:')
            start = time.monotonic()
            self.assertFalse(select.select([child.stdout], [], [], 0.5)[0])
            elapsed = time.monotonic() - start
            child.send_signal(signal.SIGINT)
            # A timeout guard still reaps on failure via the registered cleanup.
            self.assertTrue(select.select([child.stdout], [], [], 5)[0])
            pid, status, usage = os.wait4(child.pid, 0)
            self.assertEqual(pid, child.pid)
            child.returncode = os.waitstatus_to_exitcode(status)
            _, error = child.communicate(timeout=5)
            self.assertEqual(child.returncode, 0, error)
            cpu = usage.ru_utime + usage.ru_stime
            if not os.environ.get('GIN_RUNTIME_PREFIX'):
                self.assertLess(cpu, elapsed * 0.5 + 0.05, (hup, cpu, elapsed))
            print(f'CPU hup={hup}: child={cpu:.6f}s measured={elapsed:.6f}s')

    def test_stop_at_unmask_and_parser_boundaries(self):
        for mode in ('1', '2'):
            result = subprocess.run([str(REPO / '.build/runtime_boundaries'), str(self.root), mode], capture_output=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_full_stderr_signal_before_stdio_releases_root(self):
        import fcntl
        read_fd, write_fd = os.pipe()
        self.addCleanup(os.close, read_fd)
        self.addCleanup(os.close, write_fd)
        flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
        fcntl.fcntl(write_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        try:
            while True:
                os.write(write_fd, b'x' * 512)
        except BlockingIOError:
            pass
        fcntl.fcntl(write_fd, fcntl.F_SETFL, flags)
        # Filling the fixture can add native status bits.
        # Snapshot after our own writes, before the child, and compare all bits.
        flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
        self.assertFalse(flags & os.O_NONBLOCK)
        child = subprocess.Popen([str(REPO / '.build/runtime_stderr'), 'run', *self.args], stdout=subprocess.PIPE, stderr=write_fd)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        assert child.stdout is not None
        self.assertTrue(select.select([child.stdout], [], [], 5)[0])
        self.assertEqual(child.stdout.readline(), b'output\n')
        child.wait(timeout=2)
        self.assertIn(child.returncode, (0, -signal.SIGTERM))
        self.assertEqual(fcntl.fcntl(write_fd, fcntl.F_GETFL), flags)
        self.assertEqual(subprocess.run([str(BIN), 'init', *self.args], capture_output=True, timeout=5).returncode, 0)

    def test_full_stderr_error_output_has_no_root_lock(self):
        import fcntl
        read_fd, write_fd = os.pipe()
        self.addCleanup(os.close, read_fd)
        self.addCleanup(os.close, write_fd)
        child = subprocess.Popen([str(BIN), 'run', *self.args], stdout=subprocess.PIPE, stderr=write_fd)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        self.assertTrue(select.select([read_fd], [], [], 5)[0])
        self.assertIn(b'starting local input', os.read(read_fd, 512))
        deadline = time.monotonic() + 5
        while True:
            try:
                fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
                break
            except OSError as error:
                import errno
                if error.errno not in (errno.ENOENT, errno.ENXIO):
                    raise
                self.assertLess(time.monotonic(), deadline)
                select.select([], [], [], 0.01)
        flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
        fcntl.fcntl(write_fd, fcntl.F_SETFL, flags | os.O_NONBLOCK)
        try:
            while True:
                os.write(write_fd, b'x' * 512)
        except BlockingIOError:
            pass
        fcntl.fcntl(write_fd, fcntl.F_SETFL, flags)
        os.write(fd, b'unsupported\n')
        os.close(fd)
        # A real contender must acquire even if final diagnostics are blocked.
        while True:
            contender = subprocess.run([str(BIN), 'init', *self.args], capture_output=True, timeout=5)
            if contender.returncode == 0:
                break
            self.assertLess(time.monotonic(), deadline, 'stderr retained root lock')
            select.select([], [], [], 0.01)
        child.send_signal(signal.SIGTERM)
        child.wait(timeout=2)
        self.assertIn(child.returncode, (1, -signal.SIGTERM))
        self.assertEqual(fcntl.fcntl(write_fd, fcntl.F_GETFL), flags)

    def test_same_process_api_cleanup_and_restoration(self):
        result = subprocess.run([str(REPO / '.build/test_runtime_api'), str(self.root)], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_active_writer_flood_signal_shutdown(self):
        import sys
        child = self.start()
        code = '''import os, sys
fd = os.open(sys.argv[1], os.O_WRONLY)
record = b'\\n' * min(512, os.fpathconf(fd, 'PC_PIPE_BUF'))
os.write(fd, record)
print('writing', flush=True)
try:
    while True:
        os.write(fd, record)
except BrokenPipeError:
    pass
finally:
    os.close(fd)
'''
        writer = subprocess.Popen([sys.executable, '-c', code, str(self.fifo)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        def cleanup():
            if writer.poll() is None:
                writer.kill()
            writer.communicate(timeout=5)
        self.addCleanup(cleanup)
        self.assertEqual(self.line(writer), b'writing')
        child.send_signal(signal.SIGTERM)
        out, error = child.communicate(timeout=5)
        self.assertEqual((child.returncode, out, error), (0, b'', b''))
        _, error = writer.communicate(timeout=5)
        self.assertEqual(writer.returncode, 0, error)

    def test_sustained_empty_input_termination(self):
        child = self.start()
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            # Fill real kernel input while ignoring no unsupported messages.
            for _ in range(100):
                try:
                    os.write(fd, b'\n' * 512)
                except BlockingIOError:
                    break
            child.send_signal(signal.SIGTERM)
            out, error = child.communicate(timeout=5)
            self.assertEqual((child.returncode, out, error), (0, b'', b''))
        finally:
            os.close(fd)

    def test_validation_config_and_unsafe_startup_cleanup(self):
        invalid = subprocess.run([str(BIN), 'run', *self.args, '-T', '##'], capture_output=True, timeout=5)
        self.assertEqual(invalid.returncode, 2)
        self.assertFalse(self.root.exists())
        config = Path(self.temp.name) / 'config'
        config.write_bytes(b'token=PRIVATE_SECRET\n')
        config.chmod(0o600)
        invalid = subprocess.run([str(BIN), 'run', *self.args, '-f', str(config)], capture_output=True, timeout=5)
        self.assertEqual(invalid.returncode, 2)
        self.assertNotIn(b'PRIVATE_SECRET', invalid.stderr)
        self.assertFalse(self.root.exists())
        subprocess.run([str(BIN), 'init', *self.args], check=True, capture_output=True, timeout=5)
        for path in (self.fifo, self.fifo.with_name('out'), self.root / '.gin.lock'):
            path.chmod(0o644)
            failed = subprocess.run([str(BIN), 'run', *self.args], capture_output=True, timeout=5)
            self.assertEqual(failed.returncode, 1)
            self.assertEqual(path.stat().st_mode & 0o777, 0o644)
            path.chmod(0o600)
            self.assertEqual(subprocess.run([str(BIN), 'init', *self.args], capture_output=True, timeout=5).returncode, 0)

    @unittest.skipUnless(os.geteuid() == 0, 'Changing owners requires isolated root lane')
    def test_foreign_runtime_objects(self):
        subprocess.run([str(BIN), 'init', *self.args], check=True, capture_output=True, timeout=5)
        for path in (self.fifo, self.fifo.parent, self.root / '.gin.lock'):
            os.chown(path, 65534, -1)
            try:
                self.assertEqual(subprocess.run([str(BIN), 'run', *self.args], capture_output=True, timeout=5).returncode, 1)
            finally:
                os.chown(path, os.geteuid(), -1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
