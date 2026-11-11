"""Real FIFO -> command parser -> isolated REST controls; synthetic TLS only."""
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
import test_send

ROOT = Path(__file__).resolve().parents[1]

class Handler(test_send.Handler):
    def do_GET(self):
        if self.server.mode == 'old-server' and self.path == '/api/config':
            self.server.records.append((self.path, dict(self.headers)))
            body = dict(test_send.test_connect.CONFIG)
            body['protocol_version'] = 3
            body['min_protocol_version'] = 1
            self.respond(200, json.dumps(body).encode())
        else:
            super().do_GET()

    def do_POST(self):
        srv = self.server
        raw = self.rfile.read(int(self.headers.get('Content-Length', '0')))
        srv.records.append((self.path, dict(self.headers)))
        srv.posts.append((self.command, self.path, dict(self.headers), raw))
        if srv.mode == 'lost':
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)
            return
        if srv.mode == 'timeout':
            srv.stop.wait(12)
            return
        if srv.mode == 'incomplete':
            self.send_response(202); self.send_header('Content-Length', '200'); self.end_headers()
            self.wfile.write(b'{"ok":true}'); self.wfile.flush(); self.close_connection = True
            return
        status, raw = 202, b'{"ok":true}'
        if srv.mode.isdigit(): status = int(srv.mode)
        bodies = {
            'false': b'{"ok":false}', 'string': b'{"ok":"true"}',
            'duplicate': b'{"ok":false,"o\\u006b":true}',
            'nested-duplicate': b'{"ok":true,"extra":{"x":1,"x":2}}',
            'trailing': b'{"ok":true} {}', 'malformed': b'{"ok":true',
            'nested': b'{"extra":{"ok":true}}', 'nul': b'{"ok":true}\x00',
            'invalid-utf8': b'{"ok":true,"extra":"\xff"}',
            'limit': b'x' * 65537,
            'depth': b'{"ok":true,"extra":' + b'[' * 33 + b'0' + b']' * 33 + b'}',
            'escaped': b'{"o\\u006b":true,"extra":{"nested":[]}}',
        }
        raw = bodies.get(srv.mode, raw)
        self.respond(status, raw)
    do_DELETE = do_POST

