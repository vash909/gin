"""Real held-root FIFO -> native HTTPS adapter; synthetic authority only."""
import base64
import hashlib
import contextlib
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import tempfile
import threading
import unittest
import test_connect
from ws_fixture_frames import server_frame, take_client_frame

ROOT = Path(__file__).resolve().parents[1]
PATH = '/networks/Net/channels/%23ROOM/messages'


class Server(test_connect.Server):
    posts: list
    stop: threading.Event
    posted: threading.Event
    before: threading.Event
    before_sent: threading.Event
    echo_order: list


class Handler(test_connect.Handler):
    server: Server
    def do_GET(self):
        srv = self.server
        srv.records.append((self.path, dict(self.headers)))
        if self.path == '/api/config':
            body = dict(test_connect.CONFIG)
            if srv.mode == 'floor' or (srv.mode == 'fresh' and srv.posts):
                body['min_protocol_version'] = 7
        elif self.path == '/me':
            body = {'kind': 'user', 'id': '12345678-1234-1234-1234-123456789abc', 'name': 'Alice'}
            if srv.mode == 'uuid':
                body['id'] = '22345678-1234-1234-1234-123456789abc'
        else:
            self.upgrade()
            return
        self.respond(200, json.dumps(body).encode())

    def upgrade(self):
        srv = self.server
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for k, v in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                     ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(20)
        buf = bytearray()
        topic = ''
        try:
            for index in range(3):
                item = take_client_frame(buf)
                while item is None:
                    data = self.connection.recv(16384)
                    if not data:
                        return
                    buf.extend(data)
                    item = take_client_frame(buf)
                opcode, final, payload = item
                assert opcode == 1 and final
                jr, ref, topic, event, body = json.loads(payload)
                assert event == 'phx_join' and body == {} and jr == ref
                srv.joins.append(topic)
                response = {'protocol_version': 7} if index == 0 else {}
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': response}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
            for index in range(2):
                assert (srv.before if index == 0 else srv.posted).wait(20)
                text = json.loads(srv.posts[0][2])['body']
                row = {'network': 'Net', 'channel': '#room', 'kind': 'privmsg',
                       'sender': 'Alice', 'server_time': 0, 'body': text}
                frame = [None, None, topic, 'event', {'kind': 'message', 'message': row}]
                self.connection.sendall(server_frame(json.dumps(frame).encode()))
                srv.echo_order.append("before" if index == 0 else "after")
                if index == 0:
                    srv.before_sent.set()
            while self.connection.recv(1024):
                pass
        except OSError:
            pass
        finally:
            self.close_connection = True

    def respond(self, status, raw):
        self.send_response(status)
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def do_POST(self):
        srv = self.server
        raw = self.rfile.read(int(self.headers['Content-Length']))
        srv.records.append((self.path, dict(self.headers)))
        srv.posts.append((self.path, dict(self.headers), raw))
        if srv.mode == 'echo':
            srv.before.set()
            assert srv.before_sent.wait(20)
        if srv.mode == 'lost':
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if srv.mode == 'timeout':
            srv.stop.wait(12)
            return
        if srv.mode == 'incomplete':
            self.send_response(201)
            self.send_header('Content-Length', '200')
            self.end_headers()
            self.wfile.write(b'{"id":42,"network":"Net"}')
            self.wfile.flush()
            self.close_connection = True
            return
        status, body = 201, {'id': 42, 'network': 'Net', 'future': {'bounded': True}}
        if srv.mode == '202':
            status, body = 202, {'ok': True}
        elif srv.mode == 'remote':
            status, body = 500, {'error': 'private-error-do-not-print'}
        elif srv.mode == 'redirect':
            self.send_response(302)
            self.send_header('Location', 'https://127.0.0.1:1/leak')
            self.send_header('Content-Length', '0')
            self.end_headers()
            return
        elif srv.mode == 'unknown-status':
            status = 200
        elif srv.mode == 'malformed':
            body = {'id': 42, 'network': 'Wrong'}
        elif srv.mode == 'limit':
            self.respond(201, b'x' * 65537)
            return
        srv.echo_order.append("response")
        self.respond(status, json.dumps(body).encode())
        self.wfile.flush()
        srv.posted.set()


