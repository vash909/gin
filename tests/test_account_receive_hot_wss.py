#!/usr/bin/env python3
"""Owned account facade, strict loopback TLS characterization, not CLI proof."""
import base64
import contextlib
import hashlib
import json
import os
import selectors
import signal
import socket
import ssl
import subprocess
import threading
import time
import unittest
from collections.abc import Iterator
from typing import TypedDict
import shlex
import test_http
from ws_fixture_frames import server_frame, take_client_frame

def driver_command(origin: str, ca: str, mode: str) -> list[str]:
    prefix = shlex.split(os.environ.get('GIN_ACCOUNT_HOT_PREFIX',''))
    # Only parsed executable argv enables the child-only TLS setup allowance.
    return prefix + [os.environ['GIN_ACCOUNT_HOT_DRIVER'], origin, ca, mode] + (
        ['--instrumented-setup'] if prefix else [])

TOKEN = 'fixture-token.DO-NOT-LOG'
CONFIG = {'server': 'grappa', 'protocol_version': 7, 'min_protocol_version': 3}
ME = {'kind': 'user', 'id': '22345678-1234-1234-1234-123456789abc', 'name': 'FreshAlice'}
PLAN = ['grappa:user:FreshAlice', 'grappa:user:FreshAlice/network:one',
        'grappa:user:FreshAlice/network:two',
        'grappa:user:FreshAlice/network:one/channel:$server',
        'grappa:user:FreshAlice/network:two/channel:$server',
        'grappa:user:FreshAlice/network:one/channel:#old',
        'grappa:user:FreshAlice/network:one/channel:peer',
        'grappa:user:FreshAlice/network:one/channel:own1',
        'grappa:user:FreshAlice/network:two/channel:own2',
        'grappa:user:FreshAlice/network:one/channel:newpeer']

class State(TypedDict):
    paths: list[str]
    joins: list[object]
    errors: list[str]
    workers: list[threading.Thread]
    eof: int
    upgrades: int

def drain_tail(c: ssl.SSLSocket, buf: bytearray, state: State) -> None:
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

