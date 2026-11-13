"""First 18.2 semantic tracer: real CLI, verified WSS, existing output prefix."""
from pathlib import Path
from typing import Any, cast
import base64
import hashlib
import http.server
import json
import os
import select
import signal
import subprocess
import sys
import threading
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests'))
import test_session as startup
from ws_fixture_frames import server_frame, take_client_frame

def budget(native):
    """Keep native deadlines unchanged; instrumentation needs TLS/cleanup time."""
    return 90 if startup.PREFIX else native


PREFIX = b'existing prefix\n'
LINE = '[1970-01-01T00:00:00.000Z] action sender="Nick" body="café\\x0Awaves"\n'.encode()


class Handler(startup.Handler):
    def do_GET(self):
        srv = cast(Any, self.server)
        srv.paths.append(self.path)
        if self.path == '/api/config':
            assert self.headers.get('Authorization') is None
            body = {'server': 'grappa', 'protocol_version': srv.protocol, 'min_protocol_version': 1}
        elif self.path == '/me':
            assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
            body = {'kind': 'user', 'id': startup.UUID, 'name': 'Alice'}
        elif self.path == '/boot':
            assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
            body = {'networks': [{'id': 3000000001, 'slug': 'NetA', 'nick': 'OwnA'}],
                    'channels': {'NetA': [{'name': '#Room', 'joined': True, 'source': 'joined'}]}, 'heads': {}}
        elif self.path.startswith('/socket/websocket?'):
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
        srv = cast(Any, self.server)
        assert self.headers.get('Authorization') is None
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name, value in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                            ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(budget(8))
        buffered = bytearray()
        try:
            topic = ""
            for index in range(4 if srv.query else 3):
                while True:
                    frame = take_client_frame(buffered)
                    if frame is not None:
                        break
                    data = self.connection.recv(16384)
                    assert data, 'closed before subscription'
                    buffered.extend(data)
                opcode, final, payload = frame
                assert opcode == 1 and final
                jr, ref, topic, event, body = json.loads(payload)
                assert event == 'phx_join' and jr == ref and body == {}
                srv.joins.append(topic)
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': srv.protocol} if index == 0 else {}}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
                if index == 0 and srv.query:
                    snapshot = {'kind': 'query_windows_list', 'windows': {'3000000001': [
                        {'network_id': 3000000001, 'target_nick': 'Peer', 'opened_at': '2026-11-13T00:00:00Z'},
                        {'network_id': 3000000001, 'target_nick': 'Other', 'opened_at': '2026-11-13T00:00:00Z'}]}}
                    self.connection.sendall(server_frame(json.dumps([None, None, topic, 'event', snapshot]).encode()))
            srv.joined.set()
            assert srv.release_message.wait(budget(8)), 'parent did not observe client readiness'
            if srv.stopped.is_set():
                return
            root_topic = 'grappa:user:Alice'
            base = root_topic + '/network:NetA/channel:'
            row = {'network': 'NetA', 'channel': 'OwnA' if srv.query else '#room', 'kind': 'action',
                   'sender': 'Nick', 'server_time': 0, 'body': '\x01ACTION café\nwaves'}
            if srv.query:
                row['dm_with'] = 'Peer'
            events = [[None, None, base + ('owna' if srv.query else '#room'), 'event',
                       {'kind': 'message', 'message': row}]]
            if srv.query:
                echo = dict(row, channel='Peer', sender='OwnA', body='echo', kind='privmsg')
                events.append([None, None, base + 'peer', 'event', {'kind': 'message', 'message': echo}])
            if srv.case == 'metadata_burst':
                noise = [{'unrelated': i} for i in range(70)]
                noise += [{'kind': 'future_metadata', 'nested': {'kind': 'message'}},
                          {'kind': 'x' * 100 + 'é'}]
                events = [[None, None, root_topic, 'event', item] for item in noise] + events
            elif srv.case == 'escaped_stale':
                stale = json.loads(json.dumps(events[0]))
                stale[0] = 'stale-nonnull-ref'
                stale[4]['message']['body'] = 'must-not-appear'
                events.insert(0, stale)
            elif srv.case == 'reserved_long':
                events = [[None, None, root_topic, 'event', {'kind': 'e2e_' + 'x' * 100 + 'é'}]]
            elif srv.case == 'missing_dm':
                del row['dm_with']
                events = events[:1]
            elif srv.case == 'e2e':
                row['body'] = '\x01E2E secret-ciphertext'
                events = events[:1]
            elif srv.case == 'nested_e2e':
                row['kind'] = 'e2e_message'
                events = events[:1]
            elif srv.case == 'remove':
                events.insert(0, [None, None, root_topic, 'event', {'kind': 'query_windows_list', 'windows': {}}])
            elif srv.case == 'nick_change':
                events.insert(0, [None, None, root_topic, 'event', {'kind': 'own_nick_changed',
                    'network_id': 3000000001, 'nick': 'NewOwn'}])
            elif srv.case == 'offscope_channel':
                events.insert(0, [None, None, root_topic, 'event',
                    {'kind': 'window_pending', 'network': 'NetA', 'channel': '#Room'}])
            elif srv.case == 'known_other':
                # Retain the following selected echo as an actual client-receipt
                # barrier for the preceding ignored peer event.
                row['dm_with'] = 'Other'
            elif srv.case == 'wrong_network':
                row['network'] = 'Unknown'
                events = events[:1]
            elif srv.case == 'fractional_time':
                row['server_time'] = 0.5
                events = events[:1]
            wire = [json.dumps(event, ensure_ascii=False).encode() for event in events]
            if srv.case == 'escaped_stale':
                wire = [item.replace(b'"kind"', b'"k\\u0069nd"').replace(b'"message"', b'"mess\\u0061ge"') for item in wire]
            self.connection.sendall(b''.join(server_frame(item) for item in wire))
            srv.message_emitted.set()
            while self.connection.recv(4096):
                pass
        except (BrokenPipeError, ConnectionResetError):
            if not srv.stopped.is_set() and not srv.terminal:
                raise
        finally:
            self.close_connection = True


