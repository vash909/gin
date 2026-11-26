#!/usr/bin/env python3
"""First quiet new QUERY focused history tracer, not account READY."""
import base64
import contextlib
import hashlib
import json
import os
import selectors
import shlex
import signal
import socket
import ssl
import stat
import subprocess
import threading
import time
import unittest
import sys
import test_session_outbound as checkpoint_fixture
from pathlib import Path
from collections.abc import Iterator
import test_http
from test_account_dispatch_hot_wss import ME, PLAN, TOKEN, State as BaseState, drain_tail
from ws_fixture_frames import server_frame, take_client_frame

FOCUSED = '/networks/one/channels/Aaron/messages?limit=50&after=0'
CONFIG = {'server': 'grappa', 'protocol_version': 36, 'min_protocol_version': 3}

def driver_command(origin: str, ca: str, root: str, scenario: str) -> list[str]:
    """Closed positive TLS/connect profile, independent of arbitrary prefixes.

    Ordinary/unset/empty retain 700 ms. Instrumented uses 1400 ms for BOTH
    startup and fresh history positive TLS/connect phases, not HTTP totals,
    JOIN, recovery episodes, held/negative semantics or parent deadlines.
    """
    profile = os.environ.get('GIN_HISTORY_ONE_SETUP_PROFILE', '')
    if profile not in ('', 'ordinary', 'instrumented-positive'):
        raise ValueError('unknown history setup profile: ' + profile)
    flags = ['--instrumented-positive-setup'] if profile == 'instrumented-positive' else []
    return shlex.split(os.environ.get('GIN_HISTORY_ONE_PREFIX', '')) + [
        os.environ['GIN_HISTORY_ONE_DRIVER'], origin, ca, root, scenario] + flags

class State(BaseState):
    phases: list[str]
    admission_path_count: int | None
    held_receipt: dict

