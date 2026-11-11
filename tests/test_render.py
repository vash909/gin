"""Real native WSS callback -> bounded owned queue -> held-root append.

Synthetic loopback authority only; this is not live Grappa qualification.
"""
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import unittest
import test_connect
from ws_fixture_frames import server_frame, take_client_frame

ROOT = Path(__file__).resolve().parents[1]
LINE = '[1970-01-01T00:00:00.000Z] action sender="Nick" body="café\\x0Awaves"\n'.encode()


class Handler(test_connect.Handler):
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
        self.connection.settimeout(5)
        buf = bytearray()
        try:
            topic = ''
            for index in range(3):
                while True:
                    item = take_client_frame(buf)
                    if item is not None:
                        break
                    data = self.connection.recv(16384)
                    if not data:
                        return
                    buf.extend(data)
                opcode, final, payload = item
                assert opcode == 1 and final
                jr, ref, topic, event, body = json.loads(payload)
                assert event == 'phx_join' and body == {} and jr == ref
                srv.joins.append(topic)
                response = {'protocol_version': 7} if index == 0 else {}
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': response}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
            row = {'network': 'Net', 'channel': '#room', 'kind': 'action', 'sender': 'Nick',
                   'server_time': 0, 'body': '\x01ACTION café\nwaves'}
            good = {'kind': 'message', 'message': row}
            if srv.mode == 'bytes':
                row['kind'] = 'privmsg'
                row['body'] = '\x03' * 16384
            payloads = [good] * (10 if srv.mode == 'overflow' else 5 if srv.mode == 'bytes' else 1)
            payloads += [{'kind': 'e2e_future'}, {'kind': 'message', 'message': {}}, {'kind': 'future'}]
            for payload in payloads:
                raw = json.dumps([None, None, topic, 'event', payload], ensure_ascii=False).encode()
                split = raw.index(b'\xc3') + 1 if b'\xc3' in raw else len(raw) // 2
                self.connection.sendall(server_frame(raw[:split], final=False) +
                                        server_frame(raw[split:], opcode=0))
            while self.connection.recv(4096):
                pass
        except OSError:
            pass
        finally:
            self.close_connection = True


class RenderTests(unittest.TestCase):
    certdir: Path
    @classmethod
    def setUpClass(cls):
        test_connect.ConnectTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_connect.ConnectTests.tearDownClass.__func__(cls)

    tls = test_connect.ConnectTests.tls

    @contextlib.contextmanager
    def fixture(self, mode):
        srv = test_connect.Server(('127.0.0.1', 0), Handler)
        srv.mode = mode
        srv.records = []
        srv.joins = []
        srv.upgrades = 0
        srv.socket = self.tls('good').wrap_socket(srv.socket, server_side=True)
        t = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        t.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.shutdown()
            srv.server_close()
            t.join(3)
            self.assertFalse(t.is_alive())

    def test_native_owned_records_and_overflow(self):
        binary = os.environ.get('GIN_RENDER_DRIVER', str(ROOT / '.build/render_driver'))
        for mode, count, accepted, overflow in [('normal', 4, 1, 0), ('overflow', 13, 8, 2), ('bytes', 8, 3, 2)]:
            with self.subTest(mode=mode), tempfile.TemporaryDirectory() as tmp, self.fixture(mode) as (srv, origin):
                root = Path(tmp).resolve() / 'root'
                command = shlex.split(os.environ.get('GIN_RENDER_PREFIX', '')) + [binary, origin,
                    str(self.certdir / 'ca.pem'), str(root), str(count)]
                p = subprocess.run(command, capture_output=True, timeout=30)
                self.assertEqual(p.returncode, 0, p.stderr.decode(errors='replace'))
                self.assertEqual(p.stderr, b'')
                line = (('[1970-01-01T00:00:00.000Z] privmsg sender="Nick" body="' + '\\x03' * 16384 + '"\n').encode()
                        if mode == 'bytes' else LINE)
                self.assertEqual(p.stdout.decode(), f'records={accepted} errors=2 overflow={overflow} bytes={len(line) * accepted}\n')
                self.assertEqual((root / '%4Eet' / 'room' / 'out').read_bytes(), line * accepted)
                self.assertEqual(srv.joins, ['grappa:user:Alice', 'grappa:user:Alice/network:Net',
                                           'grappa:user:Alice/network:Net/channel:#room'])


if __name__ == '__main__':
    unittest.main()
