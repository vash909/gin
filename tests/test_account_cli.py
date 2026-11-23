#!/usr/bin/env python3
"""Actual production CLI, synthetic credentials, controlling PTY and strict TLS."""
import base64
import contextlib
import errno
import fcntl
import hashlib
import json
import os
import pty
import select
import signal
import socket
import stat
import subprocess
import sys
import tempfile
import termios
import threading
import time
import unittest
from pathlib import Path
import test_account_receive as wire
import test_account_history_bootstrap as support
from ws_fixture_frames import server_frame, take_client_frame
ROOT = Path(__file__).resolve().parents[1]
BOOT = {'networks': [{'id': 1, 'slug': 'one', 'nick': 'Own1'}, {'id': 2, 'slug': 'two', 'nick': 'Own2'}], 'channels': {'one': [{'name': '#aquiet', 'joined': True, 'source': 'joined'}, {'name': '#old1', 'joined': True, 'source': 'joined'}], 'two': [{'name': '#old2', 'joined': True, 'source': 'joined'}]}, 'heads': {}}
QUERIES = {'kind': 'query_windows_list', 'windows': {'1': [{'network_id': 1, 'target_nick': 'Peer', 'opened_at': 'opaque'}], '2': [{'network_id': 2, 'target_nick': 'Other', 'opened_at': 'opaque'}]}}
WS = '/socket/websocket?vsn=2.0.0&client_proto=36'
# Independently fixed admission budget and complete expected request sequence.
EXPECTED = [('GET', '/api/config'), ('POST', '/auth/login')] + [('GET', p) for p in ['/api/config', '/me', '/boot', '/api/config', '/me', WS, '/api/config', '/me', WS]]
for net, target in support.SCOPES:
    EXPECTED.extend([('GET', '/api/config'), ('GET', '/me'), ('GET', support.path(net, target))])