class OwnedHot(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        test_http.HTTPTests.setUpClass()

    @classmethod
    def tearDownClass(cls) -> None:
        test_http.HTTPTests.tearDownClass()

    @contextlib.contextmanager
    def fixture(self, mode: str) -> Iterator[tuple[State, str, threading.Event]]:
        assert mode in ('positive', 'reject', 'timeout', 'recovery', 'recovery_close', 'preflight')
        recovery = mode in ('recovery', 'recovery_close', 'preflight')
        listener = socket.socket()
        state: State = {'paths': [], 'joins': [], 'errors': [], 'workers': [], 'eof': 0, 'upgrades': 0}
        stop = threading.Event()
        gate = threading.Event()
        accept_thread: threading.Thread | None = None
        try:
            listener.bind(('127.0.0.1', 0))
            listener.listen(8)
            listener.settimeout(.05)
            ctx = test_http.HTTPTests().tls()

            def send(c: ssl.SSLSocket, value: object) -> None:
                c.sendall(server_frame(json.dumps(value, separators=(',', ':')).encode()))

            def worker(raw: socket.socket) -> None:
                c: ssl.SSLSocket | None = None
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
                        assert state['upgrades'] <= (2 if recovery else 1)
                        assert headers['Sec-WebSocket-Protocol'] == 'phoenix,base64url.bearer.phx.' + base64.b64encode(TOKEN.encode()).decode().rstrip('=')
                        assert 'Authorization' not in headers
                        accept = base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                        if mode in ('recovery', 'recovery_close') and state['upgrades'] == 1:
                            c.sendall(b'HTTP/1.1 403 Forbidden\r\nConnection: close\r\nContent-Length: 0\r\n\r\n')
                            return
                        c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                        buf = bytearray()

                        def recvframe(connection: ssl.SSLSocket) -> object:
                            while True:
                                item = take_client_frame(buf)
                                if item:
                                    op, fin, payload = item
                                    if op == 10:
                                        continue
                                    assert op == 1 and fin
                                    return json.loads(payload)
                                block = connection.recv(16384)
                                assert block, 'premature WS EOF'
                                buf.extend(block)

                        for i, topic in enumerate(PLAN[:9] if recovery else PLAN):
                            f = recvframe(c)
                            state['joins'].append(f)
                            ref = str(i + 1)
                            assert f == [ref, ref, topic, 'phx_join', {}], 'JOIN golden mismatch'
                            if i < 9:
                                send(c, [ref, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': 7} if i == 0 else {}}])
                                if i == 8 and not recovery:
                                    send(c, ['6', None, PLAN[5], 'hot_marker', {'stage': 'queued'}])
                            else:
                                send(c, ['10', 'wrong', topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                                send(c, ['10', None, topic, 'hot_marker', {'stage': 'prejoin'}])
                                send(c, ['6', None, PLAN[5], 'hot_marker', {'stage': 'held'}])
                                assert gate.wait(3), 'client receipt barrier not observed'
                                if mode != 'timeout':
                                    send(c, ['10', '10', topic, 'phx_reply', {'status': 'error' if mode == 'reject' else 'ok', 'response': {}}])
                                if mode == 'positive':
                                    send(c, [None, None, topic, 'hot_marker', {'stage': 'post'}])
                                    send(c, ['6', None, PLAN[5], 'hot_marker', {'stage': 'continued'}])
                        drain_tail(c, buf, state)
                    else:
                        assert path in ('/api/config', '/me')
                        if path == '/api/config':
                            assert 'Authorization' not in headers
                            if mode == 'preflight' and len(state['paths']) == 1:
                                c.sendall(b'HTTP/1.1 503 Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n')
                                return
                        else:
                            assert headers['Authorization'] == 'Bearer ' + TOKEN
                        body = json.dumps(CONFIG if path == '/api/config' else ME).encode()
                        c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
                except BaseException as error:
                    state['errors'].append(type(error).__name__ + ': ' + str(error))
                finally:
                    if c is not None:
                        c.close()
                    raw.close()

            def accept_connections() -> None:
                try:
                    while not stop.is_set():
                        try:
                            raw, _ = listener.accept()
                        except socket.timeout:
                            continue
                        if len(state['workers']) >= 8:
                            raw.close()
                            raise AssertionError('connection cap')
                        thread = threading.Thread(target=worker, args=(raw,))
                        state['workers'].append(thread)
                        thread.start()
                except BaseException as error:
                    state['errors'].append(type(error).__name__ + ': ' + str(error))

            accept_thread = threading.Thread(target=accept_connections)
            accept_thread.start()
            yield state, f'https://127.0.0.1:{listener.getsockname()[1]}', gate
        finally:
            stop.set()
            end = time.monotonic() + 5
            if accept_thread is not None:
                accept_thread.join(max(0, end - time.monotonic()))
            listener.close()
            for worker_thread in state['workers']:
                worker_thread.join(max(0, end - time.monotonic()))
            assert accept_thread is None or not accept_thread.is_alive(), 'accept thread leak'
            assert all(not thread.is_alive() for thread in state['workers']), 'worker leak'
            assert not state['errors'], state['errors']

    def scenario(self, mode: str) -> None:
        with self.fixture(mode) as (state, origin, gate):
            p = subprocess.Popen(driver_command(origin, str(test_http.HTTPTests.certdir / 'ca.pem'), mode), stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True, env=dict(os.environ))
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
            self.assertEqual(out, b'PASS OWNED HOT SAME_EPOCH\n')
            self.assertNotIn(TOKEN.encode(), line + out + err)
        self.assertEqual(state['paths'], ['/api/config', '/me', '/socket/websocket?vsn=2.0.0&client_proto=4'])
        self.assertEqual(len(state['joins']), 10)
        self.assertEqual(state['upgrades'], 1)
        self.assertEqual(state['eof'], 1)
        receipt = os.environ.get('GIN_ACCOUNT_HOT_RECEIPT')
        if receipt:
            with open(receipt, 'a', encoding='utf-8') as stream:
                stream.write(json.dumps({'mode': mode, 'paths': state['paths'],
                    'joins': state['joins'], 'eof': state['eof'],
                    'upgrades': state['upgrades'], 'errors': state['errors'],
                    'client_barrier': line.decode().strip(),
                    'stdout': out.decode(), 'stderr': err.decode(),
                    'returncode': p.returncode}) + '\n')
        print('PASS owned ' + mode + ': config=1 me=1 upgrade=1 joins=10 strict_EOF=1 client_barrier=1')

    def test_same_socket(self) -> None:
        self.scenario('positive')

    def recovery(self, mode: str) -> None:
        with self.fixture(mode) as (state, origin, _):
            p = subprocess.run(driver_command(origin,
                str(test_http.HTTPTests.certdir / 'ca.pem'), mode),
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=12,
                env=dict(os.environ))
            self.assertEqual(p.returncode, 0, p.stderr.decode(errors='replace'))
            self.assertEqual(p.stdout, b'PASS OWNED START RECOVERY\n')
            self.assertRegex(p.stderr.decode(), r'^START_RECOVERY first=\d+ second=0\n$')
            self.assertNotIn(TOKEN.encode(), p.stdout + p.stderr)
        attempt = ['/api/config', '/me', '/socket/websocket?vsn=2.0.0&client_proto=4']
        self.assertEqual(state['paths'], (['/api/config'] if mode == 'preflight' else attempt) + attempt)
        self.assertEqual(state['joins'], [[str(i+1), str(i+1), topic, 'phx_join', {}]
            for i, topic in enumerate(PLAN[:9])])
        self.assertEqual(state['upgrades'], 1 if mode == 'preflight' else 2)
        self.assertEqual(state['eof'], 1)
        print('PASS owned start ' + mode + ': fresh_config_and_bound_me=1 ready=1 owned_sources=9 strict_EOF=1')

    def test_start_rename_upgrade_failure_retry(self) -> None:
        self.recovery('recovery')

    def test_start_rename_upgrade_failure_close_retry(self) -> None:
        self.recovery('recovery_close')

    def test_start_failure_before_me_retry(self) -> None:
        self.recovery('preflight')

    def test_join_rejected(self) -> None:
        self.scenario('reject')

    def test_join_timeout(self) -> None:
        self.scenario('timeout')

if __name__ == '__main__':
    unittest.main(verbosity=2)
