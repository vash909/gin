#!/usr/bin/env python3
"""Native-only strict local TLS characterization. No live/account authority."""
import base64
import contextlib
import hashlib
import json
import os
import selectors
import signal
import socket
import subprocess
import threading
import time
import unittest
import test_http
from ws_fixture_frames import server_frame, take_client_frame
TOKEN = 'fixture-token.DO-NOT-LOG'
CONFIG = {'server': 'grappa', 'protocol_version': 7, 'min_protocol_version': 3}
ME = {'kind': 'user', 'id': '22345678-1234-1234-1234-123456789abc', 'name': 'FreshLabel'}
PLAN = ['grappa:user:FreshLabel', 'grappa:user:FreshLabel/network:one',
        'grappa:user:FreshLabel/network:one/channel:#old1',
        'grappa:user:FreshLabel/network:two',
        'grappa:user:FreshLabel/network:two/channel:#old2',
        'grappa:user:FreshLabel/network:one/channel:own1',
        'grappa:user:FreshLabel/network:one/channel:newpeer']

def drain_tail(c, buf, state):
    while True:
        while True:
            frame = take_client_frame(buf)
            if frame is None:
                break
            assert frame[0] in (8, 10), 'unexpected extra operation'
        block = c.recv(4096)
        if not block:
            assert not buf, 'partial frame at EOF'
            state['eof'] += 1
            return
        buf.extend(block)

class Hot(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Reuse certificate construction, not HTTPTests' test methods.
        test_http.HTTPTests.setUpClass()

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass()

    @contextlib.contextmanager
    def fixture(self, mode):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0))
        listener.listen(8)
        listener.settimeout(.05)
        state = {'paths': [], 'joins': [], 'errors': [], 'workers': [], 'eof': 0, 'upgrades': 0}
        stop = threading.Event()
        gate = threading.Event()
        ctx = test_http.HTTPTests().tls()

        def send(c, value):
            c.sendall(server_frame(json.dumps(value, separators=(',', ':')).encode()))

        def worker(raw):
            c = None
            try:
                raw.settimeout(4)
                c = ctx.wrap_socket(raw, server_side=True)
                c.settimeout(4)
                data = b''
                while b'\r\n\r\n' not in data:
                    block = c.recv(4096)
                    assert block, 'header EOF'
                    data += block
                    assert len(data) < 16384
                head, tail = data.split(b'\r\n\r\n', 1)
                lines = head.decode().split('\r\n')
                method, path, _ = lines[0].split()
                assert method == 'GET' and not tail
                headers = dict(x.split(': ', 1) for x in lines[1:])
                state['paths'].append(path)
                if path.startswith('/socket/'):
                    state['upgrades'] += 1
                    assert state['upgrades'] == 1
                    assert headers['Sec-WebSocket-Protocol'] == 'phoenix,base64url.bearer.phx.' + base64.b64encode(TOKEN.encode()).decode().rstrip('=')
                    assert 'Authorization' not in headers
                    accept = base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                    c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                    buf = bytearray()

                    def recvframe():
                        while True:
                            item = take_client_frame(buf)
                            if item:
                                op, fin, payload = item
                                if op == 10:
                                    continue
                                assert op == 1 and fin
                                return json.loads(payload)
                            block = c.recv(16384)
                            assert block, 'premature WS EOF'
                            buf.extend(block)

                    for i, topic in enumerate(PLAN):
                        f = recvframe()
                        state['joins'].append(f)
                        ref = str(i + 1)
                        assert f == [ref, ref, topic, 'phx_join', {}], f
                        if i < 6:
                            send(c, [ref, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': 7} if i == 0 else {}}])
                        else:
                            # A wrong ref plus prejoin data must not grant membership.
                            send(c, ['7', 'wrong', topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                            send(c, ['7', None, topic, 'hot_marker', {'new': True}])
                            send(c, ['3', None, PLAN[2], 'hot_marker', {'old': True}])
                            assert gate.wait(3), 'client receipt barrier not observed'
                            if mode != 'timeout':
                                send(c, ['7', '7', topic, 'phx_reply', {'status': 'error' if mode == 'reject' else 'ok', 'response': {}}])
                            if mode == 'positive':
                                send(c, ['7', None, topic, 'hot_marker', {'new': True}])
                                send(c, ['3', None, PLAN[2], 'hot_marker', {'old': True}])
                    drain_tail(c, buf, state)
                else:
                    assert path in ('/api/config', '/me')
                    if path == '/api/config':
                        assert 'Authorization' not in headers
                    else:
                        assert headers['Authorization'] == 'Bearer ' + TOKEN
                    body = json.dumps(CONFIG if path == '/api/config' else ME).encode()
                    c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
            except BaseException as error:
                state['errors'].append(type(error).__name__ + ': ' + str(error))
            finally:
                if c:
                    c.close()
                raw.close()

        def accept():
            try:
                while not stop.is_set():
                    try:
                        raw, _ = listener.accept()
                    except socket.timeout:
                        continue
                    assert len(state['workers']) < 8
                    thread = threading.Thread(target=worker, args=(raw,))
                    state['workers'].append(thread)
                    thread.start()
            except BaseException as error:
                state['errors'].append(repr(error))

        thread = threading.Thread(target=accept)
        thread.start()
        try:
            yield state, f'https://127.0.0.1:{listener.getsockname()[1]}', gate
        finally:
            stop.set()
            end = time.monotonic() + 5
            thread.join(max(0, end - time.monotonic()))
            listener.close()
            assert not thread.is_alive()
            for worker_thread in state['workers']:
                worker_thread.join(max(0, end - time.monotonic()))
                assert not worker_thread.is_alive(), 'worker leak'
            assert not state['errors'], state['errors']

    def scenario(self, mode):
        with self.fixture(mode) as (state, origin, gate):
            p = subprocess.Popen([os.environ['GIN_HOT_DRIVER'], origin, str(test_http.HTTPTests.certdir / 'ca.pem'), mode], stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
            try:
                output = p.stdout
                assert output is not None
                with selectors.DefaultSelector() as selector:
                    selector.register(output, selectors.EVENT_READ)
                    assert selector.select(8), 'outer receipt deadline'
                    line = output.readline()
                if not line:
                    _, err = p.communicate(timeout=2)
                    self.fail('early driver exit: ' + err.decode(errors='replace'))
                self.assertEqual(line, b'OLD_RECEIVED_JOINING_NEW_ZERO\n')
                gate.set()
                out, err = p.communicate(timeout=8)
            except BaseException:
                if p.poll() is None:
                    os.killpg(p.pid, signal.SIGKILL)
                p.communicate()
                raise
            self.assertEqual(p.returncode, 0, err)
            self.assertEqual(err, b'')
            self.assertEqual(out, b'PASS HOT SAME_EPOCH\n')
            self.assertNotIn(TOKEN.encode(), line + out + err)
        self.assertEqual(state['paths'], ['/api/config', '/me', '/socket/websocket?vsn=2.0.0&client_proto=4'])
        self.assertEqual(len(state['joins']), 7)
        self.assertEqual(state['upgrades'], 1)
        self.assertEqual(state['eof'], 1)
        print('PASS ' + mode + ': config=1 me=1 upgrade=1 joins=7 strict_EOF=1 client_barrier=1')

    def test_same_socket(self):
        self.scenario('positive')

    def test_join_rejected(self):
        self.scenario('reject')

    def test_join_timeout(self):
        self.scenario('timeout')

if __name__ == '__main__':
    unittest.main(verbosity=2)
