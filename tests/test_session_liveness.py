"""Actual production-owner heartbeat and continued plaintext reception."""
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import select
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
import test_session as startup
from ws_fixture_frames import server_frame, take_client_frame

TOPICS = ['grappa:user:Alice', 'grappa:user:Alice/network:NetA',
          'grappa:user:Alice/network:NetA/channel:#room']
PREFIX_BYTES = b'existing prefix\n'
LINE = '[1970-01-01T00:00:00.000Z] action sender="Nick" body="café\\x0Awaves"\n'.encode()


class Server(startup.Server):
    ready_observed: threading.Event
    heartbeat_seen: threading.Event
    release_app: threading.Event
    app_emitted: threading.Event
    heartbeat_refs: list[str]
    heartbeats_after_ready: int
    case: str
    drop_observed: threading.Event
    dropped_refs: list[str]
    persisted_id: int = 0


class Handler(startup.Handler):
    def upgrade(self):
        srv = cast(Server, self.server)
        assert self.headers.get('Authorization') is None
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1(
            (key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name, value in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                            ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.08)
        buffered = bytearray()
        joined = 0
        try:
            while not srv.stopped.is_set():
                if joined == 3 and srv.release_app.is_set() and not srv.app_emitted.is_set():
                    row = {'network': 'NetA', 'channel': '#room', 'kind': 'action',
                           'sender': 'Nick', 'server_time': 0, 'body': '\x01ACTION café\nwaves'}
                    if srv.persisted_id:
                        row['id'] = srv.persisted_id
                    frame = [None, None, TOPICS[2], 'event', {'kind': 'message', 'message': row}]
                    if srv.case == 'severance':
                        frame = [None, None, TOPICS[0], 'event',
                                 {'kind': 'web_session_severed', 'code': 'rate_limit_flood'}]
                        srv.client_close_expected.set()
                    self.connection.sendall(server_frame(json.dumps(frame, ensure_ascii=False).encode()))
                    srv.app_emitted.set()
                try:
                    data = self.connection.recv(16384)
                except socket.timeout:
                    continue
                if not data:
                    srv.client_closed.set()
                    assert srv.client_close_expected.is_set() or srv.stopped.is_set(), 'unexpected client EOF'
                    return
                buffered.extend(data)
                while True:
                    item = take_client_frame(buffered)
                    if item is None:
                        break
                    opcode, final, payload = item
                    if opcode == 8:
                        assert srv.client_close_expected.is_set() or srv.stopped.is_set(), 'unexpected client CLOSE'
                        srv.client_closed.set()
                        return
                    if opcode == 10:
                        continue
                    assert opcode == 1 and final, 'unexpected non-TEXT frame'
                    frame = json.loads(payload)
                    assert isinstance(frame, list) and len(frame) == 5
                    jr, ref, topic, event, body = frame
                    if event == 'phx_join':
                        assert joined < len(TOPICS) and topic == TOPICS[joined]
                        assert isinstance(ref, str) and ref and jr == ref and body == {}
                        reply = [jr, ref, topic, 'phx_reply', {'status': 'ok',
                                 'response': {'protocol_version': 36} if joined == 0 else {}}]
                        srv.joins.append(topic)
                        self.connection.sendall(server_frame(json.dumps(reply).encode()))
                        joined += 1
                        if srv.case == 'joining_close' and joined == 1:
                            srv.client_close_expected.set()
                            self.connection.sendall(server_frame(b'', opcode=8))
                            return
                    elif event == 'heartbeat':
                        assert jr is None and topic == 'phoenix' and body == {}
                        assert isinstance(ref, str) and ref and ref not in srv.heartbeat_refs
                        assert len(srv.heartbeat_refs) < 100, 'fixture heartbeat budget exhausted'
                        srv.heartbeat_refs.append(ref)
                        if srv.case == 'drop_ack' and srv.ready_observed.is_set():
                            srv.dropped_refs.append(ref)
                            srv.client_close_expected.set()
                            srv.drop_observed.set()
                            continue
                        reply = [None, ref, 'phoenix', 'phx_reply', {'status': 'ok', 'response': {}}]
                        self.connection.sendall(server_frame(json.dumps(reply).encode()))
                        if srv.ready_observed.is_set():
                            srv.heartbeats_after_ready += 1
                            if srv.heartbeats_after_ready >= 2:
                                srv.heartbeat_seen.set()
                    else:
                        raise AssertionError('unexpected client application operation')
        except (BrokenPipeError, ConnectionResetError):
            if not (srv.stopped.is_set() or srv.client_close_expected.is_set()):
                raise
        finally:
            self.close_connection = True


class LivenessFixture(unittest.TestCase):
    certdir: Path
    tls = startup.StartupTracer.tls

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, case):
        srv = Server(('127.0.0.1', 0), Handler)
        for name in ['final_requested', 'release_final', 'stopped', 'client_closed',
                     'client_close_expected', 'ready_observed', 'heartbeat_seen', 'release_app', 'app_emitted', 'drop_observed']:
            setattr(srv, name, threading.Event())
        srv.paths, srv.joins, srv.errors, srv.heartbeat_refs = [], [], [], []
        srv.heartbeats_after_ready = 0
        srv.dropped_refs = []
        srv.case = case
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.release_app.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'strict TLS fixture handler failed')

    def command(self, *args):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        return [*startup.PREFIX, binary, *map(str, args)]

    def run_case(self, case, cleanup_error=False):
        instrumented = bool(startup.PREFIX)
        with self.fixture(case) as (srv, origin):
            # Separate methods must not truncate an output still bound to a
            # checkpoint left by the preceding cleanup-fault case.
            root = self.certdir / ('heartbeat-' + self._testMethodName + '-' + case)
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=60 if instrumented else 5)
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output = root / '%4Eet%41' / 'room' / 'out'
            self.assertTrue(output.is_file())
            output.write_bytes(PREFIX_BYTES)
            inode = output.stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                                             'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            before_credential = credential.read_bytes()
            config = self.certdir / 'heartbeat-settings'
            interval, timeout = (1000, 3000) if instrumented else (150, 500)
            policy = f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={timeout}\n'
            policy += 'retry_base_ms=20\nretry_cap_ms=100\n'
            policy += 'max_retries=2\n' if case == 'severance' else 'max_retries=0\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            config.write_text(policy)
            config.chmod(0o600)
            read_fd, write_fd = os.pipe()
            os.set_blocking(read_fd, False)
            os.set_blocking(write_fd, False)
            proc = None
            try:
                proc = subprocess.Popen(self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', write_fd),
                    pass_fds=(write_fd,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(write_fd)
                write_fd = -1
                self.assertTrue(select.select([read_fd], [], [], 90 if instrumented else 5)[0],
                                'no actual client READY')
                self.assertEqual(os.read(read_fd, 128), b'READY\n')
                self.assertIsNone(proc.poll(), 'READY did not identify a live owner')
                self.assertEqual(srv.joins, TOPICS)
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                                            '/socket/websocket?vsn=2.0.0&client_proto=36',
                                            '/api/config', '/me',
                                            '/networks/NetA/channels/%23Room/messages?limit=200&after=0'])
                srv.ready_observed.set()
                expected = PREFIX_BYTES
                expected_code = 1
                expected_error = b'gin: connection failure\n'
                if case == 'drop_ack':
                    self.assertTrue(srv.drop_observed.wait(15 if instrumented else 3),
                                    'no actual post-READY heartbeat to withhold')
                else:
                    self.assertTrue(srv.heartbeat_seen.wait(15 if instrumented else 3),
                                    'production owner did not send configured Phoenix heartbeat')
                    self.assertGreaterEqual(srv.heartbeats_after_ready, 2)
                    self.assertIsNone(proc.poll(), 'heartbeat ACK handling stopped the owner')
                    srv.release_app.set()
                    if case == 'positive':
                        expected += LINE
                        deadline = time.monotonic() + (30 if instrumented else 3)
                        while output.read_bytes() != expected:
                            self.assertIsNone(proc.poll(), 'owner stopped before application receipt')
                            self.assertLess(time.monotonic(), deadline, 'application receipt deadline')
                            srv.stopped.wait(.02)
                        self.assertEqual(output.stat().st_ino, inode)
                        self.assertIsNone(proc.poll(), 'owner stopped after application receipt')
                        srv.client_close_expected.set()
                        proc.send_signal(signal.SIGTERM)
                        expected_code, expected_error = (1, b'gin: session cleanup failure\n') if cleanup_error else (0, b'')
                stdout, stderr = proc.communicate(timeout=60 if instrumented else 12)
                self.assertEqual(proc.returncode, expected_code, repr(stderr))
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, expected_error)
                self.assertEqual(srv.joins, TOPICS, 'unexpected automatic resubscription')
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                                            '/socket/websocket?vsn=2.0.0&client_proto=36',
                                            '/api/config', '/me',
                                            '/networks/NetA/channels/%23Room/messages?limit=200&after=0'],
                                 'unexpected HTTP retry or outgoing operation')
                if case == 'drop_ack':
                    self.assertEqual(len(srv.dropped_refs), 1, 'heartbeat replayed before expiry')
                else:
                    self.assertTrue(srv.app_emitted.is_set(), 'selected server frame not emitted')
                self.assertNotIn(startup.TOKEN.encode(), stdout + stderr)
                self.assertEqual(credential.read_bytes(), before_credential)
                self.assertEqual(output.read_bytes(), expected)
                check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                       capture_output=True, timeout=60 if instrumented else 5)
                self.assertEqual(check.returncode, 0, 'root lock remained held')
                self.assertTrue((root / '.gin.lock').is_file())
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual(output.read_bytes(), expected)
            finally:
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        try:
                            proc.terminate()
                        except ProcessLookupError:
                            pass
                    try:
                        proc.communicate(timeout=60 if instrumented else 8)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.communicate(timeout=10)
                os.close(read_fd)
                if write_fd >= 0:
                    os.close(write_fd)


class SessionLivenessTest(LivenessFixture):
    def test_real_owner_heartbeat_and_continued_reception(self):
        self.run_case('positive')

    def test_lost_heartbeat_ack_stops_without_replay(self):
        self.run_case('drop_ack')

    def test_root_session_severance_is_terminal_with_retry_budget(self):
        self.run_case('severance')


if __name__ == '__main__':
    unittest.main()
