"""Private owner CLI clock faults through real loopback TLS/WSS startup."""
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import unittest
from unittest import mock
from typing import cast
import test_session as startup
import test_session_outbound as outbound

class StopHandler(startup.Handler):
    def do_POST(self):
        srv = cast(startup.Server, self.server)
        assert self.path == '/networks/NetA/channels/%23Room/messages'
        assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
        raw = self.rfile.read(int(self.headers['Content-Length']))
        assert json.loads(raw) == {'body': outbound.TEXT}
        srv.paths.append(self.path)
        body = b'{"ok":true}'
        self.send_response(202)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()

class OwnerFaults(unittest.TestCase):
    certdir: Path
    tls = startup.StartupTracer.tls
    command = startup.StartupTracer.command
    def fixture(self):
        return startup.StartupTracer.fixture(cast(startup.StartupTracer, self))

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    def run_clock(self, fault):
        with self.fixture() as (srv, origin):
            srv.release_final.set()
            srv.client_close_expected.set()
            root = self.certdir / self._testMethodName
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin,
                'token': startup.TOKEN, 'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            original = credential.read_bytes()
            settings = self.certdir / 'settings'
            settings.write_text('startup_ms=180000\n' if startup.PREFIX else '')
            settings.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root,
                '-n', 'NetA', '-c', '#Room', '-e', origin, '-m', 'plain',
                '-a', self.certdir / 'ca.pem', '-R', rw),
                env=dict(os.environ, GIN_OWNER_FAULT=fault), pass_fds=(rw,),
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(rw)
            writer = -1
            try:
                self.assertTrue(select.select([rr], [], [], outbound.budget(10))[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                # Establish actual data consumption followed by writer EOF.
                # A virgin nonblocking FIFO need not report EOF on every OS.
                writer = os.open(next(root.rglob('in')), os.O_WRONLY | os.O_NONBLOCK)
                self.assertGreaterEqual(os.fpathconf(writer, 'PC_PIPE_BUF'), 1)
                self.assertEqual(os.write(writer, b'\n'), 1)
                os.close(writer)
                writer = -1
                if not fault:
                    self.assertFalse(select.select([rr], [], [], .3)[0])
                    self.assertIsNone(proc.poll())
                    proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=outbound.budget(12))
                self.assertNotIn(b'runtime error:', stderr, 'SEMANTIC_RED: actual FIFO EOF deadline overflow')
                self.assertEqual(proc.returncode, 1 if fault else 0, stderr)
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'gin: session I/O failure\n' if fault else b'')
                self.assertEqual(srv.paths, [
                    '/api/config', '/me', '/boot', '/api/config', '/me',
                    '/socket/websocket?vsn=2.0.0&client_proto=36',
                    '/api/config', '/me',
                    '/networks/NetA/channels/%23Room/messages?limit=200&after=0'])
                self.assertEqual(next(root.rglob('out')).read_bytes(), b'')
                self.assertEqual(credential.read_bytes(), original)
            finally:
                srv.stopped.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=outbound.budget(10))
                if writer >= 0:
                    os.close(writer)
                os.close(rr)

    def test_pending_signal_prevents_new_http(self):
        with mock.patch.object(startup, 'Handler', StopHandler), self.fixture() as (srv, origin):
            srv.release_final.set()
            srv.client_close_expected.set()
            root = self.certdir / self._testMethodName
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin,
                'token': startup.TOKEN, 'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            original = credential.read_bytes()
            rr, rw = os.pipe()
            opread, opwrite = os.pipe()
            for fd in (rr, rw, opread, opwrite):
                os.set_blocking(fd, False)
            proc = subprocess.Popen(self.command('connect', '-d', root,
                '-n', 'NetA', '-c', '#Room', '-e', origin, '-m', 'plain',
                '-a', self.certdir / 'ca.pem', '-R', rw, '-O', opwrite),
                env=dict(os.environ, GIN_OWNER_FAULT='pending-stop'),
                pass_fds=(rw, opwrite), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(rw)
            os.close(opwrite)
            writer = -1
            try:
                self.assertTrue(select.select([rr], [], [], outbound.budget(10))[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                paths = list(srv.paths)
                writer = os.open(next(root.rglob('in')), os.O_WRONLY | os.O_NONBLOCK)
                record = (outbound.TEXT + '\n').encode()
                self.assertEqual(os.write(writer, record), len(record))
                stdout, stderr = proc.communicate(timeout=outbound.budget(12))
                self.assertEqual(srv.paths, paths,
                    'SEMANTIC_RED: new HTTP after synchronous PENDING stop signal')
                self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                data = bytearray()
                while True:
                    try:
                        part = os.read(opread, 4096)
                    except BlockingIOError:
                        break
                    if not part:
                        break
                    data.extend(part)
                self.assertEqual(bytes(data),
                    b'OP1 1 SEND PENDING 0\nOP1 1 SEND NOT_ATTEMPTED 1\n')
                self.assertEqual(next(root.rglob('out')).read_bytes(), b'')
                self.assertEqual(credential.read_bytes(), original)
            finally:
                srv.stopped.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=outbound.budget(10))
                if writer >= 0:
                    os.close(writer)
                os.close(rr)
                os.close(opread)

    def test_clock_control(self):
        self.run_clock('')

    def test_fifo_eof_deadline_overflow(self):
        self.run_clock('clock-max')

class OwnerClockProtocol(unittest.TestCase):
    """No inherited HttpControl tests; each command starts at a real HTTP hold."""
    certdir: Path
    tls = startup.StartupTracer.tls
    def command(self, *args):
        # Always select the private role, including direct protocol-only runs.
        binary = os.environ.get("GIN_OWNER_BINARY", str(startup.REPO / ".build/gin_owner_fault"))
        return [*startup.PREFIX, binary, *map(str, args)]
    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    def clock_case(self, payload=b'E\n', partial=False, eof=False, bad_fd=None, bad_ack=None, rdwr=False):
        from owner_clock_fixture import OwnerClock
        from test_session_http_control import HttpControl
        with HttpControl.fixture(cast(HttpControl, self), '/api/config', 1) as (srv, origin), OwnerClock(self) as clock:
            root = self.certdir / (self._testMethodName + '-' + str(bad_fd or bad_ack or 'normal'))
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin,
                'token': startup.TOKEN, 'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            settings = self.certdir / 'clock-settings'
            settings.write_text('startup_ms=180000\nconnect_ms=10000\ntotal_ms=20000\n')
            settings.chmod(0o600)
            if rdwr:
                # Real Linux FIFO capability control, not FreeBSD emulation.
                # Native FreeBSD anonymous endpoints already expose O_RDWR.
                clock.close()
                for name in ('command', 'ack'):
                    path = root / ('clock-' + name)
                    os.mkfifo(path, 0o600)
                    endpoint = os.open(path, os.O_RDWR | os.O_NONBLOCK)
                    clock._fds.add(endpoint)
                    peer = os.dup(endpoint)
                    clock._fds.add(peer)
                    if name == 'command':
                        clock.command_reader, clock.command_writer = endpoint, peer
                    else:
                        clock.ack_reader, clock.ack_writer = endpoint, peer
                clock.child_fds = (clock.command_reader, clock.ack_writer)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            environment = clock.environment()
            passed = [*clock.child_fds, rw]
            if bad_fd == 'canonical':
                environment['GIN_OWNER_CLOCK_COMMAND_FD'] = '0' + str(clock.command_reader)
            elif bad_fd in ('direction', 'shared-pipe'):
                # pipe() endpoints can both expose O_RDWR on FreeBSD. Named
                # FIFO endpoints establish real direction/identity negatives.
                channel = root / 'clock-invalid-channel'
                os.mkfifo(channel, 0o600)
                reader = os.open(channel, os.O_RDONLY | os.O_NONBLOCK)
                self.addCleanup(os.close, reader)
                writer = os.open(channel, os.O_WRONLY | os.O_NONBLOCK)
                self.addCleanup(os.close, writer)
                environment['GIN_OWNER_CLOCK_COMMAND_FD'] = str(writer if bad_fd == 'direction' else reader)
                if bad_fd == 'shared-pipe':
                    environment['GIN_OWNER_CLOCK_ACK_FD'] = str(writer)
                passed.extend((reader, writer))
            elif bad_fd == 'blocking':
                os.set_blocking(clock.command_reader, True)

            elif bad_fd == 'status-alias':
                environment['GIN_OWNER_CLOCK_ACK_FD'] = str(rw)
            elif bad_fd == 'regular':
                regular = os.open(settings, os.O_RDONLY | os.O_NONBLOCK)
                self.addCleanup(os.close, regular)
                environment['GIN_OWNER_CLOCK_COMMAND_FD'] = str(regular)
                passed.append(regular)
            if bad_ack == 'full':
                for _ in range(256):
                    try:
                        os.write(clock.ack_writer, b'x' * 4096)
                    except BlockingIOError:
                        break
                else:
                    self.fail('SETUP: ACK pipe did not reach bounded saturation')
            elif bad_ack == 'closed':
                os.close(clock.ack_reader)
                clock._fds.remove(clock.ack_reader)
            proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root,
                '-n', 'NetA', '-c', '#Room', '-e', origin, '-m', 'plain',
                '-a', self.certdir / 'ca.pem', '-R', rw),
                env=environment, pass_fds=tuple(set(passed)),
                stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(rw)
            clock.parent_after_spawn()
            try:
                if bad_fd:
                    stdout, stderr = proc.communicate(timeout=15 if startup.PREFIX else 3)
                    self.assertEqual((proc.returncode, stdout, stderr), (1, b'', b'gin: session I/O failure\n'))
                    self.assertEqual(srv.paths, [], 'invalid descriptor reached HTTP')
                    self.assertEqual(os.read(clock.ack_reader, 64), b'', 'invalid descriptor produced an ACK')
                    self.assertEqual(os.read(rr, 128), b'')
                    return
                entered = srv.request_seen.wait(30 if startup.PREFIX else 5)
                if not entered and proc.poll() is not None:
                    stdout, stderr = proc.communicate(timeout=1)
                    self.fail('SETUP: held initial HTTP phase not entered; owner=' +
                              repr((proc.returncode, stdout, stderr)))
                self.assertTrue(entered, 'SETUP: held initial HTTP phase not entered')
                self.assertIsNone(proc.poll(), 'SETUP: owner exited before injection')
                if partial:
                    self.assertEqual(os.write(clock.command_writer, b'A'), 1)
                    self.assertFalse(select.select([clock.ack_reader], [], [], .15)[0])
                    self.assertIsNone(proc.poll())
                    self.assertEqual(os.write(clock.command_writer, b'\n'), 1)
                    clock.wait_ack(proc, 'A', 10 if startup.PREFIX else 2)
                    clock._state = 'A'
                if bad_ack:
                    self.assertEqual(os.write(clock.command_writer, b'E\n'), 2)
                elif eof:
                    os.close(clock.command_writer)
                    clock._fds.remove(clock.command_writer)
                elif payload in (b'E\n', b'B\n') and (partial or payload == b'E\n'):
                    clock.advance(proc, payload.decode().strip(), 10 if startup.PREFIX else 2)
                else:
                    self.assertEqual(os.write(clock.command_writer, payload), len(payload))
                stdout, stderr = proc.communicate(timeout=15 if startup.PREFIX else 3)
                self.assertEqual((proc.returncode, stdout, stderr),
                    (1, b'', b'gin: startup timeout\n' if not bad_ack and ((partial and payload == b'B\n') or payload == b'E\n')
                     else b'gin: session I/O failure\n'))
                self.assertFalse(srv.release_response.is_set())
                self.assertEqual(os.read(rr, 128), b'', 'READY escaped held bootstrap')
                self.assertEqual(srv.paths, ['/api/config'])
            finally:
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=15 if startup.PREFIX else 5)
                srv.stopped.set()
                os.close(rr)

    def test_e_timeout(self):
        self.clock_case()

    def test_rdwr_capabilities_reach_held_phase_and_timeout(self):
        self.clock_case(rdwr=True)

    def test_partial_a_then_b(self):
        self.clock_case(b'B\n', partial=True)

    def test_bad_frame(self):
        self.clock_case(b'A\x00')

    def test_coalesced(self):
        self.clock_case(b'A\nB\n')

    def test_wrong_order(self):
        self.clock_case(b'B\n')

    def test_eof(self):
        self.clock_case(b'', eof=True)

    def test_replay(self):
        self.clock_case(b'A\n', partial=True)


    def test_descriptor_validation_before_http(self):
        for kind in ('canonical', 'direction', 'blocking', 'shared-pipe', 'status-alias', 'regular'):
            with self.subTest(kind=kind):
                self.clock_case(bad_fd=kind)

    def test_ack_reader_closed_is_io_not_sigpipe_or_timeout(self):
        self.clock_case(bad_ack='closed')

    def test_ack_backpressure_is_io_not_timeout(self):
        self.clock_case(bad_ack='full')