class SendTests(unittest.TestCase):
    certdir: Path
    @classmethod
    def setUpClass(cls):
        test_connect.ConnectTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_connect.ConnectTests.tearDownClass.__func__(cls)

    tls = test_connect.ConnectTests.tls

    @contextlib.contextmanager
    def fixture(self, mode='ok', cert='good'):
        srv = Server(('127.0.0.1', 0), Handler)
        srv.mode, srv.records, srv.posts = mode, [], []
        srv.stop = threading.Event()
        srv.posted = threading.Event()
        srv.before = threading.Event()
        srv.before_sent = threading.Event()
        srv.echo_order = []
        srv.joins = []
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stop.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())

    def run_fifo(self, origin, records=(b'/hello "world" \\ caf\xc3\xa9\n',), mode='ok', ca='ca.pem', chunks=None):
        binary = os.environ.get('GIN_SEND_DRIVER', str(ROOT / '.build/send_driver'))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve() / 'root'
            command = shlex.split(os.environ.get('GIN_SEND_PREFIX', '')) + [binary, origin,
                str(self.certdir / ca), str(root), mode, str(len(records))]
            p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                assert p.stdout is not None
                self.assertEqual(p.stdout.readline(), b'ready\n')
                fd = os.open(root / '%4Eet' / 'room' / 'in', os.O_WRONLY)
                try:
                    for record in records:
                        if chunks:
                            for chunk in chunks:
                                self.assertEqual(os.write(fd, chunk), len(chunk))
                        else:
                            # Oversize is intentionally a hostile fragmented writer.
                            cap = os.fpathconf(fd, 'PC_PIPE_BUF')
                            for start in range(0, len(record), cap):
                                part = record[start:start + cap]
                                self.assertEqual(os.write(fd, part), len(part))
                finally:
                    os.close(fd)
                out, err = p.communicate(timeout=35)
                self.assertEqual(p.returncode, 0, err.decode(errors='replace'))
                self.assertEqual(err, b'')
                expected_log = b''
                if mode == 'echo':
                    expected_log = b'[1970-01-01T00:00:00.000Z] privmsg sender="Alice" body="echo text"\n' * 2
                self.assertEqual((root / '%4Eet' / 'room' / 'out').read_bytes(), expected_log)
                self.assertNotIn(b'aa~', out + err)
                self.assertNotIn(b'private-error', out + err)
                return out.decode().splitlines()
            finally:
                if p.poll() is None:
                    p.kill()
                    p.communicate()

    def test_exact_action_and_accepted_results(self):
        for mode, outcome, mid, status in [('ok', 'accepted-persisted', 42, 201),
                                         ('202', 'accepted-no-persist', 0, 202)]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.run_fifo(origin), [f'ok {outcome} 101 {mid} {status} 1 1'])
                self.assertEqual([x[0] for x in srv.records], ['/api/config', '/me', PATH])
                self.assertNotIn('Authorization', srv.records[0][1])
                self.assertEqual(srv.records[1][1]['Authorization'], 'Bearer aa~')
                self.assertEqual(len(srv.posts), 1)
                path, headers, raw = srv.posts[0]
                self.assertEqual(path, PATH)
                self.assertEqual(headers['Authorization'], 'Bearer aa~')
                self.assertEqual(headers['Content-Type'], 'application/json')
                self.assertEqual(json.loads(raw), {'body': '/hello "world" \\ café'})

    def test_query_uses_same_channels_action(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin, mode='query'), ['ok accepted-persisted 101 42 201 1 1'])
            self.assertEqual(srv.posts[0][0], '/networks/Net/channels/Nick/messages')
            self.assertEqual(set(json.loads(srv.posts[0][2])), {'body'})

    def test_failures_are_not_replayed_or_echoed(self):
        cases = [('remote', 'remote remote-failure', 500), ('redirect', 'remote remote-failure', 302),
                 ('malformed', 'malformed uncertain', 201), ('unknown-status', 'malformed uncertain', 200),
                 ('incomplete', 'transport uncertain', 201), ('lost', 'transport uncertain', 0),
                 ('timeout', 'timeout uncertain', 0), ('limit', 'limit uncertain', 201)]
        for mode, prefix, status in cases:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.run_fifo(origin, mode=mode), [f'{prefix} 101 0 {status} 1 1'])
                self.assertEqual(len(srv.posts), 1)

    def test_local_and_policy_fail_closed(self):
        for mode, code in [('origin', 'invalid'), ('mode', 'unsupported'), ('unknown', 'invalid'),
                           ('deadline', 'invalid'), ('id', 'invalid'), ('root', 'invalid')]:
            with self.subTest(mode=mode), self.fixture() as (srv, origin):
                identity = 0 if mode == 'id' else 101
                self.assertEqual(self.run_fifo(origin, mode=mode), [f'{code} not-attempted {identity} 0 0 0 0'])
                self.assertEqual(srv.records, [])
        for mode, expected, paths in [('floor', 'protocol not-attempted 101 0 0 0 0', ['/api/config']),
                                     ('uuid', 'auth not-attempted 101 0 0 1 0', ['/api/config', '/me'])]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.run_fifo(origin), [expected])
                self.assertEqual([x[0] for x in srv.records], paths)

    def test_fresh_policy_same_process_and_request_ids(self):
        with self.fixture('fresh') as (srv, origin):
            self.assertEqual(self.run_fifo(origin, (b'first\n', b'second\n')),
                ['ok accepted-persisted 101 42 201 1 1', 'protocol not-attempted 102 0 0 0 0'])
            self.assertEqual([x[0] for x in srv.records], ['/api/config', '/me', PATH, '/api/config'])

    def test_fifo_rejections_and_partial_utf8(self):
        for raw in [b'\xc0\xaf\n', b'a\x00b\n', b'x' * 4097 + b'\n', b'unfinished']:
            with self.subTest(raw=raw[:12]), self.fixture() as (srv, origin):
                result = self.run_fifo(origin, (raw,))
                self.assertEqual(result, ['invalid not-attempted 101 0 0 0 0'] if raw[0] == 0xc0 else ['rejected'])
                self.assertEqual(srv.records, [])
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin, chunks=[b'caf\xc3', b'\xa9\n']),
                             ['ok accepted-persisted 101 42 201 1 1'])
            self.assertEqual(json.loads(srv.posts[0][2]), {'body': 'café'})

    def test_real_wss_server_emission_before_and_after_http_response(self):
        """Prove server emission order; the driver receives both events after POST returns."""
        with self.fixture('echo') as (srv, origin):
            self.assertEqual(self.run_fifo(origin, (b'echo text\n',), mode='echo'), ['ok accepted-persisted 101 42 201 1 1'])
            self.assertEqual(srv.echo_order, ['before', 'response', 'after'])
            self.assertEqual(len(srv.posts), 1)
            self.assertEqual(srv.joins, ['grappa:user:Alice', 'grappa:user:Alice/network:Net',
                                       'grappa:user:Alice/network:Net/channel:#room'])

    def test_tls_fail_closed(self):
        for cert, ca in [('wrong', 'ca.pem'), ('good', 'bad.pem')]:
            with self.subTest(cert=cert), self.fixture(cert=cert) as (srv, origin):
                self.assertEqual(self.run_fifo(origin, ca=ca), ['protocol not-attempted 101 0 0 0 0'])
                self.assertEqual(srv.records, [])


if __name__ == '__main__':
    unittest.main(verbosity=2)
