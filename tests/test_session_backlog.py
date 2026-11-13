"""Production startup must observe receive drain, not a service work quantum."""
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import threading
import unittest
from typing import Iterator, cast

import test_http
import test_session as base
from ws_fixture_frames import server_frame, take_client_frame


class BacklogServer(base.Server):
    mode: str
    burst: int
    partial_sent: threading.Event
    release_partial: threading.Event


class BacklogHandler(base.Handler):
    def upgrade(self) -> None:
        srv = cast(BacklogServer, self.server)
        if self.headers.get('Authorization') is not None:
            raise AssertionError('upgrade authorization header')
        if self.headers.get('Sec-WebSocket-Protocol') != 'phoenix,base64url.bearer.phx.YWF+':
            raise AssertionError('invalid subprotocol')
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name, value in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                            ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(30 if base.PREFIX else 8)
        buffered = bytearray()
        root_topic = ''
        try:
            for index in range(4):
                item = take_client_frame(buffered)
                while item is None:
                    data = self.connection.recv(16384)
                    if not data:
                        raise AssertionError('closed before required join')
                    buffered.extend(data)
                    item = take_client_frame(buffered)
                opcode, final, payload = item
                if opcode != 1 or not final:
                    raise AssertionError('non-text join')
                jr, ref, topic, event, body = json.loads(payload)
                if event != 'phx_join' or body != {} or jr != ref:
                    raise AssertionError('invalid join')
                srv.joins.append(topic)
                reply = server_frame(json.dumps([jr, ref, topic, 'phx_reply',
                    {'status': 'ok', 'response': {'protocol_version': 36} if index == 0 else {}}]).encode())
                if index == 0:
                    root_topic = topic
                    snapshot = {'kind': 'query_windows_list', 'windows': {'3000000001': [
                        {'network_id': 3000000001, 'target_nick': 'Peer', 'opened_at': 'now'}]}}
                    self.connection.sendall(reply + (server_frame(json.dumps([None, None, root_topic, 'event', snapshot]).encode())
                        if srv.mode != 'flood' else b''))
                elif index == 3:
                    filler = server_frame(json.dumps([None, None, root_topic, 'event', {'kind': 'unrelated'}]).encode())
                    replacement = json.dumps([None, None, root_topic, 'event',
                        {'kind': 'query_windows_list', 'windows': {}}]).encode()
                    if srv.mode == 'flood':
                        # Unknown query inventory cannot become READY, even if a
                        # transport gap occurs between finite server writes.
                        self.connection.sendall(reply + filler * 4096)
                        while not srv.stopped.is_set():
                            self.connection.sendall(filler * 4096)
                        return
                    if srv.mode == 'partial':
                        # A complete JSON value without FIN is still not a message.
                        self.connection.sendall(reply + server_frame(replacement, final=False) + server_frame(b'p', opcode=9))
                        srv.partial_sent.set()
                        if not srv.release_partial.wait(30 if base.PREFIX else 8):
                            raise AssertionError('partial barrier expired')
                        if srv.stopped.is_set():
                            return
                        self.connection.sendall(server_frame(b'', opcode=0))
                    else:
                        tail = server_frame(replacement) if srv.mode == 'empty' else b''
                        self.connection.sendall(reply + filler * srv.burst + tail)
                else:
                    self.connection.sendall(reply)
            while not srv.stopped.is_set():
                data = self.connection.recv(16384)
                if not data:
                    srv.client_closed.set()
                    return
                buffered.extend(data)
                item = take_client_frame(buffered)
                while item is not None:
                    if item[0] not in (8, 10):
                        raise AssertionError('unexpected application operation')
                    item = take_client_frame(buffered)
        except OSError:
            # Only the deliberate post-fourth-join client shutdown is tolerated.
            if not srv.stopped.is_set() and not (srv.client_close_expected.is_set() and len(srv.joins) == 4):
                raise
        finally:
            self.close_connection = True


