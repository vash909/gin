#!/usr/bin/env python3
"""Real TLS login; cancellation never proves server session rollback."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import threading
import time
import traceback
from typing import ClassVar, cast
import unittest
import test_http

ROOT = Path(__file__).resolve().parents[1]


class StrictServer(http.server.ThreadingHTTPServer):
    errors: list[str]
    records: list[tuple[str, str]]
    entered: threading.Event
    release: threading.Event
    held: bool
    status: int
    body: bytes

    def handle_error(self, request, client_address):
        self.errors.append(traceback.format_exc())


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_POST(self):
        srv = cast(StrictServer, self.server)
        self.connection.settimeout(3)
        raw = self.rfile.read(int(self.headers['Content-Length']))
        assert self.path == '/auth/login'
        assert self.headers['Content-Type'] == 'application/json'
        assert self.headers.get('Authorization') is None
        assert self.headers.get('Cookie') is None
        # Boolean assertion avoids rendering synthetic credentials in failures.
        assert json.loads(raw) == {'identifier': 'control-user',
                                   'password': 'synthetic-password-LC'}, 'login body mismatch'
        srv.records.append(('POST', self.path))
        srv.entered.set()  # Actual POST entry, never a timing approximation.
        if srv.held:
            assert srv.release.wait(8), 'manual response gate not released'
            self.close_connection = True
            return  # No headers on a deliberately cancelled connection.
        self.send_response(srv.status)
        self.send_header('Content-Length', str(len(srv.body)))
        self.end_headers()
        self.wfile.write(srv.body)


class LoginControl(unittest.TestCase):
    certdir: ClassVar[Path]
    tls = test_http.HTTPTests.tls

    @classmethod
    def setUpClass(cls):
        # Existing certificate generator, with explicit private scratch fallback.
        scratch = os.environ.get('TMPDIR') or str(ROOT / '.build')
        Path(scratch).mkdir(parents=True, exist_ok=True)
        old = tempfile.tempdir
        tempfile.tempdir = scratch
        try:
            test_http.HTTPTests.setUpClass.__func__(cls)
        finally:
            tempfile.tempdir = old

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, held=False, status=200, body=None):
        srv = StrictServer(('127.0.0.1', 0), Handler)
        srv.errors, srv.records = [], []
        srv.entered, srv.release = threading.Event(), threading.Event()
        srv.held, srv.status = held, status
        srv.body = body if body is not None else json.dumps({
            'token': 'synthetic-token-LC', 'subject': {'kind': 'user',
            'id': 'abcdefab-2345-4567-89ab-abcdefabcdef',
            'name': 'ControlUser'}}).encode()
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.release.set()
            srv.shutdown(); srv.server_close(); thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture thread leak')
            self.assertEqual(srv.errors, [], 'strict HTTPS fixture error')

    def invoke(self, srv, origin, mode, signo=None, expected_status=0):
        binary = os.environ.get('GIN_LOGIN_CONTROL_BINARY', str(ROOT / '.build/auth_login_control_driver'))
        proc = subprocess.Popen([binary, mode, origin, str(self.certdir / 'ca.pem')],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                start_new_session=True)
        start = 0.0
        try:
            if signo is not None:
                self.assertTrue(srv.entered.wait(4), 'real POST was not entered')
                self.assertFalse(srv.release.is_set())
                start = time.monotonic()
                proc.send_signal(signo)
            stdout, stderr = proc.communicate(timeout=3 if signo else 6)
            if signo is not None:
                self.assertLess(time.monotonic() - start, 3)
                self.assertFalse(srv.release.is_set(), 'response released before client exit')
            self.assertEqual(proc.returncode, expected_status, 'native driver failed')
            self.assertEqual(stderr, b'')
            self.assertNotIn(b'synthetic-', stdout + stderr)
            return stdout.decode().strip()
        finally:
            if proc.poll() is None:
                os.killpg(proc.pid, signal.SIGKILL)
            proc.communicate(timeout=3)  # Reap before fixture response gate release.

    def test_unknown_mode_rejected_without_requests(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.invoke(srv, origin, 'unknown-selector', expected_status=2), '')
            self.assertEqual(srv.records, [], 'unknown selector sent POST')

    def test_prestopped_zero_post(self):
        for mode in ('preint', 'preterm'):
            with self.subTest(mode=mode), self.fixture() as (srv, origin):
                result = self.invoke(srv, origin, mode)
                self.assertEqual(srv.records, [], 'prestopped login sent POST')
                self.assertEqual(result, 'transport 0 1 1')

    def test_healthy_and_null_legacy(self):
        for mode in ('healthy', 'null', 'legacy'):
            with self.subTest(mode=mode), self.fixture() as (srv, origin):
                self.assertEqual(self.invoke(srv, origin, mode), 'ok 1 0 0')
                self.assertEqual(srv.records, [('POST', '/auth/login')])

    def test_held_post_signal(self):
        for signo in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signo=signo), self.fixture(held=True) as (srv, origin):
                self.assertEqual(self.invoke(srv, origin, 'held', signo), 'transport 0 1 1')
                self.assertEqual(srv.records, [('POST', '/auth/login')])

    def test_unchanged_failure_classification(self):
        for status, body, expected in ((401, b'{}', 'unauthorized'),
                                       (200, b'{', 'malformed-response')):
            for mode in ('healthy', 'null', 'legacy'):
                with self.subTest(status=status, mode=mode), self.fixture(status=status, body=body) as (srv, origin):
                    self.assertEqual(self.invoke(srv, origin, mode), expected + ' 0 1 0')
                    self.assertEqual(srv.records, [('POST', '/auth/login')])


if __name__ == '__main__':
    unittest.main(verbosity=2)
