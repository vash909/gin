"""Production CLI negatives: private files and verified loopback HTTPS/WSS only."""
import base64
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import select
import signal
import socket
import ssl
import subprocess
import threading
import time
from typing import Any, Iterator, cast
import unittest

import test_session as positive
from ws_fixture_frames import server_frame, take_client_frame

REPO = positive.REPO
PREFIX = positive.PREFIX
TOKEN = positive.TOKEN
UUID = positive.UUID
WAIT = 90 if PREFIX else 12


class NegativeServer(http.server.ThreadingHTTPServer):
    daemon_threads = False
    options: dict[str, Any]
    paths: list[str]
    methods: list[str]
    joins: list[str]
    errors: list[str]
    stopped: threading.Event
    barrier: threading.Event
    release: threading.Event
    allow_close: threading.Event
    upgraded: bool

    def handle_error(self, request: Any, client_address: Any) -> None:
        import sys
        self.errors.append(type(sys.exc_info()[1]).__name__)


class NegativeHandler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def log_message(self, format: str, *args: Any) -> None:
        pass

    def handle_one_request(self) -> None:
        srv = cast(NegativeServer, self.server)
        try:
            super().handle_one_request()
        except BrokenPipeError:
            # Certificate rejection can close an accepted TLS connection while
            # SSL_read performs protocol writes, before the first HTTP line.
            # Only the explicit TLS-negative fixture permits that exact stage.
            if (not srv.options.get('expect_tls_rejection')
                    or hasattr(self, 'raw_requestline')
                    or srv.paths or srv.methods or srv.joins or srv.upgraded):
                raise
            self.close_connection = True

    def do_POST(self) -> None:
        srv = cast(NegativeServer, self.server)
        srv.methods.append(self.command)
        raise AssertionError('unexpected mutating request')

    do_DELETE = do_POST
    do_PUT = do_POST

    def do_GET(self) -> None:
        srv = cast(NegativeServer, self.server)
        srv.paths.append(self.path)
        srv.methods.append('GET')
        opt = srv.options
        status = 200
        if self.path == '/api/config':
            assert self.headers.get('Authorization') is None, 'credentialed discovery'
            body = {'server': 'grappa', 'protocol_version': opt.get('protocol', 7),
                    'min_protocol_version': opt.get('minimum', 1)}
        elif self.path == '/me':
            assert self.headers.get('Authorization') == 'Bearer ' + TOKEN, 'bad authorization'
            status = opt.get('me_status', 200)
            body = {'kind': opt.get('kind', 'user'), 'id': opt.get('uuid', UUID), 'name': 'Alice'}
            if status != 200:
                body = {'error': 'client_token_scope'}
        elif self.path == '/boot':
            assert self.headers.get('Authorization') == 'Bearer ' + TOKEN, 'bad authorization'
            if opt.get('hold_http'):
                srv.barrier.set()
                assert srv.release.wait(WAIT), 'held HTTP barrier expired'
            channels = [] if opt.get('missing_route') else [
                {'name': '#Room', 'joined': not opt.get('unjoined'), 'source': 'joined'}]
            body = {'networks': [{'id': 3000000001, 'slug': 'NetA', 'nick': 'OwnA'}],
                    'channels': {'NetA': channels}, 'heads': {}}
        elif self.path == '/socket/websocket?vsn=2.0.0&client_proto=4':
            self.upgrade()
            return
        else:
            raise AssertionError('unexpected resource')
        raw = json.dumps(body).encode()
        self.send_response(status)
        self.send_header('Content-Length', str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def upgrade(self) -> None:
        srv = cast(NegativeServer, self.server)
        opt = srv.options
        assert self.headers.get('Authorization') is None, 'upgrade bearer header'
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+', 'bad subprotocol'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name, value in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                            ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        srv.upgraded = True
        self.connection.settimeout(.2)
        buffered = bytearray()
        root_ref = ''
        root_topic = ''

        def packet(frame: list[Any]) -> bytes:
            return server_frame(json.dumps(frame).encode())

        def metadata() -> bytes:
            result = b''
            for index in range(24):
                result += packet([root_ref, None, root_topic, 'event',
                                  {'kind': 'network_status', 'index': index,
                                   'nested': {'kind': 'query_windows_list'}}])
            for present in opt.get('snapshots', []):
                windows = {'3000000001': [{'network_id': 3000000001,
                    'target_nick': 'Other' if present == 'other' else 'Peer', 'opened_at': 'fixture'}]} if present else {}
                result += packet([root_ref, None, root_topic, 'event',
                                  {'kind': 'query_windows_list', 'windows': windows}])
            return result

        try:
            for index in range(3):
                while True:
                    item = take_client_frame(buffered)
                    if item is not None:
                        break
                    if srv.stopped.is_set():
                        return
                    try:
                        data = self.connection.recv(16384)
                    except socket.timeout:
                        continue
                    if not data and srv.allow_close.is_set():
                        return
                    assert data, 'client closed before subscription'
                    buffered.extend(data)
                opcode, final, payload = item
                assert opcode == 1 and final, 'unexpected non-text join'
                jr, ref, topic, event, body = json.loads(payload)
                assert event == 'phx_join' and body == {} and jr == ref, 'invalid join'
                srv.joins.append(topic)
                if index == 0:
                    root_ref, root_topic = jr, topic
                response = {'protocol_version': opt.get('root_protocol', 7)} if index == 0 else {}
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': response}]
                if index == opt.get('reject_join', -1):
                    reply[4]['status'] = 'error'
                if index == 2:
                    if opt.get('wrong_ack'):
                        reply[1] = 'not-the-request'
                    if opt.get('hold_ack'):
                        srv.barrier.set()
                        assert srv.release.wait(WAIT), 'held ACK barrier expired'
                        if srv.stopped.is_set():
                            return
                wire = packet(reply)
                if (index == 0 and opt.get('early')) or (index == 2 and not opt.get('early')):
                    wire += metadata()
                self.connection.sendall(wire)
                if index == opt.get('reject_join', -1) or (index == 0 and opt.get('root_protocol', 7) < 4):
                    break
            srv.barrier.set()
            while not srv.stopped.is_set():
                try:
                    data = self.connection.recv(16384)
                except socket.timeout:
                    continue
                if not data:
                    return
                buffered.extend(data)
                while True:
                    item = take_client_frame(buffered)
                    if item is None:
                        break
                    assert item[0] in (8, 10), 'unexpected ordinary application operation'
        except (ConnectionResetError, BrokenPipeError, ssl.SSLEOFError):
            # Only intentional post-upgrade client teardown is allowed.
            if not srv.upgraded or not srv.allow_close.is_set():
                raise
        finally:
            self.close_connection = True


class SessionFailures(unittest.TestCase):
    certdir: Path
    scratch: Any
    tls = positive.StartupTracer.tls
    command = positive.StartupTracer.command

    @classmethod
    def setUpClass(cls) -> None:
        positive.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls) -> None:
        positive.StartupTracer.tearDownClass.__func__(cls)

    def setUp(self) -> None:
        import tempfile
        self.case = tempfile.TemporaryDirectory(dir=self.certdir)
        self.addCleanup(self.case.cleanup)
        self.base = Path(self.case.name)
        self.root = self.base / 'root'
        self.settings = self.base / 'settings'
        self.settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
                                 if PREFIX else 'connect_ms=1000\ntotal_ms=2000\njoin_ms=600\nstartup_ms=3500\n')
        self.settings.chmod(0o600)

    @contextlib.contextmanager
    def fixture(self, **options: Any) -> Iterator[tuple[NegativeServer, str]]:
        srv = NegativeServer(('127.0.0.1', 0), NegativeHandler)
        srv.options = options
        srv.paths, srv.methods, srv.joins, srv.errors = [], [], [], []
        srv.upgraded = False
        srv.stopped, srv.barrier = threading.Event(), threading.Event()
        srv.release, srv.allow_close = threading.Event(), threading.Event()
        srv.socket = self.tls(options.get('certificate', 'good')).wrap_socket(srv.socket, server_side=True)
        worker = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        worker.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.release.set()
            srv.shutdown()
            srv.server_close()
            worker.join(3)
            self.assertFalse(worker.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'fixture handler failed: ' + ','.join(srv.errors))
            self.assertTrue(all(method == 'GET' for method in srv.methods), 'mutating request observed')

    def store(self, origin: str) -> Path:
        self.root.mkdir(mode=0o700, exist_ok=True)
        path = self.root / '.gin.credentials'
        path.write_text(json.dumps({'version': 1, 'origin': origin, 'token': TOKEN,
                                   'subject_id': UUID, 'subject_name': 'Alice'}))
        path.chmod(0o600)
        return path

    def args(self, origin: str, target: str = '#Room') -> list[str]:
        return ['connect', '-f', str(self.settings), '-d', str(self.root), '-n', 'NetA',
                '-c', target, '-e', origin, '-m', 'plain', '-a', str(self.certdir / 'ca.pem')]

    def quiet(self, proc: subprocess.CompletedProcess[bytes], code: int | None = None) -> None:
        if code is None:
            self.assertNotEqual(proc.returncode, 0, 'negative CLI unexpectedly succeeded')
        else:
            self.assertEqual(proc.returncode, code, 'unexpected CLI exit')
        self.assertTrue(proc.stdout == b'', 'unexpected ordinary output')
        self.assertFalse(any(secret in proc.stdout + proc.stderr for secret in [TOKEN.encode(), b'YWF+']),
                         'credential disclosed')

    def run_cli(self, args: list[str], **kwargs: Any) -> subprocess.CompletedProcess[bytes]:
        return subprocess.run(self.command(*args), capture_output=True, timeout=WAIT, **kwargs)

    @contextlib.contextmanager
    def child(self, args: list[str], srv: NegativeServer, stdin: Any = subprocess.DEVNULL,
              status: int | None = None) -> Iterator[tuple[subprocess.Popen[bytes], int]]:
        read_fd, write_fd = os.pipe()
        os.set_blocking(read_fd, False)
        os.set_blocking(write_fd, False)
        inherited = write_fd if status is None else status
        store = self.root / '.gin.credentials'
        before = store.read_bytes()
        try:
            proc = subprocess.Popen(self.command(*args, '-R', inherited), pass_fds=(inherited,),
                                    stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except BaseException:
            os.close(read_fd)
            os.close(write_fd)
            raise
        os.close(write_fd)
        try:
            yield proc, read_fd
        finally:
            srv.allow_close.set()
            srv.stopped.set()
            srv.release.set()
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=WAIT)
            os.close(read_fd)
            self.assertTrue(store.read_bytes() == before, 'connect changed stored credential')

    def finish(self, proc: subprocess.Popen[bytes], code: int) -> None:
        out, err = proc.communicate(timeout=WAIT)
        self.quiet(subprocess.CompletedProcess([], proc.returncode, out, err), code)

    def ready(self, proc: subprocess.Popen[bytes], fd: int) -> None:
        self.assertTrue(select.select([fd], [], [], WAIT)[0], 'READY watchdog expired')
        self.assertEqual(os.read(fd, 128), b'READY\n', 'no exact READY record')
        self.assertIsNone(proc.poll(), 'READY from dead session')

    def no_ready(self, fd: int) -> None:
        if select.select([fd], [], [], .15)[0]:
            self.assertNotIn(b'READY', os.read(fd, 65536), 'premature READY')

    def barrier(self, srv: NegativeServer, proc: subprocess.Popen[bytes]) -> None:
        deadline = time.monotonic() + WAIT
        while not srv.barrier.wait(.02):
            self.assertIsNone(proc.poll(), 'CLI exited before fixture barrier')
            self.assertLess(time.monotonic(), deadline, 'fixture barrier watchdog expired')

    def test_invalid_modes_and_non_https_fail_before_root(self) -> None:
        with self.fixture() as (srv, origin):
            args = self.args(origin)
            for mode in ['encrypted', 'Plain', '', None]:
                with self.subTest(mode=mode):
                    changed = args.copy()
                    at = changed.index('-m')
                    changed[at:at + 2] = [] if mode is None else ['-m', mode]
                    self.quiet(self.run_cli(changed), 2)
                    self.assertFalse(self.root.exists())
            changed = self.args(origin.replace('https:', 'http:'))
            self.quiet(self.run_cli(changed), 2)
            self.assertEqual(srv.paths, [])

    def test_raw_bad_configuration_is_not_hidden_by_overrides(self) -> None:
        with self.fixture() as (srv, origin):
            for raw in [b'unknown=value\n', b'mode=plain\nmode=plain\n', b'#bad\x00\n',
                        b'total_ms=01\n', b'origin=http://127.0.0.1\n', b'mode=encrypted\n']:
                with self.subTest(case=raw.split(b'=')[0]):
                    self.settings.write_bytes(raw)
                    self.quiet(self.run_cli(self.args(origin)), 2)
                    self.assertFalse(self.root.exists())
            self.assertEqual(srv.paths, [])

    def test_unsafe_config_metadata_is_rejected_without_root(self) -> None:
        with self.fixture() as (srv, origin):
            self.settings.chmod(0o644)
            self.quiet(self.run_cli(self.args(origin)), 1)
            self.settings.chmod(0o600)
            alias = self.base / 'alias'
            os.link(self.settings, alias)
            self.quiet(self.run_cli(self.args(origin)), 1)
            alias.unlink()
            saved = self.base / 'saved'
            self.settings.rename(saved)
            self.settings.symlink_to(saved)
            self.quiet(self.run_cli(self.args(origin)), 1)
            self.settings.unlink()
            os.mkfifo(self.settings, 0o600)
            self.quiet(self.run_cli(self.args(origin)), 1)
            self.assertFalse(self.root.exists())
            self.assertEqual(srv.paths, [])

    def test_legacy_init_check_and_local_run_remain_offline(self) -> None:
        args = ['-d', str(self.root), '-n', 'NetA', '-c', '#Room']
        self.quiet(self.run_cli(['check', *args]), 0)
        self.assertFalse(self.root.exists())
        self.quiet(self.run_cli(['init', *args]), 0)
        proc = subprocess.Popen(self.command('run', *args), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            fifo = next(self.root.rglob('in'))
            deadline = time.monotonic() + WAIT
            while True:
                try:
                    fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK)
                    os.close(fd)
                    break
                except OSError as error:
                    import errno
                    if error.errno != errno.ENXIO:
                        raise
                    self.assertIsNone(proc.poll(), 'local runtime exited before FIFO reader')
                    self.assertLess(time.monotonic(), deadline, 'local FIFO watchdog expired')
                    time.sleep(.02)
            proc.send_signal(signal.SIGTERM)
            proc.communicate(timeout=WAIT)
            self.assertEqual(proc.returncode, 0)
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.communicate(timeout=WAIT)

    def test_tls_bad_ca_and_san_never_send_http(self) -> None:
        for certificate, ca in [('good', 'bad.pem'), ('wrong', 'ca.pem')]:
            with self.subTest(certificate=certificate), self.fixture(
                    certificate=certificate, expect_tls_rejection=True) as (srv, origin):
                store = self.store(origin)
                before = store.read_bytes()
                args = self.args(origin)
                args[-1] = str(self.certdir / ca)
                self.quiet(self.run_cli(args), 1)
                self.assertEqual(srv.paths, [])
                self.assertEqual(srv.methods, [])
                self.assertEqual(srv.joins, [])
                self.assertFalse(srv.upgraded)
                self.assertTrue(store.read_bytes() == before, 'store changed')

    def test_origin_and_uuid_binding_preserve_store(self) -> None:
        with self.fixture(uuid='22345678-1234-1234-1234-123456789abc') as (srv, origin):
            store = self.store(origin + '/')
            before = store.read_bytes()
            self.quiet(self.run_cli(self.args(origin)), 1)
            self.assertEqual(srv.paths, [])
            self.assertTrue(store.read_bytes() == before, 'store changed')
            store = self.store(origin)
            before = store.read_bytes()
            self.quiet(self.run_cli(self.args(origin)), 1)
            self.assertEqual(srv.paths, ['/api/config', '/me'])
            self.assertTrue(store.read_bytes() == before, 'store changed')

    def test_discovery_protocol_and_minimum_gate_credentials(self) -> None:
        for options in [{'protocol': 3}, {'minimum': 5}]:
            with self.subTest(options=options), self.fixture(**options) as (srv, origin):
                self.store(origin)
                self.quiet(self.run_cli(self.args(origin)), 1)
                self.assertEqual(srv.paths, ['/api/config'])

    def test_root_protocol_and_join_rejection_never_ready_or_retry(self) -> None:
        for options in [{'root_protocol': 3}, {'reject_join': 1}, {'wrong_ack': True}]:
            with self.subTest(options=options), self.fixture(**options) as (srv, origin):
                self.store(origin)
                srv.allow_close.set()
                with self.child(self.args(origin), srv) as (proc, fd):
                    self.finish(proc, 1)
                    self.no_ready(fd)
                self.assertEqual(srv.paths.count('/api/config'), 2)
                self.assertEqual(len([p for p in srv.paths if p.startswith('/socket/')]), 1)

    def test_import_enrolls_config_then_me_without_token_output(self) -> None:
        with self.fixture() as (srv, origin):
            source = self.base / 'bearer'
            source.write_bytes(TOKEN.encode())
            source.chmod(0o600)
            args = ['credential-import', '-d', str(self.root), '-e', origin, '-s', str(source),
                    '-a', str(self.certdir / 'ca.pem')]
            self.quiet(self.run_cli(args), 0)
            self.assertEqual(srv.paths, ['/api/config', '/me'])
            store = self.root / '.gin.credentials'
            self.assertEqual(store.stat().st_mode & 0o7777, 0o600)
            saved = json.loads(store.read_bytes())
            self.assertTrue(saved['token'] == TOKEN, 'stored bearer mismatch')
            self.assertEqual(saved['subject_id'], UUID)
            self.assertEqual(saved['origin'], origin)

            # Input safety precedes root ownership and enrollment traffic.
            before = store.read_bytes()
            for unsafe in ['mode', 'hardlink', 'symlink']:
                source.unlink()
                source.write_bytes(TOKEN.encode())
                source.chmod(0o600)
                alias = self.base / unsafe
                if unsafe == 'mode':
                    source.chmod(0o644)
                elif unsafe == 'hardlink':
                    os.link(source, alias)
                else:
                    source.rename(alias)
                    source.symlink_to(alias)
                self.quiet(self.run_cli(args), 1)
                self.assertTrue(store.read_bytes() == before, 'unsafe import changed old store')
                self.assertEqual(srv.paths, ['/api/config', '/me'])

    def test_import_unsafe_scope_and_malformed_uuid_preserve_old_store(self) -> None:
        for options in [{}, {'me_status': 403}, {'uuid': 'invalid'}]:
            with self.subTest(options=options), self.fixture(**options) as (srv, origin):
                store = self.store(origin)
                before = store.read_bytes()
                source = self.base / 'bearer'
                source.write_bytes(TOKEN.encode() if options else TOKEN.encode() + b'\n')
                source.chmod(0o600)
                args = ['credential-import', '-d', str(self.root), '-e', origin, '-s', str(source),
                        '-a', str(self.certdir / 'ca.pem')]
                self.quiet(self.run_cli(args), 1)
                self.assertTrue(store.read_bytes() == before, 'failed import changed old store')
                self.assertEqual(srv.paths, ['/api/config', '/me'] if options else [])

    def test_missing_and_unjoined_channel_route_stop_before_wss(self) -> None:
        for options in [{'missing_route': True}, {'unjoined': True}]:
            with self.subTest(options=options), self.fixture(**options) as (srv, origin):
                self.store(origin)
                self.quiet(self.run_cli(self.args(origin)), 1)
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot'])

    def test_query_snapshots_and_unrelated_metadata_obey_full_replacement(self) -> None:
        for early, snapshots, expected in [(True, [True], 0), (False, [True], 0),
                                           (True, [False], 1), (True, [True, False], 1), (False, [True, False], 1),
                                           (False, [False, True], 0), (False, ['other'], 1)]:
            with self.subTest(early=early, snapshots=snapshots), self.fixture(
                    early=early, snapshots=snapshots) as (srv, origin):
                self.store(origin)
                srv.allow_close.set()
                with self.child(self.args(origin, 'Peer'), srv) as (proc, fd):
                    if expected == 0:
                        self.ready(proc, fd)
                        proc.send_signal(signal.SIGTERM)
                    self.finish(proc, expected)
                    if expected:
                        self.no_ready(fd)

    def test_missing_query_snapshot_and_withheld_ack_cannot_publish_ready(self) -> None:
        for hold in [False, True]:
            with self.subTest(hold=hold), self.fixture(hold_ack=hold) as (srv, origin):
                self.store(origin)
                with self.child(self.args(origin, 'Peer'), srv) as (proc, fd):
                    self.barrier(srv, proc)
                    self.no_ready(fd)
                    srv.allow_close.set()
                    proc.send_signal(signal.SIGINT)
                    srv.release.set()
                    self.finish(proc, 0)
                    self.no_ready(fd)

    def test_status_fd_flags_direction_and_nonpipe_fail_before_root(self) -> None:
        with self.fixture() as (srv, origin), contextlib.ExitStack() as stack:
            read_fd, write_fd = os.pipe()
            stack.callback(os.close, read_fd)
            stack.callback(os.close, write_fd)
            file_fd = stack.enter_context((self.base / 'status').open('wb')).fileno()
            for fd in [write_fd, read_fd, file_fd]:
                os.set_blocking(read_fd, False)
                self.quiet(self.run_cli([*self.args(origin), '-R', str(fd)], pass_fds=(fd,)), 1)
            self.assertFalse(os.get_blocking(read_fd), 'caller input flags changed')
            self.assertTrue(os.get_blocking(write_fd), 'caller output flags changed')
            self.quiet(self.run_cli([*self.args(origin), '-E'], stdin=subprocess.DEVNULL), 1)
            self.assertFalse(self.root.exists())
            self.assertEqual(srv.paths, [])

    def test_full_and_broken_status_pipe_release_root_without_ready(self) -> None:
        for full in [True, False]:
            with self.subTest(full=full), self.fixture() as (srv, origin), contextlib.ExitStack() as stack:
                store = self.store(origin)
                before = store.read_bytes()
                read_fd, write_fd = os.pipe()
                stack.callback(os.close, write_fd)
                os.set_blocking(write_fd, False)
                if full:
                    stack.callback(os.close, read_fd)
                    os.set_blocking(read_fd, False)
                    while True:
                        try:
                            os.write(write_fd, b'x' * 4096)
                        except BlockingIOError:
                            break
                else:
                    os.close(read_fd)
                srv.allow_close.set()
                self.quiet(self.run_cli([*self.args(origin), '-R', str(write_fd)], pass_fds=(write_fd,)), 1)
                if full:
                    self.assertNotIn(b'READY', os.read(read_fd, 1048576))
                self.assertTrue(store.read_bytes() == before, 'store changed')
                self.quiet(self.run_cli(['init', '-d', str(self.root), '-n', 'NetA', '-c', '#Room']), 0)

    def test_explicit_eof_data_ignored_stdin_and_held_http_signal(self) -> None:
        for data, code in [(b'', 0), (b'unsupported', 1)]:
            with self.subTest(data=bool(data)), self.fixture() as (srv, origin):
                read_fd, write_fd = os.pipe()
                try:
                    os.set_blocking(read_fd, False)
                    os.write(write_fd, data)
                    os.close(write_fd)
                    write_fd = -1
                    self.quiet(self.run_cli([*self.args(origin), '-E'], stdin=read_fd), code)
                    self.assertEqual(srv.paths, [])
                finally:
                    os.close(read_fd)
                    if write_fd >= 0:
                        os.close(write_fd)
        with self.fixture(hold_http=True) as (srv, origin):
            store = self.store(origin)
            before = store.read_bytes()
            with self.child(self.args(origin), srv) as (proc, fd):
                self.barrier(srv, proc)
                proc.send_signal(signal.SIGTERM)
                srv.release.set()
                self.finish(proc, 0)
                self.no_ready(fd)
            self.assertTrue(store.read_bytes() == before, 'store changed')
        with self.fixture() as (srv, origin):
            self.store(origin)
            self.quiet(self.run_cli(['init', '-d', str(self.root), '-n', 'NetA', '-c', '#Room']), 0)
            logs = list(self.root.rglob('out'))
            self.assertTrue(logs, 'legacy projection missing output log')
            log = logs[0]
            log.write_bytes(b'preserved prefix\n')
            inode = log.stat().st_ino
            with self.child(self.args(origin), srv, stdin=subprocess.PIPE) as (proc, fd):
                assert proc.stdin is not None
                proc.stdin.write(b'unsupported\n'); proc.stdin.flush()
                self.ready(proc, fd)  # Buffered stdin ignored without -E.
                srv.allow_close.set()
                proc.send_signal(signal.SIGINT)
                self.finish(proc, 0)
            self.assertEqual(log.stat().st_ino, inode)
            self.assertEqual(log.read_bytes(), b'preserved prefix\n')
            self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                                         '/socket/websocket?vsn=2.0.0&client_proto=4'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