class BacklogTests(unittest.TestCase):
    certdir: Path
    tls = test_http.HTTPTests.tls

    @classmethod
    def setUpClass(cls) -> None:
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls) -> None:
        test_http.HTTPTests.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, mode: str, burst: int) -> Iterator[tuple[BacklogServer, str]]:
        srv = BacklogServer(('127.0.0.1', 0), BacklogHandler)
        srv.mode, srv.burst = mode, burst
        for name in ['final_requested', 'release_final', 'stopped', 'client_closed',
                     'client_close_expected', 'partial_sent', 'release_partial']:
            setattr(srv, name, threading.Event())
        srv.paths, srv.joins, srv.errors = [], [], []
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.release_partial.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'listener leaked')
            self.assertFalse(srv.errors, 'backlog fixture handler failed')

    def run_case(self, mode: str, burst: int) -> None:
        with self.fixture(mode, burst) as (srv, origin):
            root = self.certdir / (mode + str(burst))
            root.mkdir(mode=0o700)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': base.TOKEN,
                'subject_id': base.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            rd, wr = os.pipe()
            os.set_blocking(rd, False)
            os.set_blocking(wr, False)
            binary = os.environ.get('GIN_SESSION_BINARY', str(base.REPO / '.build/gin'))
            settings = root / 'settings'
            # Real operational timeout settings, not a production test switch.
            # Instrumentation needs TLS/preflight time before the flood phase;
            # the exact request/join assertions below prove we reached that phase.
            if base.PREFIX:
                startup_ms = 8000 if mode == 'flood' else 180000
                contents = ('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
                            f'startup_ms={startup_ms}\n')
            else:
                contents = 'startup_ms=1500\n' if mode == 'flood' else ''
            settings.write_text(contents)
            settings.chmod(0o600)
            proc = subprocess.Popen([*base.PREFIX, binary, 'connect', '-f', str(settings), '-d', str(root), '-n', 'NetA',
                '-c', 'Peer', '-e', origin, '-m', 'plain', '-a', str(self.certdir / 'ca.pem'),
                '-R', str(wr)], pass_fds=(wr,), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(wr)
            try:
                srv.client_close_expected.set()
                if mode == 'partial':
                    self.assertTrue(srv.partial_sent.wait(30 if base.PREFIX else 5), 'no partial snapshot')
                    self.assertFalse(select.select([rd], [], [], .2)[0], 'READY while TEXT lacks FIN')
                    self.assertIsNone(proc.poll(), 'owner exited during partial snapshot')
                    srv.release_partial.set()
                if mode == 'valid':
                    self.assertTrue(select.select([rd], [], [], 30 if base.PREFIX else 5)[0], 'no READY after valid backlog')
                    self.assertEqual(os.read(rd, 128), b'READY\n', 'invalid readiness')
                    proc.send_signal(signal.SIGTERM)
                out, err = proc.communicate(timeout=60 if base.PREFIX else 10)
                status = os.read(rd, 128)
                self.assertEqual(status, b'', 'premature READY before queued replacement')
                self.assertEqual(proc.returncode, 0 if mode == 'valid' else 1, 'wrong session exit')
                self.assertEqual(out, b'', 'unexpected stdout')
                if mode != 'valid':
                    expected = b'startup timeout' if mode == 'flood' else b'selected route unavailable'
                    self.assertIn(expected, err, 'missing startup failure')
                for secret in [base.TOKEN.encode(), b'YWF+']:
                    self.assertNotIn(secret, out + err, 'credential disclosed')
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                    '/socket/websocket?vsn=2.0.0&client_proto=36'], 'incomplete startup HTTP flow')
                self.assertEqual(srv.joins, ['grappa:user:Alice', 'grappa:user:Alice/network:NetA',
                    'grappa:user:Alice/network:NetA/channel:peer',
                    'grappa:user:Alice/network:NetA/channel:owna'], 'missing required subscriptions')
            finally:
                srv.stopped.set()
                srv.release_partial.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=30 if base.PREFIX else 5)
                os.close(rd)

    def test_empty_replacement_after_128_frames(self) -> None:
        self.run_case('empty', 128)

    def test_empty_replacement_after_large_backlog(self) -> None:
        self.run_case('empty', 4096)

    def test_valid_backlog_still_ready(self) -> None:
        self.run_case('valid', 128)

    def test_text_backlog_obeys_startup_deadline(self) -> None:
        self.run_case('flood', 0)

    def test_partial_snapshot_waits_for_fin(self) -> None:
        self.run_case('partial', 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
