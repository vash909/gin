"""Real FIFO API tests, with explicit pipe handshakes and bounded children."""
import os
from pathlib import Path
import select
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent
BIN = REPO / '.build/gin'
CONSUMER = REPO / '.build/fifo_consumer'


class InputTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get('TMPDIR', str(REPO / '.build/test-scratch'))).resolve()
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='gin-input-', dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / 'root'
        subprocess.run([str(BIN), 'init', '-d', str(self.root), '-n', 'Net',
                        '-c', '&Foo', '-T', '#&'], check=True, capture_output=True, timeout=5)
        self.fifo = self.root / '%4Eet/+26foo/in'

    def line(self, child):
        self.assertTrue(select.select([child.stdout], [], [], 5)[0], 'consumer timeout')
        return child.stdout.readline().rstrip(b'\n')

    def start(self, expected=b'ready'):
        child = subprocess.Popen([str(CONSUMER), str(self.root)], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, bufsize=0)
        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        self.assertEqual(self.line(child), expected)
        return child

    def next(self, child):
        child.stdin.write(b'n')
        raw = self.line(child)
        status, data = raw.split(b':')
        return int(status), bytes.fromhex(data.decode())

    def finish(self, child):
        _, error = child.communicate(input=b'q', timeout=5)
        self.assertEqual(child.returncode, 0, error)

    def writer(self):
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        self.addCleanup(os.close, fd)
        return fd

    def event(self, child):
        for _ in range(100):
            result = self.next(child)
            if result[0] != 0:
                return result
        self.fail('bounded reader did not produce event')

    def feed_partial(self, child, writer, data):
        # Drain each <=256-byte fragment before the next write: independent of
        # pipe capacity, single writer only, deliberately not an atomic record.
        for start in range(0, len(data), 256):
            part = data[start:start + 256]
            self.assertEqual(os.write(writer, part), len(part))
            self.assertEqual(self.next(child), (0, b''))

    def test_invalid_whole_records_resynchronize(self):
        child = self.start()
        writer = self.writer()
        # Byte policy: C0 (including tab/CR), DEL rejected; high bytes opaque.
        for data in (b'prefix\x00tail', b'x\rtail', b'x\ttail', b'x\x1btail',
                     b'x\x7ftail', b'x' * 4097, b'x' * 16384,
                     b'x' * 4096 + b'\x00'):
            self.feed_partial(child, writer, data)
            self.assertEqual(os.write(writer, b'\nok\n'), 4)
            self.assertEqual(self.event(child), (3, b''))
            self.assertEqual(self.event(child), (1, b'ok'))
        self.feed_partial(child, writer, b'x' * 4096)
        self.assertEqual(os.write(writer, b'\n\n\xff\x80\n'), 5)
        self.assertEqual(self.event(child), (1, b'x' * 4096))
        self.assertEqual(self.event(child), (2, b''))
        self.assertEqual(self.event(child), (1, b'\xff\x80'))
        self.finish(child)

    def test_partial_eagain_coalescing_and_observed_eof(self):
        child = self.start()
        self.assertEqual(self.next(child), (6, b''))  # no writer, no blocking
        self.assertEqual(self.next(child), (6, b''))
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            self.assertEqual(self.next(child), (4, b''))
            self.assertEqual(os.write(fd, b''), 0)
            self.assertEqual(self.next(child), (4, b''))
            os.write(fd, b'par')
            self.assertEqual(self.next(child), (0, b''))
            self.assertEqual(self.next(child), (4, b''))
            os.write(fd, b'tial\nsecond\n\n')
            self.assertEqual(self.next(child), (1, b'partial'))
            self.assertEqual(self.next(child), (1, b'second'))
            self.assertEqual(self.next(child), (2, b''))
            os.write(fd, b'unterminated')
            self.assertEqual(self.next(child), (0, b''))
        finally:
            os.close(fd)
        self.assertEqual(self.next(child), (3, b''))
        self.assertEqual(self.next(child), (6, b''))
        self.assertEqual(self.next(child), (6, b''))
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, b'new\n')
            self.assertEqual(self.next(child), (1, b'new'))
            os.write(fd, b'\x00bad')
            self.assertEqual(self.next(child), (0, b''))
        finally:
            os.close(fd)
        self.assertEqual(self.next(child), (3, b''))
        self.assertEqual(self.next(child), (6, b''))
        self.finish(child)

    def test_unobserved_writer_handoff_has_no_session_identity(self):
        child = self.start()
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        os.write(fd, b'first')
        self.assertEqual(self.next(child), (0, b''))
        os.close(fd)
        fd = os.open(self.fifo, os.O_WRONLY | os.O_NONBLOCK)
        try:
            os.write(fd, b'second\n')
            self.assertEqual(self.next(child), (1, b'firstsecond'))
        finally:
            os.close(fd)
        self.finish(child)

    def test_many_coalesced_lines_are_not_lost(self):
        child = self.start()
        writer = self.writer()
        for _ in range(4):
            self.assertEqual(os.write(writer, b'x\n' * 256), 512)
            for _ in range(256):
                self.assertEqual(self.next(child), (1, b'x'))
        self.assertEqual(self.next(child), (4, b''))
        self.finish(child)

    def test_atomic_concurrent_writers(self):
        child = self.start()
        # Query this real FIFO, not a hardcoded PIPE_BUF or the framing limit.
        anchor = self.writer()
        pipe_buf = os.fpathconf(anchor, 'PC_PIPE_BUF')
        self.assertGreaterEqual(pipe_buf, 512)
        code = '''import os, sys
fd = os.open(sys.argv[1], os.O_WRONLY)
limit = os.fpathconf(fd, 'PC_PIPE_BUF')
print('ready', flush=True)
assert sys.stdin.buffer.read(1) == b'g'
for i in range(20):
    record = (sys.argv[2] + ':' + str(i)).encode() + b'\\n'
    assert len(record) <= limit
    assert os.write(fd, record) == len(record)
os.close(fd)
'''
        import sys
        writers = []
        for identity in ('A', 'B'):
            proc = subprocess.Popen([sys.executable, '-c', code, str(self.fifo), identity],
                                    stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                    stderr=subprocess.PIPE, bufsize=0)
            def cleanup(proc=proc):
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=5)
            self.addCleanup(cleanup)
            writers.append(proc)
        for proc in writers:
            self.assertEqual(self.line(proc), b'ready')
        for proc in writers:
            proc.stdin.write(b'g')
        # At most 180 bytes per writer: both fit even the POSIX minimum pipe
        # capacity. Wait for actual exit so no sleep/retry timing assumptions.
        for proc in writers:
            _, error = proc.communicate(timeout=5)
            self.assertEqual(proc.returncode, 0, error)
        received = [self.event(child) for _ in range(40)]
        self.assertEqual(sorted(received), sorted((1, f'{who}:{i}'.encode())
                         for who in ('A', 'B') for i in range(20)))
        self.assertEqual(self.next(child), (4, b''))
        self.finish(child)

    def test_unsafe_existing_inputs_do_not_mutate_or_block(self):
        for kind in ('missing', 'symlink', 'hardlink', 'regular', 'directory',
                     'mode000', 'mode400', 'mode200', 'mode640', 'mode1600', 'mode4600'):
            with self.subTest(kind=kind):
                self.fifo.unlink()
                victim = self.root / ('victim-' + kind)
                os.mkfifo(victim, 0o600)
                if kind == 'symlink':
                    self.fifo.symlink_to(victim)
                elif kind == 'hardlink':
                    os.link(victim, self.fifo)
                elif kind == 'regular':
                    self.fifo.write_bytes(b'not a pipe')
                    self.fifo.chmod(0o600)
                elif kind == 'directory':
                    self.fifo.mkdir(mode=0o700)
                elif kind.startswith('mode'):
                    os.mkfifo(self.fifo, 0o600)
                    self.fifo.chmod(int(kind[4:], 8))
                paths = [p for p in self.root.rglob('*')]
                before = {p: (p.lstat().st_ino, p.lstat().st_mode, p.lstat().st_nlink,
                              p.lstat().st_uid, p.lstat().st_size) for p in paths}
                child = self.start(b'unsafe')
                contender = subprocess.run([str(BIN), 'init', '-d', str(self.root),
                                            '-n', 'other', '-c', '#other'],
                                           capture_output=True, timeout=5)
                self.assertEqual(contender.returncode, 1)
                self.assertFalse((self.root / 'other').exists())
                self.finish(child)
                self.assertEqual({p: (p.lstat().st_ino, p.lstat().st_mode, p.lstat().st_nlink,
                                     p.lstat().st_uid, p.lstat().st_size) for p in paths}, before)
                if self.fifo.is_dir():
                    self.fifo.rmdir()
                elif self.fifo.exists() or self.fifo.is_symlink():
                    self.fifo.unlink()
                victim.unlink()
                os.mkfifo(self.fifo, 0o600)

    def test_symlink_and_unsafe_window_ancestors_rejected(self):
        for directory in (self.fifo.parent, self.fifo.parent.parent):
            moved = directory.with_name(directory.name + '-saved')
            directory.rename(moved)
            try:
                directory.symlink_to(moved, target_is_directory=True)
                self.finish(self.start(b'unsafe'))
                directory.unlink()
            finally:
                moved.rename(directory)
            directory.chmod(0o755)
            try:
                self.finish(self.start(b'unsafe'))
            finally:
                directory.chmod(0o700)

    @unittest.skipUnless(os.geteuid() == 0, 'Changing owners requires isolated root lane')
    def test_foreign_fifo_and_ancestor_owner_rejected(self):
        for path in (self.fifo, self.fifo.parent, self.fifo.parent.parent):
            os.chown(path, 65534, -1)
            try:
                self.finish(self.start(b'unsafe'))
                self.assertEqual(path.stat().st_uid, 65534)
            finally:
                os.chown(path, os.geteuid(), -1)

    def test_every_declared_control_is_a_whole_record_rejection(self):
        child = self.start()
        writer = self.writer()
        for byte in [*range(10), *range(11, 32), 127]:
            os.write(writer, b'prefix' + bytes([byte]) + b'tail\nok\n')
            self.assertEqual(self.next(child), (3, b''))
            self.assertEqual(self.next(child), (1, b'ok'))
        os.write(writer, b'/join $HOME `echo nope` \\ literal\n')
        self.assertEqual(self.next(child), (1, b'/join $HOME `echo nope` \\ literal'))
        self.finish(child)

    def test_input_open_never_creates_missing_layout_or_touches_log(self):
        log = self.fifo.with_name('out')
        log.unlink()
        child = self.start()
        self.assertFalse(log.exists())
        self.finish(child)
        self.assertFalse(log.exists())
        for directory in (self.fifo.parent, self.fifo.parent.parent):
            moved = directory.with_name(directory.name + '-saved')
            directory.rename(moved)
            try:
                self.finish(self.start(b'unsafe'))
                self.assertFalse(directory.exists())
            finally:
                moved.rename(directory)

    def test_actual_fifo_record_and_held_root(self):
        child = self.start()
        writer = self.writer()
        self.assertEqual(os.write(writer, b'hello\n'), 6)
        self.assertEqual(self.next(child), (1, b'hello'))
        result = subprocess.run([str(BIN), 'init', '-d', str(self.root), '-n', 'other',
                                 '-c', '#other'], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 1)
        self.finish(child)


if __name__ == '__main__':
    unittest.main(verbosity=2)