class ControlTests(unittest.TestCase):
    certdir: Path
    @classmethod
    def setUpClass(cls): test_send.SendTests.setUpClass.__func__(cls)
    @classmethod
    def tearDownClass(cls): test_send.SendTests.tearDownClass.__func__(cls)
    tls = test_send.SendTests.tls

    @contextlib.contextmanager
    def fixture(self, mode='ok', cert='good'):
        srv = test_send.Server(('127.0.0.1', 0), Handler)
        srv.mode, srv.records, srv.posts = mode, [], []
        srv.stop = threading.Event()
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stop.set(); srv.shutdown(); srv.server_close(); thread.join(3)
            self.assertFalse(thread.is_alive())

    def run_fifo(self, origin, records=(b'/join #ROOM\n',), mode='ok', ca='ca.pem', chunks=None):
        binary = os.environ.get('GIN_CONTROL_DRIVER', str(ROOT / '.build/control_driver'))
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve() / 'root'
            cmd = shlex.split(os.environ.get('GIN_CONTROL_PREFIX', '')) + [binary, origin,
                str(self.certdir / ca), str(root), mode, str(len(records))]
            p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                assert p.stdout is not None
                self.assertEqual(p.stdout.readline(), b'ready\n')
                # Adapter must leave these existing objects and their bytes unchanged.
                before = {str(x.relative_to(root)): (x.stat().st_ino, x.stat().st_mode,
                    x.read_bytes() if x.is_file() else None) for x in root.rglob('*')}
                fd = os.open(root / '%4Eet' / 'room' / 'in', os.O_WRONLY)
                try:
                    for record in records:
                        for part in (chunks or [record]):
                            self.assertEqual(os.write(fd, part), len(part))
                finally:
                    os.close(fd)
                out, err = p.communicate(timeout=35)
                self.assertEqual(p.returncode, 0, err.decode(errors='replace'))
                self.assertEqual(err, b'')
                after = {str(x.relative_to(root)): (x.stat().st_ino, x.stat().st_mode,
                    x.read_bytes() if x.is_file() else None) for x in root.rglob('*')}
                self.assertEqual(after, before)
                self.assertNotIn(b'aa~', out + err)
                return out.decode().splitlines()
            finally:
                if p.poll() is None:
                    p.kill(); p.communicate()

    def test_control_then_literal_retains_owner_destination(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin,
                (b'/join #Other\n', b'//part #Else\n'), mode='literal'),
                ['0 1 101 202 0 0 1', 'send 0 2 102 202'])
            self.assertEqual([p[1] for p in srv.posts],
                ['/networks/Net/channels', '/networks/Net/channels/%23ROOM/messages'])
            self.assertEqual(json.loads(srv.posts[1][3]), {'body': '/part #Else'})

    def test_literal_slash_uses_plain_send(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin, (b'//join #ROOM\n',), mode='literal'),
                ['send 0 2 101 202'])
            self.assertEqual(len(srv.posts), 1)
            self.assertEqual(srv.posts[0][0:2], ('POST', '/networks/Net/channels/%23ROOM/messages'))
            self.assertEqual(json.loads(srv.posts[0][3]), {'body': '/join #ROOM'})

    def test_exact_part_original_segments(self):
        for target in ['#MiXeD%café', '&', '#', '+Room', '!Room']:
            with self.subTest(target=target), self.fixture() as (srv, origin):
                self.assertEqual(self.run_fifo(origin, (('/part ' + target + '\n').encode(),)), ['0 1 101 202 0 0 1'])
                from urllib.parse import quote
                self.assertEqual(srv.posts[0][0:2], ('DELETE', '/networks/Net/channels/' + quote(target, safe='-._~')))
                self.assertEqual(srv.posts[0][3], b'')
                self.assertNotIn('Content-Type', srv.posts[0][2])

    def test_ack_and_error_classification_no_replay(self):
        cases = [(str(x), 15, 3, x) for x in (200, 201, 204)]
        cases += [(m, 15, 3, 202) for m in ('false', 'string', 'duplicate', 'nested-duplicate',
            'trailing', 'malformed', 'nested', 'nul', 'invalid-utf8')]
        cases += [('limit', 4, 3, 202), ('depth', 4, 3, 202), ('incomplete', 13, 3, 202),
            ('lost', 13, 3, 0), ('timeout', 12, 3, 0), ('escaped', 0, 1, 202)]
        cases += [(str(status), code, 2, status) for status, code in
            [(302, 14), (400, 14), (401, 6), (403, 8), (426, 9), (429, 10), (500, 14)]]
        for mode, code, outcome, status in cases:
            for verb in ('join', 'part'):
                with self.subTest(mode=mode, verb=verb), self.fixture(mode) as (srv, origin):
                    self.assertEqual(self.run_fifo(origin, (f'/{verb} #ROOM\n'.encode(),), mode=mode),
                        [f'{code} {outcome} 101 {status} 0 0 1'])
                    self.assertEqual(len(srv.posts), 1)

    def test_local_rejections_before_discovery(self):
        for mode, code in [('origin', 1), ('mode', 2), ('unknown-mode', 2), ('unknown', 1),
                ('query', 1), ('deadline', 1), ('policy', 1), ('id', 1), ('root', 1), ('bad-lock', 1), ('bad-root', 1)]:
            with self.subTest(mode=mode), self.fixture() as (srv, origin):
                identity = 0 if mode == 'id' else 101
                self.assertEqual(self.run_fifo(origin, mode=mode), [f'{code} 0 {identity} 0 1 1 0'])
                self.assertEqual(srv.records, [])
        for record in [b'/quit\n', b'/part #a/b\n', b'/join #a,#b\n', b'/part #a reason\n', b'/join +\n',
                b'/join #a b\n', b'/join #\xc0\xaf\n', b'/join #' + b'x' * 50 + b'\n',
                b'/join #a\x00b\n', b'/join #a\r\n', b'/join #unfinished']:
            with self.subTest(record=record), self.fixture() as (srv, origin):
                self.assertEqual(self.run_fifo(origin, (record,)), ['rejected'])
                self.assertEqual(srv.records, [])

    def test_preflight_identity_and_freshness(self):
        for mode, result, paths in [('floor', '5 0 101 0 4 1 0', ['/api/config']),
                ('uuid', '5 0 101 0 0 7 0', ['/api/config', '/me']),
                ('old-server', '5 0 101 0 5 1 0', ['/api/config'])]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.run_fifo(origin), [result])
                self.assertEqual([x[0] for x in srv.records], paths)
                self.assertEqual(srv.posts, [])
        with self.fixture('fresh') as (srv, origin):
            self.assertEqual(self.run_fifo(origin, (b'/join #ROOM\n', b'/part #ROOM\n')),
                ['0 1 101 202 0 0 1', '5 0 102 0 4 1 0'])
            self.assertEqual([x[0] for x in srv.records], ['/api/config', '/me', '/networks/Net/channels', '/api/config'])
            self.assertNotIn('Authorization', srv.records[-1][1])

    def test_tls_ca_and_san_fail_closed(self):
        for cert, ca in [('wrong', 'ca.pem'), ('good', 'bad.pem')]:
            with self.subTest(cert=cert), self.fixture(cert=cert) as (srv, origin):
                self.assertEqual(self.run_fifo(origin, ca=ca), ['11 0 101 0 6 1 0'])
                self.assertEqual(srv.records, [])

    def test_split_utf8_fifo_and_exact_join(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin, chunks=[b'/join #caf\xc3', b'\xa9\n']), ['0 1 101 202 0 0 1'])
            self.assertEqual(json.loads(srv.posts[0][3]), {'name': '#café'})

    def test_native_api_join(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin, mode='native'), ['0 1 101 202 0 0 1'])
            self.assertEqual(len(srv.posts), 1)

    def test_exact_join(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.run_fifo(origin), ['0 1 101 202 0 0 1'])
            self.assertEqual([x[0] for x in srv.records], ['/api/config', '/me', '/networks/Net/channels'])
            self.assertNotIn('Authorization', srv.records[0][1])
            self.assertEqual(srv.records[1][1]['Authorization'], 'Bearer aa~')
            method, path, headers, raw = srv.posts[0]
            self.assertEqual(method, 'POST')
            self.assertEqual(headers['Content-Type'], 'application/json')
            self.assertEqual(headers['Authorization'], 'Bearer aa~')
            self.assertEqual(json.loads(raw), {'name': '#ROOM'})

if __name__ == '__main__':
    unittest.main(verbosity=2)
