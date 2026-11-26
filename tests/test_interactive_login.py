#!/usr/bin/env python3
"""Actual native HTTPS/PTTY oracles; strict bounded owned server."""
import contextlib
import http.server
import json
import os
import pty
import select
import signal
import socket
import ssl
import subprocess
import sys
import termios
import time
import threading
import unittest
import tempfile
from collections.abc import Iterator
from typing import ClassVar, cast
from unittest.mock import patch
from pathlib import Path
import test_protocol


from pty_test_support import PtyProcess, drain_pty


class PtyDrainTests(unittest.TestCase):
    def run_child(self, body: str) -> None:
        code = ('import os,time\nfrom test_interactive_login import drain_pty\n' + body)
        child = subprocess.Popen([sys.executable, '-B', '-c', code],
            cwd=Path(__file__).resolve().parent,
            env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            out, err = child.communicate(timeout=2)
        except subprocess.TimeoutExpired:
            os.killpg(child.pid, signal.SIGKILL)
            child.communicate()
            self.fail('independent PTY drain process deadline')
        self.assertEqual(child.returncode, 0, err.decode(errors='replace'))
        self.assertEqual(out, b'')

    def test_empty_eof_returns(self):
        self.run_child('r,w=os.pipe(); os.close(w)\n'
            'assert drain_pty(r,b"prefix",time.monotonic()+1)==b"prefix"\nos.close(r)')

    def test_data_then_eof_retains_bytes(self):
        self.run_child('r,w=os.pipe(); os.write(w,b"tail"); os.close(w)\n'
            'assert drain_pty(r,b"prefix",time.monotonic()+1)==b"prefixtail"\nos.close(r)')

    def test_expired_deadline_rejected(self):
        self.run_child('r,w=os.pipe(); os.write(w,b"x"); os.close(w)\n'
            'try: drain_pty(r,b"",time.monotonic()-1)\n'
            'except TimeoutError: pass\nelse: raise AssertionError("deadline not enforced")\nos.close(r)')

    def test_capture_overflow_rejected(self):
        self.run_child('r,w=os.pipe(); os.write(w,b"x"); os.close(w)\n'
            'try: drain_pty(r,b"",time.monotonic()+1,cap=0)\n'
            'except AssertionError: pass\nelse: raise AssertionError("capture cap not enforced")\nos.close(r)')


class StrictServer(http.server.HTTPServer):
    def __init__(self, certdir: Path, body: dict[str, object], auth_status: int):
        super().__init__(('127.0.0.1', 0), StrictHandler)
        self.socket.settimeout(.1)
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.minimum_version = ssl.TLSVersion.TLSv1_2
        self.context.load_cert_chain(certdir / 'good.pem', certdir / 'good.key')
        self.body = json.dumps(body).encode()
        self.auth_status = auth_status
        self.records: list[tuple[str, str, dict[str, str], bytes]] = []
        self.failures: list[BaseException] = []
        self.workers: list[threading.Thread] = []
        self.connections: set[socket.socket] = set()
        self.lock = threading.Lock()
        self.stopped = threading.Event()
        self.entered = threading.Event()
        self.release = threading.Event()
        self.held = False
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def serve(self) -> None:
        try:
            while not self.stopped.is_set():
                try:
                    conn, addr = self.socket.accept()
                except TimeoutError:
                    continue
                conn.settimeout(1)
                with self.lock:
                    self.connections.add(conn)
                worker = threading.Thread(target=self.work, args=(conn, addr), daemon=True)
                self.workers.append(worker)
                worker.start()
        except BaseException as exc:
            self.failures.append(exc)

    def work(self, raw: socket.socket, addr: tuple[str, int]) -> None:
        conn: socket.socket = raw
        try:
            tls = self.context.wrap_socket(raw, server_side=True, do_handshake_on_connect=False)
            conn = tls
            with self.lock:
                self.connections.discard(raw)
                self.connections.add(tls)
            tls.settimeout(1)
            tls.do_handshake()
            StrictHandler(tls, addr, self)
        except BaseException as exc:
            self.failures.append(exc)
        finally:
            conn.close()
            with self.lock:
                self.connections.discard(raw)
                self.connections.discard(conn)

    def close_bounded(self) -> None:
        self.stopped.set()
        self.release.set()
        self.thread.join(timeout=.5)
        self.server_close()
        deadline = time.monotonic() + 2
        for worker in self.workers:
            worker.join(timeout=max(0, deadline - time.monotonic()))
        with self.lock:
            for conn in self.connections:
                conn.close()
        assert not self.thread.is_alive(), 'accept worker deadline'
        assert not any(w.is_alive() for w in self.workers), 'request worker deadline'
        assert not self.connections, 'active connection cleanup'
        assert not self.failures, 'server worker failures: ' + ','.join(type(e).__name__ for e in self.failures)


class StrictHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass

    def handle_one_request(self) -> None:
        # The stdlib implementation swallows read timeouts; this one must not.
        self.raw_requestline = self.rfile.readline(65537)
        if not self.raw_requestline:
            self.close_connection = True
            return
        if len(self.raw_requestline) > 65536 or not self.parse_request():
            raise ValueError('invalid request')
        if self.command == 'GET':
            self.do_GET()
        elif self.command == 'POST':
            self.do_POST()
        else:
            raise ValueError('unexpected method')
        self.wfile.flush()

    def handle_request(self) -> None:
        srv = cast(StrictServer, self.server)
        body = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        srv.records.append((self.command, self.path, dict(self.headers), body))
        self.close_connection = True
        if self.path == '/auth/login' and srv.held:
            srv.entered.set()
            if not srv.release.wait(timeout=8):
                raise TimeoutError('held POST release deadline')
            return  # Exact expected abort path: manually released, no write.
        if self.path == '/api/config':
            status, response = 200, srv.body
        elif self.path == '/auth/login':
            status = srv.auth_status
            response = json.dumps({'token': test_protocol.TOKEN, 'subject': test_protocol.IDENTITY}).encode()
        else:
            raise ValueError('unexpected path')
        self.send_response(status)
        self.send_header('Content-Length', str(len(response)))
        self.send_header('Set-Cookie', 'ambient=forbidden')
        self.end_headers()
        self.wfile.write(response)

    do_GET = do_POST = handle_request


class InteractiveTests(unittest.TestCase):
    certdir: ClassVar[Path]

    @classmethod
    def setUpClass(cls):
        fallback = Path(__file__).resolve().parents[1] / '.build' / 'cert-tmp'
        fallback.mkdir(parents=True, exist_ok=True)
        with patch.object(tempfile, 'tempdir', os.environ.get('TMPDIR', str(fallback))):
            test_protocol.ProtocolTests.setUpClass()
        cls.certdir = test_protocol.ProtocolTests.certdir

    @classmethod
    def tearDownClass(cls):
        test_protocol.ProtocolTests.tearDownClass()

    @contextlib.contextmanager
    def fixture(self, body: dict[str, object] = test_protocol.CONFIG,
                auth_status: int = 200) -> Iterator[tuple[StrictServer, str]]:
        srv = StrictServer(self.certdir, body, auth_status)
        try:
            srv.thread.start()
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.close_bounded()

    def run_case(self, mode, body=test_protocol.CONFIG, status=200,
                 defer_pty_read=False):
        assert not defer_pty_read or mode == 'blocked'
        with self.fixture(body=body, auth_status=status) as (srv, origin):
            srv.held = mode == 'heldpost'
            entered, release = srv.entered, srv.release
            master: int | None = None
            slave: int | None = None
            child: PtyProcess | None = None
            try:
                master, slave = pty.openpty()
                termios.tcgetattr(slave)  # Check setup while the PTY is still live.
                # Retain the controlling session after native exit until its
                # report and secrecy assertions have consumed all PTY output.
                child = PtyProcess([os.environ['INTERACTIVE_DRIVER'], mode,
                    origin, str(self.certdir / 'ca.pem')], slave)
                transcript = b''
                sent = False
                interrupted_at = None
                end = time.monotonic() + 6
                while time.monotonic() < end:
                    if defer_pty_read:
                        # A slow observer is legitimate: no password is needed
                        # for this rejected caller. Read only after native exit.
                        time.sleep(.002)
                    elif select.select([master], [], [], .02)[0]:
                        transcript += os.read(master, 16384)
                    if b'Password: ' in transcript and not sent:
                        self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
                        os.write(master, b'HiddenSentinel\n')
                        sent = True
                    if mode == 'heldpost' and entered.is_set() and interrupted_at is None:
                        interrupted_at = time.monotonic()
                        child.send_signal(signal.SIGTERM)
                    if child.poll() is not None:
                        break
                self.assertIsNotNone(child.poll(), 'outer process-group deadline')
                child.wait(timeout=.2)
                if mode == 'heldpost':
                    self.assertIsNotNone(interrupted_at)
                    assert interrupted_at is not None
                    self.assertLess(time.monotonic() - interrupted_at, 1.5,
                                    'held POST must stop via control, not the 3s HTTP timeout')
                    self.assertFalse(release.is_set())
                transcript = drain_pty(master, transcript, end)
                self.assertEqual(child.returncode, 0, 'native driver failed (transcript omitted)')
                # The native driver checks observable termios before session exit.
                # FreeBSD revokes the controlling PTY when its session leader exits.
                for secret in (b'HiddenSentinel', test_protocol.TOKEN.encode(), b'SyntheticUser'):
                    self.assertFalse(secret in transcript, 'secret leaked (transcript omitted)')
                records = [r[:2] for r in srv.records]
                early = mode in ('preaborted', 'default', 'ignored', 'blocked')
                if early:
                    self.assertEqual(records, [])
                else:
                    self.assertEqual(records[0], ('GET', '/api/config'))
                expected = mode in ('healthy', 'failure', 'postauthstop', 'heldpost')
                self.assertEqual(records.count(('POST', '/auth/login')), int(expected),
                                 'post-read stop must prevent POST')
                self.assertEqual(records, ([] if early else
                    [('GET', '/api/config')] + ([('POST', '/auth/login')] if expected else [])))
                self.assertEqual(sent, mode != 'bad' and not early)
                if early:
                    self.assertFalse(b'Password: ' in transcript)
                self.assertTrue(
                    ('ok=%d attempted=%d' % (int(mode == 'healthy'), int(expected))).encode() in transcript,
                    'result report absent: captured_bytes=%d deferred=%d child_rc=%d '
                    '(transcript omitted)' % (len(transcript), int(defer_pty_read), child.returncode))
                if expected:
                    self.assertTrue(b'wiped=1' in transcript)
            finally:
                try:
                    if child is not None:
                        try:
                            try:
                                if child.poll() is None:
                                    child.kill()
                            finally:
                                child.wait(timeout=1)
                        finally:
                            child.close()
                finally:
                    try:
                        release.set()
                    finally:
                        try:
                            if master is not None:
                                os.close(master)
                        finally:
                            if slave is not None:
                                os.close(slave)

    def test_blocked_caller_deferred_pty_reader(self):
        self.run_case('blocked', defer_pty_read=True)

    def test_pty_report_session_boundary(self):
        # Controlled transport characterization, not a login replacement.
        # Both schedules use the same checked write and wait for native exit.
        # Only the retained session owner's lifetime differs before PTY drain.
        marker = b'PTYReportBoundaryCheckedWrite'
        code = ('import os,sys; data=b"PTYReportBoundaryCheckedWrite"; '
                'count=os.write(1,data); assert count==len(data); '
                'assert os.write(int(sys.argv[1]),str(count).encode()+b"\\n")>0')
        for close_before_read in (False, True):
            with self.subTest(close_before_read=close_before_read):
                descriptors: list[int] = []
                child: PtyProcess | None = None
                deadline = time.monotonic() + 6
                try:
                    master, slave = pty.openpty()
                    descriptors.extend((master, slave))
                    receipt_read, receipt_write = os.pipe()
                    descriptors.extend((receipt_read, receipt_write))
                    os.set_blocking(receipt_read, False)
                    child = PtyProcess(
                        [sys.executable, '-B', '-c', code, str(receipt_write)],
                        slave, pass_fds=(receipt_write,))
                    os.close(receipt_write)
                    descriptors.remove(receipt_write)
                    self.assertEqual(child.wait(timeout=max(0, deadline-time.monotonic())), 0)
                    self.assertTrue(select.select(
                        [receipt_read], [], [], max(0, deadline-time.monotonic()))[0])
                    self.assertEqual(os.read(receipt_read, 64), str(len(marker)).encode()+b'\n')
                    if close_before_read:
                        child.close()
                    captured = drain_pty(master, b'', deadline)
                    self.assertIn(captured, (b'', marker), 'unexpected probe bytes (omitted)')
                    if not close_before_read:
                        self.assertEqual(captured, marker, 'live owner must preserve checked output')
                    print('PTY_REPORT_BOUNDARY '+json.dumps({
                        'owner_closed_before_read': close_before_read,
                        'native_exit': child.returncode,
                        'checked_write_bytes': len(marker),
                        'captured_bytes': len(captured),
                        'report_present': captured == marker,
                    }, sort_keys=True), flush=True)
                finally:
                    try:
                        if child is not None:
                            child.close()
                    finally:
                        for fd in descriptors:
                            os.close(fd)

    def test_after_response_fault_is_failure(self):
        original = StrictHandler.do_POST
        replied: list[bool] = []
        def fault(handler: StrictHandler) -> None:
            original(handler)
            replied.append(True)
            raise OSError('injected-after-normal-reply')
        with patch.object(StrictHandler, 'do_POST', fault):
            with self.assertRaisesRegex(AssertionError, 'server worker failures'):
                self.run_case('healthy')
        self.assertEqual(replied, [True])

    def test_spawn_failure_closes_pty(self):
        opened: list[int] = []
        original = pty.openpty
        def tracked() -> tuple[int, int]:
            pair = original()
            opened.extend(pair)
            return pair
        with patch.object(pty, 'openpty', tracked), patch.object(subprocess, 'Popen', side_effect=OSError('spawn guard')):
            with self.assertRaisesRegex(OSError, 'spawn guard'):
                self.run_case('healthy')
        self.assertEqual(len(opened), 2)
        for fd in opened:
            with self.assertRaises(OSError):
                os.fstat(fd)

    def test_half_open_tls_deadline(self):
        code = ('import sys,socket,time; sys.path.insert(0,sys.argv[1]); '
                'from pathlib import Path; from test_interactive_login import StrictServer; '
                's=StrictServer(Path(sys.argv[2]),{},200); s.thread.start(); '
                'c=socket.create_connection(s.server_address,timeout=1); time.sleep(1.3); '
                '\ntry: s.close_bounded()\n'
                'except AssertionError as e:\n'
                ' assert "server worker failures" in str(e)\n'
                ' assert len(s.failures)==1 and isinstance(s.failures[0],TimeoutError)\n'
                'else: raise AssertionError("missing TLS timeout ledger")\n'
                'finally: c.close()\n')
        child = subprocess.Popen([sys.executable, '-c', code, str(Path(__file__).parent), str(self.certdir)],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        try:
            out, err = child.communicate(timeout=4)
            self.assertEqual(child.returncode, 0, 'half-open TLS guard failed (output omitted)')
            self.assertFalse(bool(out or err), 'guard emitted diagnostics')
        finally:
            if child.poll() is None:
                os.killpg(child.pid, signal.SIGKILL)
                child.wait(timeout=1)

    def test_held_post_interrupted(self): self.run_case('heldpost')
    def test_postauth_success_stop(self): self.run_case('postauthstop')
    def test_initial_aborted(self): self.run_case('preaborted')
    def test_default_caller(self): self.run_case('default')
    def test_ignored_caller(self): self.run_case('ignored')
    def test_blocked_caller(self): self.run_case('blocked')
    def test_postread_int(self): self.run_case('int')
    def test_postread_term(self): self.run_case('term')
    def test_healthy(self): self.run_case('healthy')
    def test_bad(self): self.run_case('bad', {**test_protocol.CONFIG, 'server': 'other'})
    def test_floor(self): self.run_case('bad', {**test_protocol.CONFIG, 'min_protocol_version': 6})
    def test_auth_failure(self): self.run_case('failure', status=401)

if __name__ == '__main__':
    unittest.main(verbosity=2)