class CLI(wire.Replan):
    @contextlib.contextmanager
    def fixture(self):
        listener = socket.socket()
        listener.bind(('127.0.0.1', 0)); listener.listen(36); listener.settimeout(.05)
        ctx = self.tls(); stop = threading.Event(); late = threading.Event()
        state = {'records': [], 'joins': [], 'errors': [], 'workers': [], 'eof': [], 'late': late, 'active': 0, 'ws': 0,
                 'final': threading.Event(), 'release': threading.Event(),
                 'challenge': threading.Event(), 'controls': []}
        def worker(raw):
            c = None; attempt = 0
            try:
                raw.settimeout(4); c = ctx.wrap_socket(raw, server_side=True); c.settimeout(4)
                data = b''
                while b'\r\n\r\n' not in data:
                    block = c.recv(4096); assert block, 'header EOF'; data += block; assert len(data) < 16384
                head, body = data.split(b'\r\n\r\n', 1)
                lines = head.decode().split('\r\n'); method, path, _ = lines[0].split()
                headers = dict(x.split(': ', 1) for x in lines[1:])
                size = int(headers.get('Content-Length', '0'))
                while len(body) < size:
                    block = c.recv(4096); assert block; body += block
                assert len(body) == size
                state['records'].append((method, path))
                assert state['records'] == EXPECTED[:len(state['records'])], 'unexpected request ledger'
                if path == WS:
                    assert method == 'GET' and not body and 'Authorization' not in headers
                    assert headers['Sec-WebSocket-Protocol'] == 'phoenix,base64url.bearer.phx.' + base64.b64encode(wire.TOKEN.encode()).decode().rstrip('=')
                    state['ws'] += 1; attempt = state['ws']; state['active'] += 1
                    assert state['active'] == 1
                    if attempt == 2: assert state['eof'] == [1], 'discovery socket not closed'
                    accept = base64.b64encode(hashlib.sha1((headers['Sec-WebSocket-Key'] + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
                    c.sendall(b'HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Protocol: phoenix\r\nSec-WebSocket-Accept: ' + accept + b'\r\n\r\n')
                    buf = bytearray()
                    def send(frame):
                        c.sendall(server_frame(json.dumps(frame).encode()))
                    for i, topic in enumerate(support.TOPICS[:1] if attempt == 1 else support.TOPICS):
                        while True:
                            frame = take_client_frame(buf)
                            if frame is not None: break
                            block = c.recv(16384); assert block, 'join EOF'; buf.extend(block)
                        assert frame[0] == 1 and frame[1]
                        jr, ref, got, event, value = json.loads(frame[2])
                        assert jr == ref and got == topic and event == 'phx_join' and value == {}
                        state['joins'].append(got)
                        reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': 40} if i == 0 else {}}]
                        if attempt == 1:
                            c.sendall(server_frame(json.dumps(reply).encode()) + server_frame(json.dumps([jr, None, topic, 'event', QUERIES]).encode()))
                        else: send(reply)
                    if attempt == 2:
                        assert state['challenge'].wait(12), 'checkpoint challenge absent'
                        # Sent only after the independent parent pins the last
                        # bootstrap checkpoint. Native has no service call
                        # between that append and the owner's steady loop.
                        send([None, None, support.TOPICS[6], 'event', {'kind': 'message', 'message': support.row('one', '#old1', 35)}])
                        state['controls'].append(('fresh-live', 35))
                        assert late.wait(4), 'client marker receipt absent'
                        # Teardown sets stop only after reaping the client.
                        # Do not emit a later record to that cancelled child.
                        if not stop.is_set():
                            send([None, None, support.TOPICS[6], 'event', {'kind': 'message', 'message': support.row('one', '#old1', 40)}])
                            state['controls'].append(('late-live', 40))
                    while True:
                        frame = take_client_frame(buf)
                        if frame is None:
                            block = c.recv(16384)
                            if not block:
                                assert not buf, 'partial frame at EOF'
                                state['eof'].append(attempt); break
                            buf.extend(block); continue
                        opcode, final, payload = frame
                        assert final
                        if opcode == 1:
                            jr, ref, topic, event, value = json.loads(payload)
                            assert jr is None and topic == 'phoenix' and event == 'heartbeat' and value == {}
                            send([None, ref, topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                        else: assert opcode == 8, 'unexpected control/operation'
                else:
                    if path == '/auth/login':
                        assert method == 'POST' and 'Authorization' not in headers
                        assert json.loads(body) == {'identifier': 'SyntheticUser', 'password': 'HiddenSentinel'}
                        value = {'token': wire.TOKEN, 'subject': wire.ME}
                    else:
                        assert method == 'GET' and not body
                        if path == '/api/config':
                            assert 'Authorization' not in headers
                            value = {'server': 'grappa', 'protocol_version': 40, 'min_protocol_version': 3}
                        else:
                            assert headers['Authorization'] == 'Bearer ' + wire.TOKEN
                            if path == '/me': value = wire.ME
                            elif path == '/boot': value = BOOT
                            else:
                                matches = [(n, t) for n, t in support.SCOPES if support.path(n, t) == path]
                                assert len(matches) == 1
                                net, target = matches[0]
                                value = [] if target == '#aquiet' else [support.row(net, target, 10)]
                                if (net, target) == ('two', 'Other'):
                                    state['final'].set()
                                    assert state['release'].wait(8), 'final REST gate absent'
                    payload = json.dumps(value).encode()
                    c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: ' + str(len(payload)).encode() + b'\r\n\r\n' + payload)
            except BaseException as exc:
                state['errors'].append(repr(exc))
            finally:
                if c is not None: c.close()
                raw.close()
                if attempt: state['active'] -= 1
        def accept_loop():
            try:
                while not stop.is_set():
                    try: raw, _ = listener.accept()
                    except socket.timeout: continue
                    if len(state['workers']) >= 36:
                        raw.close(); raise AssertionError('request admission cap')
                    thread = threading.Thread(target=worker, args=(raw,)); state['workers'].append(thread); thread.start()
            except BaseException as exc: state['errors'].append(repr(exc))
        thread = threading.Thread(target=accept_loop); thread.start()
        try: yield state, f'https://127.0.0.1:{listener.getsockname()[1]}', late
        finally:
            stop.set(); late.set(); state['release'].set(); state['challenge'].set(); thread.join(2); listener.close(); assert not thread.is_alive()
            end = time.monotonic() + 5
            for worker_thread in state['workers']:
                worker_thread.join(max(0, end - time.monotonic())); assert not worker_thread.is_alive(), 'worker leak'
            assert not state['errors'], state['errors']

    def test_prompt_cancellation(self):
        binary = os.environ.get('GIN_ACCOUNT_CLI_BINARY', str(ROOT / '.build/gin'))
        for number in (signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT,
                       signal.SIGTSTP, signal.SIGTTIN, signal.SIGTTOU):
            with self.subTest(signal=number), tempfile.TemporaryDirectory(prefix='gin-cancel-', dir=os.environ.get('TMPDIR') or str(ROOT / '.build')) as tmp:
                master, slave = pty.openpty(); before = termios.tcgetattr(slave)
                proc = None; transcript = b''; active = False
                try:
                    with self.fixture() as (state, origin, _):
                        try:
                            wrapper = 'import os,sys,fcntl,termios; fcntl.ioctl(int(sys.argv[1]),termios.TIOCSCTTY,0); os.execv(sys.argv[2],sys.argv[2:])'
                            proc = subprocess.Popen([sys.executable, '-B', '-c', wrapper, str(slave), binary, origin, 'SyntheticUser', '-d', str(Path(tmp) / 'irc'), '-a', str(self.certdir / 'ca.pem')], cwd=tmp, stdin=subprocess.DEVNULL, stdout=slave, stderr=slave, pass_fds=(slave,), start_new_session=True)
                            end = time.monotonic() + 5
                            while time.monotonic() < end and proc.poll() is None:
                                if select.select([master], [], [], .02)[0]: transcript += os.read(master, 16384)
                                if b'Password: ' in transcript and not termios.tcgetattr(slave)[3] & termios.ECHO:
                                    active = True
                                    os.write(master, b'PartialPrivateSentinel')
                                    os.kill(proc.pid, number)
                                    break
                            proc.wait(timeout=3)
                        finally:
                            if proc is not None:
                                if proc.poll() is None: os.killpg(proc.pid, signal.SIGKILL)
                                proc.wait(timeout=2)
                        while select.select([master], [], [], .01)[0]: transcript += os.read(master, 16384)
                        self.assertTrue(active, 'no active hidden password prompt')
                        self.assertEqual(termios.tcgetattr(slave), before)
                        self.assertEqual(state['records'], [('GET', '/api/config')])
                        self.assertEqual(state['joins'], [])
                        self.assertEqual(list(Path(tmp).iterdir()), [], 'cancel created local state')
                        for secret in (b'PartialPrivateSentinel', b'SyntheticUser', wire.TOKEN.encode()):
                            self.assertNotIn(secret, transcript)
                        assert proc is not None
                        self.assertEqual(proc.returncode, 0, 'hidden prompt cancellation must be orderly')
                        print('PASS real hidden prompt cancellation signal=' + str(number) + ' exit=0 GET=config-only no-root no-auth termios-restored')
                finally:
                    os.close(master); os.close(slave)

    def pinned_bytes(self, path):
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        try:
            before = os.fstat(fd)
            self.assertTrue(stat.S_ISREG(before.st_mode))
            self.assertEqual((before.st_uid, stat.S_IMODE(before.st_mode), before.st_nlink), (os.geteuid(), 0o600, 1))
            self.assertLessEqual(before.st_size, 8192)
            data = os.read(fd, 8193); after = os.fstat(fd); current = path.lstat()
            self.assertEqual((before.st_dev, before.st_ino, before.st_mode, before.st_uid, before.st_nlink, before.st_size, before.st_mtime_ns, before.st_ctime_ns),
                             (after.st_dev, after.st_ino, after.st_mode, after.st_uid, after.st_nlink, after.st_size, after.st_mtime_ns, after.st_ctime_ns))
            self.assertTrue(stat.S_ISREG(current.st_mode))
            self.assertEqual((current.st_dev, current.st_ino, current.st_nlink, current.st_size, current.st_mode, current.st_uid),
                             (after.st_dev, after.st_ino, 1, after.st_size, after.st_mode, after.st_uid))
            self.assertEqual(len(data), after.st_size)
            return data, after
        finally: os.close(fd)

    def checkpoint_barrier(self, root, expected, origin):
        paths = {str(Path(p).parent / '.gin.checkpoint') for p, data in expected.items() if data}
        actual = {str(p.relative_to(root)) for p in root.rglob('.gin.checkpoint')}
        self.assertTrue(actual <= paths, 'unexpected checkpoint')
        if actual != paths: return False
        for path, data in expected.items():
            got, log = self.pinned_bytes(root / path)
            if got != data: return False
            if not data: continue
            record, _ = self.pinned_bytes(root / Path(path).parent / '.gin.checkpoint')
            self.assertEqual(record[:8], b'GINCP\0\0\1')
            self.assertEqual(tuple(int.from_bytes(record[i:i+8], 'big') for i in (8,16,24,32,40)), (0,1,log.st_dev,log.st_ino,log.st_size))
            self.assertIn(origin.encode(), record); self.assertIn(wire.ME['id'].encode(), record)
        return True

    def lock_available(self, root):
        code = '''import os,sys,fcntl,errno
fd=os.open(sys.argv[1],os.O_RDWR|os.O_NOFOLLOW|os.O_NONBLOCK)
try:
 try: fcntl.lockf(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
 except OSError as e:
  if e.errno not in (errno.EACCES,errno.EAGAIN): raise
  sys.exit(3)
finally: os.close(fd)
'''
        probe = subprocess.run([sys.executable, '-B', '-c', code, str(root / '.gin.lock')], capture_output=True, timeout=2)
        self.assertIn(probe.returncode, (0, 3), probe.stderr)
        return probe.returncode == 0

    def test_full_stderr(self):
        self.run_receive(True)

    def test_receive(self):
        self.run_receive(False)

    def test_default_root(self):
        self.run_receive(False, default_root=True)

    def test_control_errors(self):
        binary = os.environ.get('GIN_ACCOUNT_CLI_CONTROL_DRIVER', str(ROOT / '.build/account_cli_control_driver'))
        for mode in ('restore-owner', 'restore-interrupted', 'interrupted'):
            with self.subTest(mode=mode):
                result = subprocess.run([binary, mode], capture_output=True, timeout=3)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b'PASS private typed-result mapping and signal restoration\n')

    def test_invalid_grammar(self):
        binary = os.environ.get('GIN_ACCOUNT_CLI_BINARY', str(ROOT / '.build/gin'))
        with self.fixture() as (state, origin, _):
            cases = [[], ['unknown-command'], [origin],
                     ['http://127.0.0.1', 'user'], ['https://user@127.0.0.1', 'user'],
                     [origin + '/path', 'user'], [origin + '?query', 'user'],
                     [origin, ''], [origin, 'bad\nuser'], [origin, '-user'],
                     [origin, 'user', '-d'], [origin, 'user', '-a'],
                     [origin, 'user', '-d', 'a', '-d', 'b'],
                     [origin, 'user', '-a', 'a', '-a', 'b'],
                     [origin, 'user', '-z', 'x'], [origin, 'user', '-d', '']]
            for args in cases:
                with self.subTest(args=args), tempfile.TemporaryDirectory(prefix='gin-grammar-', dir=os.environ.get('TMPDIR') or str(ROOT / '.build')) as tmp:
                    result = subprocess.run([binary, *args], cwd=tmp, stdin=subprocess.DEVNULL, capture_output=True, timeout=3)
                    self.assertEqual(result.returncode, 2, result.stderr)
                    self.assertNotIn(b'Password:', result.stdout + result.stderr)
                    self.assertEqual(list(Path(tmp).iterdir()), [])
            self.assertEqual(state['records'], [])
            self.assertEqual(state['joins'], [])
            print('PASS invalid grammar cases=' + str(len(cases)) + ' no-prompt no-files no-HTTP')

    def run_receive(self, backpressure, default_root=False):
        binary = os.environ.get('GIN_ACCOUNT_CLI_BINARY', str(ROOT / '.build/gin'))
        with tempfile.TemporaryDirectory(prefix='gin-cli-', dir=os.environ.get('TMPDIR') or str(ROOT / '.build')) as tmp:
            root = Path(tmp) / 'irc'; master, slave = pty.openpty(); before = termios.tcgetattr(slave)
            proc = None; transcript = b''; sent = False
            read_fd, write_fd = os.pipe()
            original_flags = fcntl.fcntl(write_fd, fcntl.F_GETFL)
            saturated = False; challenged = False; challenge_end = 0.0
            def line(target, ident):
                return ('[1970-01-01T00:00:00.123Z] ' + ('notice' if target == '$server' else 'privmsg') + ' sender="sender" body="row' + str(ident) + '"\n').encode()
            expected = {n + '/' + ('' if t == '$server' else ('@' + t.lower() if t in ('Peer', 'Other') else t) + '/') + 'out': b'' if t == '#aquiet' else line(t, 10) for n, t in support.SCOPES}
            try:
                with self.fixture() as (state, origin, _):
                    wrapper = 'import os,sys,fcntl,termios; fcntl.ioctl(int(sys.argv[1]),termios.TIOCSCTTY,0); os.execv(sys.argv[2],sys.argv[2:])'
                    root_args = [] if default_root else ['-d', str(root)]
                    proc = subprocess.Popen([sys.executable, '-B', '-c', wrapper, str(slave), binary, origin, 'SyntheticUser', *root_args, '-a', str(self.certdir / 'ca.pem')], cwd=tmp, stdin=subprocess.DEVNULL, stdout=slave, stderr=write_fd if backpressure else slave, pass_fds=(slave,), start_new_session=True)
                    end = time.monotonic() + 12; completed = False; live = False
                    while time.monotonic() < end:
                        if select.select([master], [], [], .02)[0]: transcript += os.read(master, 16384)
                        if backpressure and not saturated and select.select([read_fd], [], [], 0)[0]: transcript += os.read(read_fd, 16384)
                        if b'Password: ' in transcript and not sent:
                            self.assertIsNone(proc.poll(), 'password prompt no longer active')
                            self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
                            os.write(master, b'HiddenSentinel\n'); sent = True
                        if state['final'].is_set() and not state['release'].is_set():
                            if backpressure:
                                self.assertIn(b'plaintext receive tracer', transcript)
                                fcntl.fcntl(write_fd, fcntl.F_SETFL, original_flags | os.O_NONBLOCK)
                                try:
                                    while True:
                                        try: os.write(write_fd, b'X' * 4096)
                                        except BlockingIOError: break
                                    while True:
                                        try: os.write(write_fd, b'X')
                                        except BlockingIOError: break
                                finally: fcntl.fcntl(write_fd, fcntl.F_SETFL, original_flags)
                                self.assertEqual(fcntl.fcntl(write_fd, fcntl.F_GETFL), original_flags)
                                saturated = True
                            state['release'].set()
                        if not challenged and self.checkpoint_barrier(root, expected, origin):
                            self.assertFalse(self.lock_available(root), 'owner lost root lease')
                            print('OBSERVED six safe checkpoints, seven exact outputs, root held; fresh LIVE35 armed', flush=True)
                            challenged = True; challenge_end = time.monotonic() + 1.5
                            state['challenge'].set()
                        if challenged and not completed:
                            if (root / 'one/#old1/out').read_bytes() == expected['one/#old1/out'] + line('#old1', 35):
                                completed = True; state['late'].set()
                            elif time.monotonic() >= challenge_end:
                                self.assertFalse(self.lock_available(root), 'root unexpectedly released')
                                print(('BACKPRESSURE RED' if backpressure else 'HEALTHY CONTROL FAILED') + ': root held, fresh LIVE35 not received', flush=True)
                                os.killpg(proc.pid, signal.SIGKILL); proc.wait(timeout=2)
                                self.fail('post-checkpoint Native service blocked: no fresh LIVE35 append while root held')
                        if completed and (root / 'one/#old1/out').read_bytes() == expected['one/#old1/out'] + line('#old1', 35) + line('#old1', 40):
                            live = True; os.kill(proc.pid, signal.SIGTERM); break
                        if proc.poll() is not None: break
                    if proc.poll() is None and not live: self.fail('outer deadline before complete receive tracer')
                    proc.wait(timeout=3)
                    self.assertTrue(self.lock_available(root), 'root lease retained after stop before stderr drain')
                    self.assertEqual(fcntl.fcntl(write_fd, fcntl.F_GETFL), original_flags)
                    if backpressure:
                        self.assertTrue(saturated)
                        while select.select([read_fd], [], [], 0)[0]: transcript += os.read(read_fd, 16384)
                    while select.select([master], [], [], .01)[0]: transcript += os.read(master, 16384)
                    self.assertTrue(sent, f'missing hidden prompt; actual production CLI exit={proc.returncode}')
                    self.assertEqual(proc.returncode, 0, 'production owner failed')
                    self.assertTrue(completed and live)
                self.assertEqual(state['records'], EXPECTED)
                self.assertEqual(state['eof'], [1, 2]); self.assertEqual(state['active'], 0)
                self.assertEqual(state['joins'], support.TOPICS[:1] + support.TOPICS)
                self.assertEqual(state['controls'], [('fresh-live', 35), ('late-live', 40)])
                self.assertEqual(termios.tcgetattr(slave), before)
                for secret in (b'HiddenSentinel', wire.TOKEN.encode(), b'SyntheticUser', b'row10', b'row35', b'row40', b'sender'):
                    self.assertNotIn(secret, transcript)
                self.assertFalse((root / '.gin.credentials').exists())
                expected['one/#old1/out'] += line('#old1', 35) + line('#old1', 40)
                self.assertEqual({str(p.relative_to(root)): p.read_bytes() for p in root.rglob('out')}, expected)
                self.assertTrue(self.checkpoint_barrier(root, expected, origin))
                for p in root.rglob('*'):
                    st = p.lstat(); self.assertEqual(stat.S_IMODE(st.st_mode), 0o700 if stat.S_ISDIR(st.st_mode) else 0o600)
                    if stat.S_ISREG(st.st_mode):
                        self.assertNotIn(wire.TOKEN.encode(), p.read_bytes()); self.assertNotIn(b'HiddenSentinel', p.read_bytes())
                print('PASS actual CLI PTY TLS loginPOST=1 GET=31 WSS=2 joins=1+12 destinations=7 lateLIVE=40 orderlySIGTERM=0 strict-ledger-clean')
            finally:
                if proc is not None:
                    if proc.poll() is None: os.killpg(proc.pid, signal.SIGKILL)
                    proc.wait(timeout=2)
                os.close(master); os.close(slave); os.close(read_fd); os.close(write_fd)

if __name__ == '__main__': unittest.main(verbosity=2)