class QuietHistory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass()

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass()

    @contextlib.contextmanager
    def fixture(self, held=False, probe=None) -> Iterator[tuple[State, str, tuple[threading.Event, ...]]]:
        listener = socket.socket()
        state: State = {'sent': [], 'paths': [], 'joins': [], 'errors': [],
                        'workers': [], 'eof': 0, 'upgrades': 0, 'phases': [],
                        'admission_path_count': None, 'held_receipt': {}}
        stop, initial_gate, admitted_gate, history_gate = (threading.Event() for _ in range(4))
        focused_entered, release_http = threading.Event(), threading.Event()
        release_row, rejection_observed = threading.Event(), threading.Event()
        accept_thread = None
        try:
            listener.bind(('127.0.0.1', 0))
            listener.listen(8)
            listener.settimeout(.05)
            ctx = test_http.HTTPTests().tls()

            def send(c, value):
                state['sent'].append(value)
                c.sendall(server_frame(json.dumps(value, separators=(',', ':')).encode()))

            def worker(raw):
                c = None
                cancelled_request = False
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

                        def old_message(body, rowid):
                            send(c, ['7', None, PLAN[6], 'event', {'kind': 'message', 'message': {
                                'kind': 'privmsg', 'network': 'one', 'channel': 'Peer',
                                'dm_with': 'Peer', 'sender': 'sender', 'body': body,
                                'server_time': 123, 'id': rowid}}])

                        for i, topic in enumerate(PLAN):
                            f = recvframe()
                            state['joins'].append(f)
                            ref = str(i+1)
                            assert f == [ref, ref, topic, 'phx_join', {}], 'JOIN golden mismatch'
                            if i < 9:
                                send(c, [ref, ref, topic, 'phx_reply', {'status': 'ok', 'response': {'protocol_version': 36} if i == 0 else {}}])
                                if i == 8:
                                    old_message('initial-old', 101)
                                    assert initial_gate.wait(3), 'client initial commit receipt missing'
                                    send(c, ['1', None, PLAN[0], 'event', {
                                        'kind': 'query_windows_list', 'windows': {'1': [
                                            {'network_id': 1, 'target_nick': 'Peer', 'opened_at': 'x'},
                                            {'network_id': 1, 'target_nick': 'Aaron', 'opened_at': 'new'}]}}])
                            else:
                                send(c, ['10', 'wrong', topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                                send(c, ['10', '10', topic, 'phx_reply', {'status': 'ok', 'response': {}}])
                                assert admitted_gate.wait(3), 'client quiet admission receipt missing'
                                old_message('after-new-old', 102)
                        if probe in ('unsupported', 'metadata'):
                            assert focused_entered.wait(3), 'post-admission parent receipt missing'
                            send(c, ['10', None, PLAN[9], 'event', {'kind': 'typing'}])
                            if probe == 'unsupported':
                                send(c, ['10', None, PLAN[9], 'event', {'kind': 'message', 'message': {
                                    'kind': 'future_plain', 'network': 'one', 'channel': 'Aaron',
                                    'dm_with': 'Aaron', 'sender': 'sender', 'body': 'bounded-regression',
                                    'server_time': 123, 'id': 201}}])
                        if held:
                            assert focused_entered.wait(3), 'focused handler entry missing'
                            assert not release_http.is_set(), 'HTTP released before old LIVE'
                            old_message('held-old', 103)
                            if probe == 'held-unsupported':
                                assert release_row.wait(3), 'old checkpoint/FD/lease barrier missing'
                                assert not release_http.is_set(), 'unsupported row released after HTTP'
                                send(c, ['10', None, PLAN[9], 'event', {'kind': 'message', 'message': {
                                    'kind': 'future_plain', 'network': 'one', 'channel': 'Aaron',
                                    'dm_with': 'Aaron', 'sender': 'sender', 'body': 'held-bounded-regression',
                                    'server_time': 123, 'id': 201}}])
                        assert history_gate.wait(3), 'real VISIBLE_DRAINED receipt missing'
                        drain_tail(c, buf, state)
                    else:
                        assert path in ('/api/config', '/me', FOCUSED)
                        if path == '/api/config':
                            assert 'Authorization' not in headers
                            value = CONFIG
                        else:
                            assert headers['Authorization'] == 'Bearer ' + TOKEN
                            value = ME if path == '/me' else []
                            if path == FOCUSED and held:
                                focused_entered.set()
                                assert release_http.wait(3), 'independent client checkpoint/rejection receipt missing'
                                if probe == 'held-unsupported':
                                    assert rejection_observed.is_set(), 'client rejection not observed'
                                    cancelled_request = True
                                    state['held_receipt']['cancelled_request'] = path
                                value = [{'kind': 'privmsg', 'network': 'one', 'channel': 'Aaron',
                                          'dm_with': 'Aaron', 'sender': 'sender', 'body': body,
                                          'server_time': 123, 'id': rowid}
                                         for rowid, body in ((10, 'rest-ten'), (20, 'rest-twenty'))]
                        body = json.dumps(value).encode()
                        try:
                            c.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: ' + str(len(body)).encode() + b'\r\n\r\n' + body)
                        except ssl.SSLEOFError:
                            # Only this held request, marked after client rejection.
                            if not (cancelled_request and path == FOCUSED and rejection_observed.is_set()):
                                raise
                            state['held_receipt']['qualified_tls_write_eof'] = True
                except BaseException as error:
                    state['errors'].append(type(error).__name__ + ': ' + str(error))
                finally:
                    if c is not None:
                        c.close()
                    raw.close()

            def accept_connections():
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
            yield state, f'https://127.0.0.1:{listener.getsockname()[1]}', (initial_gate, admitted_gate, history_gate, focused_entered, release_http, release_row, rejection_observed)
        finally:
            stop.set()
            end = time.monotonic()+5
            if accept_thread is not None:
                accept_thread.join(max(0, end-time.monotonic()))
            listener.close()
            for worker_thread in state['workers']:
                worker_thread.join(max(0, end-time.monotonic()))
            assert accept_thread is None or not accept_thread.is_alive(), 'accept thread leak'
            assert all(not thread.is_alive() for thread in state['workers']), 'worker leak'
            assert not state['errors'], state['errors']

    def pinned_bytes(self, path):
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK | os.O_CLOEXEC)
        try:
            before = os.fstat(fd)
            self.assertTrue(stat.S_ISREG(before.st_mode))
            self.assertEqual((before.st_uid, stat.S_IMODE(before.st_mode), before.st_nlink), (os.geteuid(), 0o600, 1))
            self.assertLessEqual(before.st_size, 8192)
            data = os.read(fd, 8193)
            after = os.fstat(fd)
            current = path.lstat()
            fields = lambda s: (s.st_dev, s.st_ino, s.st_uid, s.st_mode, s.st_nlink, s.st_size, s.st_mtime_ns, s.st_ctime_ns)
            self.assertEqual(fields(before), fields(after))
            self.assertEqual(fields(after), fields(current))
            self.assertEqual(len(data), after.st_size)
            return data, after
        finally:
            os.close(fd)

    def lock_available(self, root):
        code = "import os,sys,fcntl,errno; fd=os.open(sys.argv[1],os.O_RDWR|os.O_NOFOLLOW|os.O_NONBLOCK)\ntry:\n try: fcntl.lockf(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)\n except OSError as e:\n  if e.errno not in (errno.EACCES,errno.EAGAIN): raise\n  sys.exit(3)\nfinally: os.close(fd)\n"
        probe = subprocess.run([sys.executable, '-B', '-c', code, str(root/'.gin.lock')], capture_output=True, timeout=2)
        self.assertIn(probe.returncode, (0,3), probe.stderr)
        return probe.returncode == 0

    def test_quiet_new_query_requires_focused_history(self):
        self.run_scenario("quiet")

    def test_held_history_services_old_live_then_nonempty_rest(self):
        self.run_scenario("held")

    def test_unsupported_message_cannot_drain_success(self):
        self.run_scenario("unsupported")

    def test_held_unsupported_message_cannot_drain_success(self):
        self.run_scenario("held-unsupported")

    def test_nonrow_metadata_remains_acceptable(self):
        self.run_scenario("metadata")

    def run_scenario(self, scenario):
        held_negative = scenario == "held-unsupported"
        negative = scenario in ("unsupported", "held-unsupported")
        held = scenario in ("held", "held-unsupported")
        root = Path(os.environ['GIN_HISTORY_ONE_ROOT']) / scenario
        receipt_path = Path(os.environ['GIN_HISTORY_ONE_RECEIPT']).with_name(scenario+'-receipt.json')
        self.assertFalse(root.exists())
        root.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
        lines = []
        with self.fixture(held, scenario if scenario in ("unsupported", "metadata", "held-unsupported") else None) as (state, origin, gates):
            binary = os.environ['GIN_HISTORY_ONE_DRIVER']
            command = driver_command(origin, str(test_http.HTTPTests.certdir/'ca.pem'), str(root), scenario)
            launch = {'argv': command, 'cwd': os.getcwd(),
                      'selectors': {key: os.environ.get(key) for key in ('GIN_HISTORY_ONE_DRIVER', 'GIN_HISTORY_ONE_ROOT', 'GIN_HISTORY_ONE_RECEIPT', 'GIN_HISTORY_ONE_PREFIX', 'GIN_HISTORY_ONE_SETUP_PROFILE')},
                      'binary': binary, 'prefix_argv': shlex.split(os.environ.get('GIN_HISTORY_ONE_PREFIX', '')),
                      'binary_sha256': hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                      'scenario': scenario, 'ca_sha256': hashlib.sha256((test_http.HTTPTests.certdir/'ca.pem').read_bytes()).hexdigest()}
            receipt_path.with_suffix('.launch.json').write_text(json.dumps(launch, indent=2)+'\n')
            receipt_path.with_suffix('.ca.pem').write_bytes((test_http.HTTPTests.certdir/'ca.pem').read_bytes())
            p = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 start_new_session=True, env=dict(os.environ))
            initial_checkpoint = None
            try:
                assert p.stdout is not None
                os.set_blocking(p.stdout.fileno(), False)
                pending = bytearray()
                deadline = time.monotonic()+8
                with selectors.DefaultSelector() as selector:
                    selector.register(p.stdout, selectors.EVENT_READ)
                    for prefix, gate in zip((b'INITIAL_READY_OLD_COMMITTED\n', b'NEW_QUIET_ADMITTED ', b'OLD_POST_ADMISSION_COMMITTED ', b'TYPED_RESULT ' if negative else b'NEW_HISTORY_VISIBLE_DRAINED '), (gates[0], gates[1], None, gates[2])):
                        while b'\n' not in pending:
                            assert selector.select(max(0, deadline-time.monotonic())), 'parent receipt deadline'
                            block = os.read(p.stdout.fileno(), 4096)
                            assert block, 'early driver EOF before healthy control'
                            pending.extend(block)
                        line, _, rest = pending.partition(b'\n')
                        pending = bytearray(rest)
                        line += b'\n'
                        if held_negative and prefix == b'TYPED_RESULT ':
                            self.assertEqual(line, b'HELD_REJECTED source=9 ref=10 epoch=1 old_appended=1 selected_appended=0\n')
                            lines.append(line)
                            while b'\n' not in pending:
                                assert selector.select(max(0, deadline-time.monotonic())), 'rejection receipt deadline'
                                block = os.read(p.stdout.fileno(), 4096)
                                assert block, 'EOF before rejection receipt'
                                pending.extend(block)
                            line, _, rest = pending.partition(b'\n')
                            pending = bytearray(rest)
                            line += b'\n'
                        self.assertTrue(line.startswith(prefix), line)
                        lines.append(line)
                        state['phases'].append(line.decode().rstrip())
                        if gate is gates[0]:
                            initial_checkpoint = (root/'one/@peer/.gin.checkpoint').read_bytes()
                        elif gate is gates[1]:
                            self.assertEqual((root/'one/@peer/.gin.checkpoint').read_bytes(), initial_checkpoint,
                                             'quiet admission must preserve the exact old checkpoint')
                            self.assertEqual((root/'one/@peer/out').read_bytes(),
                                             b'[1970-01-01T00:00:00.123Z] privmsg sender="sender" body="initial-old"\n')
                            state['admission_path_count'] = len(state['paths'])
                        if scenario in ('unsupported', 'metadata') and prefix == b'OLD_POST_ADMISSION_COMMITTED ':
                            gates[3].set()
                        if held and prefix == b'OLD_POST_ADMISSION_COMMITTED ':
                            self.assertTrue(gates[3].wait(max(0, deadline-time.monotonic())), 'held handler entry missing')
                            self.assertFalse(gates[4].is_set())
                            old_log = root/'one/@peer/out'
                            expected = (b'[1970-01-01T00:00:00.123Z] privmsg sender="sender" body="initial-old"\n'
                                        b'[1970-01-01T00:00:00.123Z] privmsg sender="sender" body="after-new-old"\n'
                                        b'[1970-01-01T00:00:00.123Z] privmsg sender="sender" body="held-old"\n')
                            # Existing bounded no-follow descriptor observer parses GINCP,
                            # handles atomic rename retirement and checks owner liveness.
                            checkpoint_fixture.wait_checkpoint(self, p, old_log, expected, min(1.2, deadline-time.monotonic()))
                            self.assertIsNone(p.poll())
                            self.assertFalse(gates[4].is_set(), 'response must still be held')
                            fields = dict(item.split('=') for item in line.decode().split()[1:])
                            pinned = os.stat(f'/proc/{p.pid}/fd/{fields["fd"]}')
                            self.assertEqual((pinned.st_dev, pinned.st_ino, pinned.st_size),
                                             (int(fields['dev']), int(fields['ino']), len(expected)))
                            log_bytes, log_meta = self.pinned_bytes(old_log)
                            self.assertEqual(log_bytes, expected)
                            self.assertEqual((log_meta.st_dev, log_meta.st_ino, log_meta.st_size),
                                             (pinned.st_dev, pinned.st_ino, pinned.st_size))
                            checkpoint, _ = self.pinned_bytes(root/'one/@peer/.gin.checkpoint')
                            assert initial_checkpoint is not None
                            self.assertEqual(checkpoint[:40], initial_checkpoint[:40])
                            self.assertEqual(checkpoint[48:], initial_checkpoint[48:])
                            self.assertFalse((root/'one/@aaron/.gin.checkpoint').exists())
                            self.assertEqual((root/'one/@aaron/out').read_bytes(), b'')
                            self.assertFalse(self.lock_available(root), 'root lease escaped during tracer')
                            state['held_receipt'] = {'handler_still_held': True, 'old_output_hex': expected.hex(),
                                'checkpoint_hex': checkpoint.hex(), 'old_fd': int(fields['fd']),
                                'dev': pinned.st_dev, 'ino': pinned.st_ino, 'size': pinned.st_size,
                                'anchor': 0, 'unknown_before': True, 'root_lease_held': True}
                            if held_negative:
                                gates[5].set()
                            else:
                                gates[4].set()
                        if held_negative and prefix == b'TYPED_RESULT ':
                            fields = dict(item.split('=') for item in line.decode().split()[1:])
                            self.assertEqual((fields['code'], fields['drained'], fields['incomplete'], fields['retained'], fields['queued'], fields['pages'], fields['cleanup'], fields['quarantine']),
                                             ('6','0','1','0','1','0','0','0'))
                            self.assertFalse(gates[4].is_set(), 'HTTP escaped before client rejection')
                            self.assertEqual((root/'one/@peer/out').read_bytes().hex(), state['held_receipt']['old_output_hex'])
                            self.assertEqual((root/'one/@peer/.gin.checkpoint').read_bytes().hex(), state['held_receipt']['checkpoint_hex'])
                            self.assertEqual((root/'one/@aaron/out').read_bytes(), b'')
                            self.assertFalse((root/'one/@aaron/.gin.checkpoint').exists())
                            gates[6].set()
                            gates[4].set()
                        if gate is not None:
                            gate.set()
                out, err = p.communicate(timeout=max(.01, deadline-time.monotonic()))
                out = b''.join(lines)+bytes(pending)+out
            except BaseException:
                if p.poll() is None:
                    os.killpg(p.pid, signal.SIGKILL)
                out, err = p.communicate()
                # Persist the actual reaped OS exit, including instrumentation overrides.
                # stderr diagnostics never enter the line-oriented stdout protocol.
                receipt_path.with_suffix('.failure.json').write_text(json.dumps({
                    'returncode': p.returncode, 'stdout': (b''.join(lines)+out).decode(errors='replace'),
                    'stderr': err.decode(errors='replace'), 'paths': state['paths'],
                    'phases': state['phases'], 'upgrades': state['upgrades'],
                    'joins': len(state['joins']), 'reaped': p.poll() is not None,
                    'native_start_diagnostics': [line for line in err.decode(errors='replace').splitlines()
                                                 if line.startswith('NATIVE_START ')]}, indent=2)+'\n')
                print('SETUP_DRIVER_OUTPUT', b''.join(lines)+out, err, flush=True)
                raise
        # Preserve baseline controls and require the actual API terminal receipt.
        self.assertEqual(p.returncode, 0, err.decode())
        self.assertEqual(state['upgrades'], 1)
        self.assertEqual(state['eof'], 1)
        self.assertEqual(len(state['joins']), 10)
        self.assertIn(b'CLEANUP_COMPLETE\n', out)
        self.assertIn(b'OLD_POST_ADMISSION_COMMITTED ', out)
        self.assertNotIn(TOKEN.encode(), out+err)
        self.assertEqual(err, b'')
        self.assertEqual(state['paths'][:3], ['/api/config', '/me', '/socket/websocket?vsn=2.0.0&client_proto=4'])
        if negative:
            expected_suffix = ['/api/config', '/me', FOCUSED] if held_negative else []
            if held_negative:
                self.assertEqual(state['paths'][3:], expected_suffix, 'exact negative paths; no retry/new request')
            if held_negative:
                self.assertEqual((root/'one/@peer/out').read_bytes().hex(), state['held_receipt']['old_output_hex'])
                self.assertEqual((root/'one/@peer/.gin.checkpoint').read_bytes().hex(), state['held_receipt']['checkpoint_hex'])
                self.assertEqual(set(root.rglob('.gin.checkpoint')), {root/'one/@peer/.gin.checkpoint'})
                selected_messages = [f for f in state['sent'] if isinstance(f, list) and len(f) == 5 and f[2] == PLAN[9] and isinstance(f[4], dict) and f[4].get('kind') == 'message']
                self.assertEqual(len(selected_messages), 1)
            logs = {str(path.relative_to(root)): path.read_bytes().hex() for path in root.rglob('out')}
            receipt_path.write_text(json.dumps(dict(state, workers=[], stdout=out.decode(), stderr=err.decode(),
                returncode=p.returncode, output_hex=logs, healthy_controls=True,
                checkpoints={str(path.relative_to(root)): path.read_bytes().hex() for path in root.rglob('.gin.checkpoint')},
                root_lease_released=self.lock_available(root)), indent=2)+'\n')
            print(out.decode(), end='', flush=True)
            fields = dict(x.split('=') for x in next(x for x in out.decode().splitlines() if x.startswith('TYPED_RESULT ')).split()[1:])
            self.assertEqual((fields['drained'], fields['incomplete'], fields['retained']), ('0','1','0'),
                             'ONE-001: unsupported consumed message must not become drained success')
            self.assertNotEqual(fields['code'], '0')
            self.assertEqual(fields['queued'], '1', 'failed row must remain queued; preceding non-row metadata may be consumed')
            self.assertEqual((fields['cleanup'], fields['quarantine']), ('0','0'))
            self.assertEqual((root/'one/@aaron/out').read_bytes(), b'')
            self.assertFalse((root/'one/@aaron/.gin.checkpoint').exists())
            self.assertTrue(self.lock_available(root))
            return
        self.assertEqual(state['paths'][3:], ['/api/config', '/me', FOCUSED], 'fresh bound focused fetch only; no old history')
        self.assertIn((f'NEW_HISTORY_VISIBLE_DRAINED pages=1 rows={2 if held else 0} after=0 unknown=1 cursor={20 if held else 0} old_appended={1 if held else 0} selected_appended={2 if held else 0}\n').encode(), out)
        self.assertFalse(any('/messages?' in path and path != FOCUSED for path in state['paths'][state['admission_path_count']:]), 'old history must not restart after admission')
        self.assertFalse(any(isinstance(f, list) and len(f)==5 and isinstance(f[4], dict) and f[4].get('kind')=='message' and f[2]==PLAN[9] for f in state['sent']), 'new query must remain application-frame quiet')
        def expected_line(body: str) -> bytes:
            return ('[1970-01-01T00:00:00.123Z] privmsg sender="sender" body="'+body+'"\n').encode()
        logs = {str(path.relative_to(root)): path.read_bytes() for path in root.rglob('out')}
        self.assertEqual(logs, {'one/out': b'', 'two/out': b'', 'one/#old/out': b'',
                                'one/@peer/out': expected_line('initial-old')+expected_line('after-new-old')+(expected_line('held-old') if held else b''),
                                'one/@aaron/out': expected_line('rest-ten')+expected_line('rest-twenty') if held else b''})
        cp = root/'one/@peer/.gin.checkpoint'
        checkpoint = cp.read_bytes()
        st = (cp.parent/'out').stat()
        self.assertIsNotNone(initial_checkpoint)
        assert initial_checkpoint is not None
        self.assertEqual(checkpoint[:24], initial_checkpoint[:24])
        self.assertEqual(checkpoint[24:40], initial_checkpoint[24:40])
        self.assertEqual(checkpoint[48:], initial_checkpoint[48:])
        self.assertNotEqual(checkpoint[40:48], initial_checkpoint[40:48])
        self.assertEqual(checkpoint[:8], b'GINCP\0\0\1')
        self.assertEqual([int.from_bytes(checkpoint[i:i+8], 'big') for i in range(8,48,8)], [0,1,st.st_dev,st.st_ino,st.st_size])
        if held:
            selected = root/'one/@aaron/out'
            selected_cp = selected.with_name('.gin.checkpoint').read_bytes()
            sm = selected.stat()
            self.assertEqual(selected_cp[:8], b'GINCP\0\0\1')
            self.assertEqual([int.from_bytes(selected_cp[i:i+8], 'big') for i in range(8,48,8)], [0,1,sm.st_dev,sm.st_ino,sm.st_size])
            self.assertEqual(set(root.rglob('.gin.checkpoint')), {cp, selected.with_name('.gin.checkpoint')})
            self.assertTrue(state['held_receipt']['handler_still_held'])
        else:
            self.assertFalse((root/'one/@aaron/.gin.checkpoint').exists())
            self.assertEqual(list(root.rglob('.gin.checkpoint')), [cp])
        self.assertTrue(self.lock_available(root), 'root borrower cleanup did not release lease')
        for path in root.rglob('*'):
            meta = path.lstat()
            self.assertEqual(stat.S_IMODE(meta.st_mode), 0o700 if stat.S_ISDIR(meta.st_mode) else 0o600)
            if stat.S_ISREG(meta.st_mode):
                self.assertEqual(meta.st_nlink, 1)
                self.assertNotIn(TOKEN.encode(), path.read_bytes())
        receipt = {key: value for key, value in state.items() if key!='workers'}
        receipt.update(command=command, cwd=os.getcwd(), stdout=out.decode(), stderr=err.decode(),
                       returncode=p.returncode, healthy_controls=True, strict_cleanup_before_oracle=True,
                       classification='first quiet focused history GREEN, not account READY',
                       cold_bootstrap_calls=0, new_query_application_frames=0,
                       old_initial_checkpoint_hex=initial_checkpoint.hex(), old_checkpoint_hex=checkpoint.hex(),
                       output_hex={key: value.hex() for key,value in logs.items()},
                       binary_sha256=hashlib.sha256(Path(binary).read_bytes()).hexdigest(),
                       ca_sha256=hashlib.sha256((test_http.HTTPTests.certdir/'ca.pem').read_bytes()).hexdigest())
        receipt_path.write_text(json.dumps(receipt, indent=2)+'\n')
        print(out.decode(), end='', flush=True)
        self.assertIn(FOCUSED, state['paths'], 'MISSING_FOCUSED_AARON_GET: quiet new query is not recovered without focused after=0 history; component-composition semanticRED, not legacy API defect')

if __name__ == '__main__':
    unittest.main(verbosity=2)
