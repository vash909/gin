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
            try:
                self.assertTrue(select.select([rr], [], [], outbound.budget(10))[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                if not fault:
                    self.assertFalse(select.select([rr], [], [], .3)[0])
                    self.assertIsNone(proc.poll())
                    proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=outbound.budget(12))
                self.assertNotIn(b'runtime error:', stderr, 'SEMANTIC_RED: actual FIFO EOF deadline overflow')
                self.assertEqual(proc.returncode, 1 if fault else 0, stderr)
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'gin: session I/O failure\n' if fault else b'')
                self.assertEqual(len(srv.paths), 6)
                self.assertEqual(next(root.rglob('out')).read_bytes(), b'')
                self.assertEqual(credential.read_bytes(), original)
            finally:
                srv.stopped.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=outbound.budget(10))
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

if __name__ == '__main__':
    unittest.main(verbosity=2)