class OwnerClockResources(unittest.TestCase):
    def test_short_ack_reads_reassemble_one_record(self):
        from owner_clock_fixture import OwnerClock
        with OwnerClock(self) as clock:
            self.assertEqual(os.write(clock.ack_writer, b'A\n'), 2)
            real_read = os.read
            def fragmented(fd, count):
                return real_read(fd, 1 if fd == clock.ack_reader else count)
            proc = mock.Mock()
            proc.poll.return_value = None
            with mock.patch('owner_clock_fixture.os.read', side_effect=fragmented):
                clock.wait_ack(proc, 'A', 1)

    def test_bad_or_surplus_ack_and_eof_fail(self):
        from owner_clock_fixture import OwnerClock
        for record in (b'B\n', b'A\nA\n', b''):
            with self.subTest(record=record), OwnerClock(self) as clock:
                if record:
                    os.write(clock.ack_writer, record)
                else:
                    os.close(clock.ack_writer)
                    clock._fds.remove(clock.ack_writer)
                proc = mock.Mock()
                proc.poll.return_value = None
                with self.assertRaisesRegex(AssertionError, 'clock ACK'):
                    clock.wait_ack(proc, 'A', 1)

    def test_parent_ack_deadline_is_independent(self):
        from owner_clock_fixture import OwnerClock
        with OwnerClock(self) as clock:
            proc = mock.Mock()
            proc.poll.return_value = None
            with mock.patch('owner_clock_fixture.time.monotonic', side_effect=[0, 1]):
                with self.assertRaisesRegex(AssertionError, 'clock ACK timeout'):
                    clock.wait_ack(proc, 'A', .1)
            proc.poll.assert_called_once()

    def test_close_error_still_releases_other_endpoints(self):
        from owner_clock_fixture import OwnerClock
        clock = OwnerClock(self)
        endpoints = tuple(clock._fds)
        os.close(clock.command_reader)  # Deliberate caller misuse must not leak siblings.
        with self.assertRaises(OSError):
            clock.close()
        self.assertFalse(clock._fds)
        for fd in endpoints:
            with self.assertRaises(OSError):
                os.fstat(fd)
        clock.close()  # Idempotent after all endpoints have been handled.

    def test_partial_pipe_allocation_closes_acquired_endpoints(self):
        from owner_clock_fixture import OwnerClock
        endpoints = os.pipe()
        try:
            with mock.patch('owner_clock_fixture.os.pipe', side_effect=[endpoints, OSError('allocation failure')]):
                with self.assertRaisesRegex(OSError, 'allocation failure'):
                    OwnerClock(self)
            for fd in endpoints:
                with self.assertRaises(OSError):
                    os.fstat(fd)
        finally:
            for fd in endpoints:
                try:
                    os.close(fd)
                except OSError:
                    pass  # Test cleanup also reaps the deliberately failing old helper.


if __name__ == '__main__':
    unittest.main(verbosity=2)
