"""Actual production-executable startup against synthetic verified HTTPS/WSS."""
import base64
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import select
import shlex
import signal
import socket
import subprocess
import sys
import threading
import time
import unittest
from typing import cast

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests'))
import test_http
from ws_fixture_frames import server_frame, take_client_frame

TOKEN = 'aa~'
UUID = '12345678-1234-1234-1234-123456789abc'
PREFIX = shlex.split(os.environ.get('GIN_SESSION_PREFIX', ''))


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = False
    paths: list[str]
    joins: list[str]
    errors: list[str]
    final_requested: threading.Event
    release_final: threading.Event
    stopped: threading.Event
    client_closed: threading.Event
    client_close_expected: threading.Event

    def handle_error(self, request, client_address):
        # This positive TLS fixture must never silently swallow handler faults.
        self.errors.append(type(sys.exc_info()[1]).__name__)


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        srv = cast(Server, self.server)
        srv.paths.append(self.path)
        if self.path == '/api/config':
            assert self.headers.get('Authorization') is None, 'credentialed discovery'
            body = {'server': 'grappa', 'protocol_version': 7, 'min_protocol_version': 1}
        elif self.path == '/me':
            assert self.headers.get('Authorization') == 'Bearer ' + TOKEN, 'invalid identity authorization'
            body = {'kind': 'user', 'id': UUID, 'name': 'Alice'}
        elif self.path == '/boot':
            assert self.headers.get('Authorization') == 'Bearer ' + TOKEN, 'invalid boot authorization'
            body = {'networks': [{'id': 3000000001, 'slug': 'NetA', 'nick': 'OwnA'}],
                    'channels': {'NetA': [{'name': '#Room', 'joined': True, 'source': 'joined'}]},
                    'heads': {}}
        elif self.path == '/socket/websocket?vsn=2.0.0&client_proto=4':
            self.upgrade()
            return
        else:
            raise AssertionError('unexpected resource')
        raw = json.dumps(body).encode()
        self.send_response(200)
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def upgrade(self):
        srv = cast(Server, self.server)
        assert self.headers.get('Authorization') is None, 'upgrade bearer header'
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+', 'invalid subprotocol offer'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        self.send_header('Upgrade', 'websocket')
        self.send_header('Connection', 'Upgrade')
        self.send_header('Sec-WebSocket-Accept', accept)
        self.send_header('Sec-WebSocket-Protocol', 'phoenix')
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(30 if PREFIX else 8)
        buffered = bytearray()
        try:
            for index in range(3):
                while True:
                    item = take_client_frame(buffered)
                    if item is not None:
                        break
                    data = self.connection.recv(16384)
                    assert data, 'client closed before required subscription'
                    buffered.extend(data)
                opcode, final, payload = item
                assert opcode == 1 and final, 'unexpected non-text join'
                frame = json.loads(payload)
                jr, ref, topic, event, body = frame
                assert event == 'phx_join' and body == {} and jr == ref, 'invalid join shape'
                srv.joins.append(topic)
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': 7} if index == 0 else {}}]
                if index == 2:
                    srv.final_requested.set()
                    assert srv.release_final.wait(60 if PREFIX else 8), 'final-ACK barrier expired'
                    if srv.stopped.is_set():
                        return
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
            # Remain open; only the foreground owner may end this test session.
            while not srv.stopped.is_set():
                data = self.connection.recv(16384)
                if not data:
                    srv.client_closed.set()
                    return
                buffered.extend(data)
                while True:
                    item = take_client_frame(buffered)
                    if item is None:
                        break
                    assert item[0] in (8, 10), 'unexpected application operation'
        except OSError:
            if not srv.stopped.is_set() and not (srv.client_close_expected.is_set() and len(srv.joins) == 3):
                raise
        finally:
            self.close_connection = True


class StartupTracer(unittest.TestCase):
    certdir: Path
    tls = test_http.HTTPTests.tls

    def command(self, *args):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        return [*PREFIX, binary, *map(str, args)]
    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self):
        srv = Server(('127.0.0.1', 0), Handler)
        for name in ['final_requested', 'release_final', 'stopped', 'client_closed', 'client_close_expected']:
            setattr(srv, name, threading.Event())
        srv.paths, srv.joins, srv.errors = [], [], []
        ctx = self.tls()
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.release_final.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'positive fixture handler failed')

    def test_connected_check_is_read_only_and_does_not_probe_ca(self):
        root = self.certdir / 'check-must-not-exist'
        proc = subprocess.run(self.command('connect-check', '-d', root, '-n', 'NetA',
            '-c', '#Room', '-e', 'https://127.0.0.1:9', '-m', 'plain',
            '-a', root / 'missing-ca.pem'), capture_output=True, timeout=30 if PREFIX else 5)
        self.assertEqual(proc.returncode, 0, 'missing read-only connected-check frontdoor')
        self.assertEqual(proc.stdout + proc.stderr, b'')
        self.assertFalse(root.exists(), 'read-only check mutated root')

    def test_actual_executable_observes_final_correlated_ready_and_releases_root(self):
        with self.fixture() as (srv, origin):
            root = self.certdir / 'root'
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': TOKEN,
                'subject_id': UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            before = credential.read_bytes()
            read_fd, write_fd = os.pipe()
            os.set_blocking(read_fd, False)
            os.set_blocking(write_fd, False)
            settings = self.certdir / 'session-settings'
            settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n' if PREFIX else '')
            settings.chmod(0o600)
            proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                '-R', write_fd), pass_fds=(write_fd,), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(write_fd)
            try:
                deadline = time.monotonic() + (90 if PREFIX else 10)
                while not srv.final_requested.wait(.02):
                    self.assertIsNone(proc.poll(), 'production connected frontdoor exited before required joins')
                    self.assertLess(time.monotonic(), deadline, 'required-join barrier expired')
                self.assertFalse(select.select([read_fd], [], [], .1)[0], 'READY before final correlated ACK')
                self.assertIsNone(proc.poll(), 'foreground owner stopped during held ACK')
                srv.release_final.set()
                self.assertTrue(select.select([read_fd], [], [], 30 if PREFIX else 5)[0], 'no actual client READY')
                self.assertEqual(os.read(read_fd, 128), b'READY\n', 'invalid readiness record')
                self.assertIsNone(proc.poll(), 'READY was not a live foreground session')
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                    '/socket/websocket?vsn=2.0.0&client_proto=4'])
                self.assertEqual(srv.joins, ['grappa:user:Alice', 'grappa:user:Alice/network:NetA',
                    'grappa:user:Alice/network:NetA/channel:#room'])
                srv.client_close_expected.set()
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=60 if PREFIX else 12)
                self.assertEqual(proc.returncode, 0, 'orderly SIGTERM did not exit zero')
                self.assertEqual(stdout, b'', 'unexpected ordinary output')
                for secret in [TOKEN.encode(), b'YWF+']:
                    self.assertNotIn(secret, stdout + stderr, 'credential disclosed')
                self.assertEqual(credential.read_bytes(), before, 'connect mutated credential store')
                self.assertFalse((root / 'NetA').exists(), 'session-only stage projected chat files')
                check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                    capture_output=True, timeout=30 if PREFIX else 5)
                self.assertEqual(check.returncode, 0, 'root lock leaked after session teardown')
                self.assertTrue((root / '.gin.lock').is_file(), 'persistent lock was deleted')
            finally:
                srv.stopped.set()
                srv.release_final.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=30 if PREFIX else 5)
                os.close(read_fd)


if __name__ == '__main__':
    unittest.main(verbosity=2)
