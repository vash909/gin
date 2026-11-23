#!/usr/bin/env python3
"""Known-open channel/query SEND tracer with held HTTP, client append and completion barriers.

The original genuine RED test is retained outside this candidate. Private
observer runs qualify typed outcomes separately from the ordinary executable.
"""
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
import array
from pathlib import Path
from typing import ClassVar
import test_http
import test_account_cli as legacy
import test_account_receive as wire
import test_account_history_bootstrap as support
from ws_fixture_frames import server_frame, take_client_frame
ROOT = Path(__file__).resolve().parents[1]
def cancelled_post_eof(stream, cancelled):
    """Observe closure only after the parent reaped the cancelled client."""
    assert cancelled, 'POST client was not independently cancelled and reaped'
    try:
        data = stream.recv(1)
    except BrokenPipeError:
        # Reproduced OpenSSL read-side result after this held POST's client
        # exited. Do not exempt resets, timeouts or any unmarked request.
        return
    assert data == b'', 'cancelled POST connection retained'

BOOT = {'networks': [{'id': 1, 'slug': 'one', 'nick': 'Own1'}, {'id': 2, 'slug': 'two', 'nick': 'Own2'}], 'channels': {'one': [{'name': '#aquiet', 'joined': True, 'source': 'joined'}, {'name': '#old1', 'joined': True, 'source': 'joined'}], 'two': [{'name': '#old2', 'joined': True, 'source': 'joined'}]}, 'heads': {}}
QUERIES = {'kind': 'query_windows_list', 'windows': {'1': [{'network_id': 1, 'target_nick': 'Peer', 'opened_at': 'opaque'}], '2': [{'network_id': 2, 'target_nick': 'Other', 'opened_at': 'opaque'}]}}
WS = '/socket/websocket?vsn=2.0.0&client_proto=36'
# Independently fixed admission budget and complete expected request sequence.
EXPECTED = [('GET', '/api/config'), ('POST', '/auth/login')] + [('GET', p) for p in ['/api/config', '/me', '/boot', '/api/config', '/me', WS, '/api/config', '/me', WS]]
for net, target in support.SCOPES:
    EXPECTED.extend([('GET', '/api/config'), ('GET', '/me'), ('GET', support.path(net, target))])


class PTYTranscript:
    """One bounded reader, kept active until the producer has been reaped."""
    LIMIT = 262144

    def __init__(self, fd):
        self.fd = fd
        self.data = bytearray()
        self.stop = threading.Event()
        self.errors = []
        self.overflow = False
        self.thread = threading.Thread(target=self.drain)
        self.thread.start()

    def drain(self):
        try:
            while True:
                ready = select.select([self.fd], [], [], .02)[0]
                if ready:
                    block = os.read(self.fd, 16384)
                    if not block:
                        return
                    remaining = self.LIMIT - len(self.data)
                    self.data.extend(block[:remaining])
                    self.overflow |= len(block) > remaining
                elif self.stop.is_set():
                    return
        except BaseException as exc:
            self.errors.append(exc)

    def finish(self):
        self.stop.set()
        self.thread.join(2)
        assert not self.thread.is_alive(), 'PTY collector did not stop'
        assert not self.errors, self.errors
        assert not self.overflow, 'PTY transcript exceeded bounded collection cap'

def fixture_fault_probe(step):
    """Run only in a deadline-bounded, separately reaped process."""
    import unittest.mock as mock
    def inventory():
        result = {}
        for entry in os.listdir('/dev/fd'):
            fd = int(entry)
            try:
                s = os.fstat(fd)
            except OSError as exc:
                if exc.errno != errno.EBADF:
                    raise
                # listdir's own descriptor has closed before this inventory.
                continue
            result[fd] = (s.st_dev, s.st_ino, s.st_mode, s.st_rdev)
        return result
    case = FIFO('test_channel')
    failure = RuntimeError('injected-' + step)
    original = fcntl.fcntl
    calls = 0
    def inject(*args):
        nonlocal calls
        calls += 1
        if calls == int(step):
            raise failure
        return original(*args)
    before = inventory()
    try:
        if step == 'tls':
            with mock.patch.object(case, 'tls', side_effect=failure):
                with case.fixture():
                    raise AssertionError('unreachable')
        else:
            target = mock.patch.object(os, 'pipe', side_effect=failure) if step == 'pipe' else mock.patch.object(fcntl, 'fcntl', side_effect=inject)
            with target:
                case.run_channel(False)
    except RuntimeError as exc:
        assert exc is failure, 'original exception replaced'
    else:
        raise AssertionError('injection not reached')
    after = inventory()
    print('FAULT', step, 'original-exception-preserved', before, after, flush=True)
    assert after == before, 'exact FD inventory not restored'