class ReceptionTracer(unittest.TestCase):
    tls = startup.StartupTracer.tls
    certdir: Path

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    def test_unknown_metadata_burst_preserves_selected_followup(self):
        self.receive(case='metadata_burst')

    def test_escaped_keys_and_stale_reference_preserve_valid_followup(self):
        self.receive(case='escaped_stale')

    def test_long_nonascii_reserved_kind_is_not_ignored(self):
        self.receive(case='reserved_long', failure=True)

    @staticmethod
    def session_flags(root):
        if not startup.PREFIX:
            return []
        settings = root / 'receive-settings'
        settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n')
        settings.chmod(0o600)
        return ['-f', str(settings)]

    def test_real_cli_appends_channel_action_without_replacing_prefix(self):
        self.receive()

    def test_inbound_ownnick_and_peer_echo_use_one_selected_output(self):
        self.receive(query=True)

    def test_query_without_explicit_dm_with_fails(self):
        self.receive(query=True, case='missing_dm', failure=True)

    def test_reserved_ctcp_body_fails_without_output(self):
        self.receive(case='e2e', failure=True)

    def test_reserved_nested_kind_fails_without_output(self):
        self.receive(case='nested_e2e', failure=True)

    def test_removal_at_queue_position_blocks_later_chat(self):
        self.receive(query=True, case='remove', failure=True)

    def test_canonical_own_nick_change_blocks_later_chat(self):
        self.receive(query=True, case='nick_change', failure=True)

    def test_known_offscope_channel_lifecycle_preserves_selected_dm(self):
        self.receive(query=True, case='offscope_channel')

    def test_known_other_peer_does_not_fan_out(self):
        self.receive(query=True, case='known_other', ignored=True)

    def test_fractional_time_is_not_integer_milliseconds(self):
        self.receive(case='fractional_time', failure=True)

    def test_server35_rejected_before_identity_and_enrollment(self):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        old_policy = os.environ.get('GIN_RECEPTION_OLD_POLICY') == '1'
        server = cast(Any, startup.Server(('127.0.0.1', 0), Handler))
        server.paths, server.joins, server.errors = [], [], []
        server.protocol = 35
        server.socket = self.tls().wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            root = self.certdir / 'gate35'
            root.mkdir(mode=0o700)
            origin = f'https://127.0.0.1:{server.server_port}'
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            before = credential.read_bytes()
            bearer = self.certdir / 'gate-bearer'
            bearer.write_text(startup.TOKEN)
            bearer.chmod(0o600)
            enroll = subprocess.run([*startup.PREFIX, binary, 'credential-import', '-d', str(root), '-e', origin,
                '-a', str(self.certdir / 'ca.pem'), '-s', str(bearer)], capture_output=True, timeout=budget(6))
            self.assertEqual(enroll.returncode, 0 if old_policy else 1)
            self.assertEqual(server.paths, ['/api/config', '/me'] if old_policy else ['/api/config'])
            if old_policy:
                self.assertEqual(json.loads(credential.read_bytes()), json.loads(before))
            else:
                self.assertEqual(credential.read_bytes(), before)
            if not old_policy:
                server.paths.clear()
                connect = subprocess.run([*startup.PREFIX, binary, 'connect', *self.session_flags(root), '-d', str(root), '-n', 'NetA', '-c', '#Room',
                    '-e', origin, '-m', 'plain', '-a', str(self.certdir / 'ca.pem')], capture_output=True, timeout=budget(6))
                self.assertEqual(connect.returncode, 1)
                self.assertEqual(server.paths, ['/api/config'])
                self.assertEqual(credential.read_bytes(), before)
            self.assertFalse(server.errors)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(3)

    def receive(self, query=False, case='normal', failure=False, ignored=False):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        server = cast(Any, startup.Server(('127.0.0.1', 0), Handler))
        server.query, server.case, server.terminal = query, case, failure
        server.paths, server.joins, server.errors = [], [], []
        server.protocol = int(os.environ.get('GIN_RECEPTION_PROTOCOL', '63'))
        for name in ['joined', 'release_message', 'message_emitted', 'stopped']:
            setattr(server, name, threading.Event())
        server.socket = self.tls().wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        proc = None
        stdout = stderr = b''
        read_fd = -1
        try:
            root = self.certdir / ('live-' + case + ('-query' if query else '-channel'))
            target = 'Peer' if query else '#Room'
            init = subprocess.run([*startup.PREFIX, binary, 'init', '-d', str(root), '-n', 'NetA', '-c', target], capture_output=True, timeout=budget(5))
            self.assertEqual(init.returncode, 0)
            outputs = list(root.rglob('out'))
            self.assertEqual(len(outputs), 1)
            output = outputs[0]
            output.write_bytes(PREFIX)
            inode = output.stat().st_ino
            origin = f'https://127.0.0.1:{server.server_port}'
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            before = credential.read_bytes()
            read_fd, write_fd = os.pipe()
            os.set_blocking(read_fd, False)
            os.set_blocking(write_fd, False)
            proc = subprocess.Popen([*startup.PREFIX, binary, 'connect', *self.session_flags(root), '-d', str(root), '-n', 'NetA', '-c', target,
                '-e', origin, '-m', 'plain', '-a', str(self.certdir / 'ca.pem'), '-R', str(write_fd)],
                pass_fds=(write_fd,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(write_fd)
            self.assertTrue(select.select([read_fd], [], [], budget(6))[0], 'no real READY')
            self.assertEqual(os.read(read_fd, 128), b'READY\n')
            self.assertTrue(server.joined.wait(budget(1)))
            server.release_message.set()
            self.assertTrue(server.message_emitted.wait(budget(3)), 'selected message phase not reached')
            echo_line = b'[1970-01-01T00:00:00.000Z] privmsg sender="OwnA" body="echo"\n'
            expected = PREFIX if failure else PREFIX + (echo_line if ignored else LINE + (
                echo_line if query else b''))
            deadline = time.monotonic() + budget(2)
            if failure:
                proc.wait(timeout=budget(5))
                self.assertEqual(proc.returncode, 1)
            else:
                while output.read_bytes() != expected and time.monotonic() < deadline:
                    self.assertIsNone(proc.poll(), 'connected owner stopped before append')
                    time.sleep(.02)
                if ignored:
                    time.sleep(.1)
                    self.assertIsNone(proc.poll())
            self.assertEqual(server.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                '/socket/websocket?vsn=2.0.0&client_proto=36'])
            self.assertEqual(server.joins, ['grappa:user:Alice', 'grappa:user:Alice/network:NetA',
                'grappa:user:Alice/network:NetA/channel:' + ('peer' if query else '#room')] +
                (['grappa:user:Alice/network:NetA/channel:owna'] if query else []))
            self.assertEqual(list(root.rglob('out')), [output], 'unexpected output fanout')
            self.assertEqual(output.read_bytes(), expected, 'incorrect real CLI event-to-log projection')
            self.assertEqual(output.stat().st_ino, inode, 'output inode replaced')
            self.assertTrue(credential.read_bytes() == before, 'credential changed')
        finally:
            server.stopped.set()
            server.release_message.set()
            if proc is not None:
                if proc.poll() is None:
                    proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=budget(8))
            if read_fd >= 0:
                os.close(read_fd)
            server.shutdown()
            server.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertFalse(server.errors, 'strict verified-TLS fixture handler failure')
            if proc is not None:
                self.assertEqual(proc.returncode, 1 if failure else 0, 'unexpected owner result')
                self.assertEqual(stdout, b'')
                self.assertFalse(any(value in stdout + stderr for value in [startup.TOKEN.encode(), b'YWF+', b'secret-ciphertext']))


if __name__ == '__main__':
    unittest.main(verbosity=2)