class FIFOFixture(unittest.TestCase):
    certdir: ClassVar[Path]
    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)
    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)
    tls = test_http.HTTPTests.tls
    pinned_bytes = legacy.CLI.pinned_bytes
    checkpoint_barrier = legacy.CLI.checkpoint_barrier
    lock_available = legacy.CLI.lock_available
    @contextlib.contextmanager
    def fixture(self, query=False, network_join=False, network_part=False, network_query=False):
        assert not network_query or not (query or network_join or network_part)
        assert not (network_part and (network_join or query))
        network_control = network_join or network_part
        mutation_method = "DELETE" if network_part else "POST"
        message_path = "/networks/one/channels/Peer/messages" if query else "/networks/one/channels/%23old1/messages"
        if network_join:
            message_path = "/networks/one/channels"
        if network_part:
            message_path = "/networks/one/channels/%23old1"
        with contextlib.closing(socket.socket()) as listener:
            listener.bind(('127.0.0.1', 0)); listener.listen(36); listener.settimeout(.05)
            ctx = self.tls(); stop = threading.Event(); late = threading.Event()
            state = {'records': [], 'joins': [], 'errors': [], 'workers': [], 'eof': [], 'late': late, 'active': 0, 'ws': 0,
                     'final': threading.Event(), 'release': threading.Event(),
                     'challenge': threading.Event(), 'controls': [], 'posted': threading.Event(), 'post': None,
                     'post_release': threading.Event(), 'held_marker': threading.Event(), 'cancelled': False,
                     'query_sent': threading.Event(), 'query_frames': [], 'root_join': None, 'join_refs': []}
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
                    ledger = EXPECTED if network_query else EXPECTED + [('GET', '/api/config'), ('GET', '/me'), (mutation_method, message_path)]
                    assert state['records'] == ledger[:len(state['records'])], 'unexpected request ledger'
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
                            if attempt == 2:
                                state['join_refs'].append(ref)
                                if i == 0: state['root_join'] = (jr, topic)
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
                            if network_query:
                                pass  # Read the actual operation below, without waiting on its reader.
                            elif state['posted'].wait(1.2 if query or network_control else 4):
                                send([None, None, support.TOPICS[6], 'event', {'kind': 'message', 'message': support.row('one', '#old1', 40)}])
                                state['held_marker'].set()
                            else:
                                assert query or network_control, 'POST missing after healthy input'

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
                                if network_query and event == 'open_query_window':
                                    assert attempt == 2 and (jr, topic) == state['root_join']
                                    assert isinstance(ref, str) and ref and ref not in state['join_refs']
                                    assert value == {'network_id': 1, 'target_nick': 'NewPeer'}
                                    assert type(value['network_id']) is int
                                    assert not state['query_frames'], 'duplicate query operation'
                                    state['query_frames'].append([jr, ref, topic, event, value])
                                    state['query_sent'].set()
                                    send([None, None, support.TOPICS[6], 'event', {'kind': 'message', 'message': support.row('one', '#old1', 40)}])
                                    state['held_marker'].set()
                                    assert state['post_release'].wait(4), 'query ACK gate absent'
                                    if not state['cancelled']:
                                        send([jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                                    continue
                                assert jr is None and topic == 'phoenix' and event == 'heartbeat' and value == {}
                                send([None, ref, topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                            else: assert opcode == 8, 'unexpected control/operation'
                    else:
                        if method == mutation_method and path == message_path:
                            assert path == message_path
                            assert headers['Authorization'] == 'Bearer ' + wire.TOKEN
                            if network_part:
                                assert body == b'', 'PART must have no body/reason/params'
                            else:
                                assert json.loads(body) == ({'name': '#NewRoom'} if network_join else {'body': 'FIFO tracer literal'})
                            state['post'] = (path, body)
                            state['posted'].set()
                            assert state['post_release'].wait(4), 'POST response not independently released'
                            if state['cancelled']:
                                cancelled_post_eof(c, state['cancelled'])
                                return
                            value = {'ok': True}
                        elif path == '/auth/login':
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
                        c.sendall((b'HTTP/1.1 202 Accepted' if method == mutation_method and path == message_path else b'HTTP/1.1 200 OK') + b'\r\nConnection: close\r\nContent-Length: ' + str(len(payload)).encode() + b'\r\n\r\n' + payload)
                except BaseException:
                    import traceback
                    state['errors'].append(traceback.format_exc())
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
                stop.set(); late.set(); state['release'].set(); state['challenge'].set(); state['post_release'].set(); thread.join(2); listener.close(); assert not thread.is_alive()
                end = time.monotonic() + 5
                for worker_thread in state['workers']:
                    worker_thread.join(max(0, end - time.monotonic())); assert not worker_thread.is_alive(), 'worker leak'
                assert not state['errors'], state['errors']


    def run_guard(self, code):
        result = subprocess.run([sys.executable, '-B', '-c', code],
                                cwd=ROOT / 'tests', env=dict(os.environ, PYTHONDONTWRITEBYTECODE='1'),
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10,
                                check=False)
        self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace'))

    def run_channel(self, cancel, query=False, network_join=False, network_part=False, network_query=False):
        assert not network_query or not (query or network_join or network_part)
        assert not (network_part and (network_join or query))
        network_control = network_join or network_part
        mutation_method = "DELETE" if network_part else "POST"
        message_path = "/networks/one/channels/Peer/messages" if query else "/networks/one/channels/%23old1/messages"
        if network_join:
            message_path = "/networks/one/channels"
        if network_part:
            message_path = "/networks/one/channels/%23old1"
        input_path = "one/in" if network_control or network_query else "one/@peer/in" if query else "one/#old1/in"
        binary = os.environ.get('GIN_ACCOUNT_CLI_BINARY', str(ROOT / '.build/gin'))
        with tempfile.TemporaryDirectory(prefix='gin-fifo-send-', dir=os.environ.get('TMPDIR') or str(ROOT / '.build')) as tmp, contextlib.ExitStack() as owned:
            root = Path(tmp) / 'irc'
            master, slave = pty.openpty()
            owned.callback(os.close, master); owned.callback(os.close, slave)
            before = termios.tcgetattr(slave)
            proc = None; prompted = False; healthy = False; posted = False
            receipt_r, receipt_w = os.pipe()
            owned.callback(os.close, receipt_r); owned.callback(os.close, receipt_w)
            terminal = []
            for receipt_fd in (receipt_r, receipt_w):
                fcntl.fcntl(receipt_fd, fcntl.F_SETFL,
                            fcntl.fcntl(receipt_fd, fcntl.F_GETFL) | os.O_NONBLOCK)
            collector = PTYTranscript(master)
            owned.callback(collector.finish)
            transcript = collector.data
            def line(target, ident):
                return ('[1970-01-01T00:00:00.123Z] ' + ('notice' if target == '$server' else 'privmsg') + ' sender="sender" body="row' + str(ident) + '"\n').encode()
            expected = {n + '/' + ('' if t == '$server' else ('@' + t.lower() if t in ('Peer', 'Other') else t) + '/') + 'out': b'' if t == '#aquiet' else line(t, 10) for n, t in support.SCOPES}
            try:
                with self.fixture(query=query, network_join=network_join, network_part=network_part, network_query=network_query) as (state, origin, late):
                    try:
                        wrapper = 'import os,sys,fcntl,termios; fcntl.ioctl(int(sys.argv[1]),termios.TIOCSCTTY,0); os.execv(sys.argv[2],sys.argv[2:])'
                        env = dict(os.environ, GIN_FIFO_STATUS_FD=str(receipt_w))
                        proc = subprocess.Popen([sys.executable, '-B', '-c', wrapper, str(slave), binary, origin, 'SyntheticUser', '-d', str(root), '-a', str(self.certdir / 'ca.pem')], cwd=tmp, stdin=subprocess.DEVNULL, stdout=slave, stderr=slave, pass_fds=(slave,receipt_w), env=env, start_new_session=True)
                        deadline = time.monotonic() + 12
                        challenged = False
                        while time.monotonic() < deadline and proc.poll() is None:
                            time.sleep(.02)
                            if b'Password: ' in transcript and not prompted:
                                self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
                                os.write(master, b'HiddenSentinel\n'); prompted = True
                            state['release'].set()
                            if not challenged and self.checkpoint_barrier(root, expected, origin):
                                self.assertFalse(self.lock_available(root))
                                challenged = True; state['challenge'].set()
                            if challenged and (root / 'one/#old1/out').read_bytes() == expected['one/#old1/out'] + line('#old1', 35):
                                healthy = True; break
                        self.assertTrue(healthy, 'setup failed: no client LIVE35 after initial checkpoint binding')
                        print('HEALTHY hidden-login discovery bootstrap CP-bound LIVE35 independent-root-held', flush=True)
                        retained = {}
                        if network_part or network_query:
                            for name in ('one/#old1', 'one/#old1/in', 'one/#old1/out'):
                                st = (root / name).stat()
                                retained[name] = (st.st_dev, st.st_ino, stat.S_IFMT(st.st_mode))
                        late.set()
                        fd = os.open(root / input_path, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
                        try:
                            record = b'/query NewPeer\n' if network_query else b'/part #old1\n' if network_part else b'/join #NewRoom\n' if network_join else b'FIFO tracer literal\n'
                            self.assertLessEqual(len(record), os.fpathconf(fd, 'PC_PIPE_BUF'))
                            self.assertEqual(os.write(fd, record), len(record))
                        finally: os.close(fd)
                        if network_join:
                            print('INPUT network=one path=one/in atomic-record=/join #NewRoom LF', flush=True)
                        if network_part:
                            print('INPUT network=one path=one/in atomic-record=/part #old1 LF', flush=True)
                        if network_query:
                            print('INPUT network=one numeric-id=1 path=one/in atomic-record=/query NewPeer LF', flush=True)
                        posted = (state['query_sent'] if network_query else state['posted']).wait(1.0)
                        if not query and not network_control and not network_query:
                            self.assertTrue(posted, 'semantic RED: healthy CLI never POSTed window FIFO SEND')
                        if posted:
                            deadline = time.monotonic() + 2
                            while time.monotonic() < deadline:
                                if (root / 'one/#old1/out').read_bytes() == expected['one/#old1/out'] + line('#old1', 35) + line('#old1', 40): break
                                time.sleep(.01)
                            self.assertEqual((root / 'one/#old1/out').read_bytes(), expected['one/#old1/out'] + line('#old1', 35) + line('#old1', 40), 'client failed WSS append while mutation held')
                            self.assertFalse(self.lock_available(root))
                            if network_query:
                                self.assertEqual(state['records'], EXPECTED)
                                self.assertFalse((root / 'one/@newpeer').exists())
                            else:
                                self.assertEqual(sum(m == mutation_method and p == message_path for m,p in state['records']),1)
                            if not cancel:
                                # Insert only after POST handler entry: no earlier parser
                                # read can have prefetched this fresh rejected sentinel.
                                fd = os.open(root / input_path, os.O_RDWR | os.O_NONBLOCK | os.O_NOFOLLOW)
                                try:
                                    os.write(fd,b'/deferred\n')
                                    pending = array.array('i',[0]); fcntl.ioctl(fd,termios.FIONREAD,pending,True)
                                    self.assertGreater(pending[0],0,'sentinel consumed before POST completion')
                                    if network_query:
                                        until = time.monotonic() + .2
                                        while time.monotonic() < until:
                                            self.assertIsNone(proc.poll())
                                            fcntl.ioctl(fd,termios.FIONREAD,pending,True)
                                            self.assertGreater(pending[0],0,'premature FIFO consumption before query ACK')
                                            time.sleep(.01)
                                    state['post_release'].set()
                                    deadline = time.monotonic()+2
                                    while time.monotonic()<deadline:
                                        fcntl.ioctl(fd,termios.FIONREAD,pending,True)
                                        if pending[0]==0: break
                                        time.sleep(.01)
                                    self.assertEqual(pending[0],0,'no client post-send parser completion barrier')
                                finally: os.close(fd)
                                if os.environ.get('GIN_FIFO_OBSERVER') and not network_control and not network_query:
                                    self.assertTrue(select.select([receipt_r],[],[],1)[0])
                                    self.assertEqual(os.read(receipt_r,4096),b'T 1 0 2 202 1\n')
                                elif os.environ.get('GIN_FIFO_OBSERVER') and not network_query:
                                    self.assertTrue(select.select([receipt_r],[],[],1)[0])
                                    returned = os.read(receipt_r,4096)
                                    self.assertEqual(returned,(b'PT' if network_part else b'JT') + b' 1 0 1 202 1\n')
                                    print('TYPED returned-control=' + returned.decode(),flush=True)
                            print(('PASS held-query-ACK' if network_query else 'PASS held-DELETE' if network_part else 'PASS held-POST') + ' client-LIVE40 root-held no-echo no-replay completion=' + ('cancel' if cancel else 'post-operation-parser'),flush=True)
                        self.assertIsNone(proc.poll(), 'owner terminated instead of retaining healthy FIFO service')
                        self.assertFalse(self.lock_available(root), 'FIFO input released the root lease')
                    finally:
                        if proc is not None:
                            if proc.poll() is None: os.kill(proc.pid, signal.SIGTERM)
                            try: proc.wait(timeout=3)
                            except subprocess.TimeoutExpired:
                                os.killpg(proc.pid, signal.SIGKILL); proc.wait(timeout=2); raise
                            final=b''
                            if cancel and not network_query:
                                self.assertTrue(select.select([receipt_r],[],[],1)[0])
                                receipt = os.read(receipt_r,4096)
                                terminal=receipt.splitlines()[0].split()
                                if network_control:
                                    print('TYPED returned-control=' + receipt.splitlines()[0].decode(),flush=True)
                                self.assertEqual(terminal[0:2],[b'PT' if network_part else b'JT' if network_join else b'T',b'1'])
                                self.assertEqual(terminal[3],b'3' if network_control else b'4')
                                self.assertEqual(terminal[5],b'1')
                                final=b'\n'.join(receipt.splitlines()[1:])
                                state['cancelled']=True
                            if os.environ.get('GIN_FIFO_OBSERVER') and network_query:
                                deadline=time.monotonic()+1
                                while time.monotonic()<deadline and b'QF ' not in final:
                                    try: final+=os.read(receipt_r,4096)
                                    except BlockingIOError: time.sleep(.01)
                                rows=final.splitlines()
                                taken=[r.split() for r in rows if r.startswith(b'QT ')]
                                stored=[r.split() for r in rows if r.startswith(b'QF ')]
                                self.assertEqual(len(taken),1)
                                self.assertEqual(len(stored),1)
                                self.assertEqual(taken[0][1],b'1')
                                self.assertEqual(taken[0][3],b'5' if cancel else b'3')
                                self.assertNotEqual(taken[0][4],b'0')
                                self.assertEqual(stored[0][1:],taken[0][1:])
                                self.assertIn(b'F 0 1 0 0 1',rows,'query overwrote SEND receipt')
                                print('TYPED query=' + final.decode(),flush=True)
                            if cancel and network_query:
                                state['cancelled']=True
                            if os.environ.get('GIN_FIFO_OBSERVER') and network_control:
                                deadline=time.monotonic()+1
                                while time.monotonic()<deadline and (b'PF ' if network_part else b'JF ') not in final:
                                    try: final+=os.read(receipt_r,4096)
                                    except BlockingIOError: time.sleep(.01)
                                self.assertIn(b'F 0 1 0 0 1',final.splitlines(),'control overwrote SEND receipt')
                                if cancel:
                                    stored=[row.split() for row in final.splitlines() if row.startswith((b'PF ' if network_part else b'JF ')) ]
                                    self.assertEqual(len(stored),1)
                                    self.assertEqual(stored[0],[b'PF' if network_part else b'JF',b'1',b'1',terminal[2],b'3',terminal[4],b'1',b'1'],'owner lost actual uncertain control receipt')
                                else:
                                    self.assertIn((b'PF' if network_part else b'JF') + b' 1 1 0 1 202 1 1',final.splitlines(),'owner lost actual accepted control receipt')
                                print('TYPED stored-control=' + final.decode(),flush=True)
                            elif os.environ.get('GIN_FIFO_OBSERVER') and not network_query:
                                deadline=time.monotonic()+1
                                while time.monotonic()<deadline and b'F ' not in final:
                                    try: final+=os.read(receipt_r,4096)
                                    except BlockingIOError: time.sleep(.01)
                                self.assertIn(b'F 1 1 ',final,'owner lost stored terminal outcome')
                                fields=final.split()
                                self.assertEqual(fields[-1],b'1')
                                self.assertEqual(fields[4],b'4' if cancel else b'2')
                                if cancel: self.assertEqual(fields[3],terminal[2])
                                print('TYPED stored-terminal=' + final.decode(),flush=True)
                            state['post_release'].set()
                        late.set()
                    self.assertTrue(self.lock_available(root))
                collector.finish()
                assert proc is not None
                self.assertEqual(proc.returncode, 0)
                self.assertEqual(termios.tcgetattr(slave), before)
                self.assertEqual(state['eof'], [1, 2])
                print('OBSERVED role=' + ('network-query' if network_query else 'network-part' if network_part else 'network-join' if network_join else 'query' if query else 'channel') + ' request-count=' + str(len(state['records'])) +
                      ' message-POST-count=' + str(sum(m == 'POST' and p.endswith('/messages') for m, p in state['records'])) +
                      ' joins=' + str(len(state['joins'])) + ' orderly-exit=0 strict-handler-clean', flush=True)
                for secret in (b'HiddenSentinel', wire.TOKEN.encode(), b'FIFO tracer literal', b'/join #NewRoom', b'/part #old1', b'/query NewPeer', wire.ME['id'].encode()):
                    self.assertNotIn(secret, transcript)
                self.assertFalse((root / '.gin.credentials').exists())
                self.assertEqual((root / 'one/#old1/out').read_bytes(), expected['one/#old1/out'] + line('#old1', 35) + (line('#old1', 40) if posted else b''))
                for name, initial in expected.items():
                    if name != 'one/#old1/out':
                        self.assertEqual((root / name).read_bytes(), initial, 'unexpected output/local echo')
                if network_control or network_query:
                    for entry in root.rglob('*'):
                        if entry.is_file():
                            contents = entry.read_bytes()
                            # UUID is required identity metadata in GINACC/GINCP,
                            # already independently pinned by checkpoint_barrier.
                            # It is not an authentication bearer or password.
                            for secret in (b'HiddenSentinel', wire.TOKEN.encode()):
                                self.assertNotIn(secret, contents, 'persisted secret/credential UUID')
                            if entry.name not in ('.gin.account', '.gin.checkpoint'):
                                self.assertNotIn(wire.ME['id'].encode(), contents, 'UUID leaked outside private identity metadata')
                    self.assertFalse((root / 'one/#newroom').exists(), 'acceptance alone created a local window')
                    if network_part or network_query:
                        self.assertTrue((root / 'one/#old1').is_dir(), 'PART deleted retained window')
                        self.assertTrue(stat.S_ISFIFO((root / 'one/#old1/in').stat().st_mode), 'PART removed retained FIFO')
                        for name, identity in retained.items():
                            st = (root / name).stat()
                            self.assertEqual((st.st_dev, st.st_ino, stat.S_IFMT(st.st_mode)), identity, 'PART replaced retained local entry')
                    print('ORACLES CP-LIVE35 atomic-network-input orderly0 root-release TTY-restored EOF[1,2] strict-clean secrets-absent unrelated-output-unchanged', flush=True)
                    if network_query:
                        self.assertFalse((root / 'one/@newpeer').exists(), 'ACK alone created local query inventory')
                        self.assertEqual(state['records'], EXPECTED, 'query opening performed extra HTTP')
                        self.assertEqual(len(state['joins']), 13, 'query opening subscribed again')
                        print('QUERY-RECEIPT open_query_window=' + str(len(state['query_frames'])) + ' initial-HTTP=32 extra-HTTP=0 message-POST=0 owner-retained-before-stop', flush=True)
                        self.assertEqual(len(state['query_frames']), 1, 'semantic RED: healthy CLI ignored network FIFO QUERY; missing active-root WSS open_query_window network_id=1 target_nick=NewPeer')
                        return
                    if not posted:
                        self.assertEqual(state['records'], EXPECTED, 'unexpected HTTP before missing network control')
                        self.assertEqual(sum(m == 'POST' and path != '/auth/login' for m, path in state['records']), 0)
                        print('NO-MUTATION post-login-POST=0 DELETE=0 exact-initial-ledger', flush=True)
                    if network_part:
                        self.assertEqual(sum(m == 'DELETE' for m, _ in state['records']), int(posted))
                        self.assertTrue(posted, 'semantic RED: healthy CLI ignored network FIFO PART; missing DELETE /networks/one/channels/%23old1')
                    self.assertTrue(posted, 'semantic RED: healthy CLI ignored network FIFO JOIN; missing POST /networks/one/channels name=#NewRoom')
                if not posted:
                    self.assertEqual(state['records'], EXPECTED, 'unexpected HTTP before missing SEND')
                self.assertTrue(posted, 'semantic RED: healthy CLI never POSTed known-open QUERY FIFO SEND' if query else 'semantic RED: healthy CLI never POSTed window FIFO SEND')
                self.assertEqual(state['records'], EXPECTED + [('GET', '/api/config'), ('GET', '/me'), (mutation_method, message_path)])
                if network_part:
                    print('PASS network FIFO PART DELETE exact-path/no-body/auth fresh-config bound-me local-inodes-retained account-service-retained no-local-echo')
                else:
                    print('PASS ' + ('query' if query else 'channel') + ' FIFO literal POST exact-path/body/auth fresh-config bound-me no-local-echo')
            finally:
                if proc is not None and proc.poll() is None:
                    os.killpg(proc.pid, signal.SIGKILL); proc.wait(timeout=2)

class FIFO(FIFOFixture):
    def test_setup_failure_ownership(self):
        for step in ('pipe', '1', '2', '3', '4', 'tls'):
            with self.subTest(step=step):
                self.run_guard('from test_account_fifo_send import fixture_fault_probe; '
                               + 'fixture_fault_probe(' + repr(step) + ')')

    def test_transcript_late_output(self):
        self.run_guard("""
import contextlib, os, pty, tty
from test_account_fifo_send import PTYTranscript
with contextlib.ExitStack() as owned:
    master, slave = pty.openpty()
    owned.callback(os.close, master); owned.callback(os.close, slave)
    tty.setraw(slave)
    collector = PTYTranscript(master)
    owned.callback(collector.finish)
    os.write(slave, b'late-after-SEND HiddenSentinel')
    collector.finish()
    assert b'HiddenSentinel' in collector.data
""")

    def test_transcript_flood_is_bounded_and_drained(self):
        self.run_guard("""
import contextlib, os, pty, tty
from test_account_fifo_send import PTYTranscript
with contextlib.ExitStack() as owned:
    master, slave = pty.openpty()
    owned.callback(os.close, master); owned.callback(os.close, slave)
    tty.setraw(slave)
    collector = PTYTranscript(master)
    payload = b'x' * (collector.LIMIT * 2)
    sent = 0
    while sent < len(payload):
        sent += os.write(slave, payload[sent:])
    try:
        collector.finish()
    except AssertionError as exc:
        assert str(exc) == 'PTY transcript exceeded bounded collection cap'
    else:
        raise AssertionError('overflow silently accepted')
    assert len(collector.data) == collector.LIMIT
    assert not collector.thread.is_alive()
""")

    def test_channel(self):
        self.run_channel(False)

    def test_cancelled_post_close_guard(self):
        class Stream:
            def __init__(self, value):
                self.value = value
            def recv(self, size):
                if isinstance(self.value, BaseException):
                    raise self.value
                return self.value
        cancelled_post_eof(Stream(b''), True)
        cancelled_post_eof(Stream(BrokenPipeError(32, 'synthetic guard')), True)
        with self.assertRaises(AssertionError):
            cancelled_post_eof(Stream(BrokenPipeError(32, 'synthetic guard')), False)
        with self.assertRaises(AssertionError):
            cancelled_post_eof(Stream(b'x'), True)
        for failure in (ConnectionResetError, TimeoutError, ValueError):
            with self.subTest(failure=failure), self.assertRaises(failure):
                cancelled_post_eof(Stream(failure('synthetic guard')), True)

    def test_held_stop(self):
        if not os.environ.get('GIN_FIFO_OBSERVER'):
            self.skipTest('typed cancellation requires separately qualified private observer')
        self.run_channel(True)

    def test_query(self):
        self.run_channel(False, query=True)

    def test_query_held_stop(self):
        # Role characterization after the minimal query tracer fix, not RED.
        if not os.environ.get('GIN_FIFO_OBSERVER'):
            self.skipTest('typed cancellation requires separately qualified private observer')
        self.run_channel(True, query=True)


if __name__ == '__main__':
    unittest.main(defaultTest='FIFO.test_channel', verbosity=2)
