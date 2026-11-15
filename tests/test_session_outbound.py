"""Real connected CLI: selected FIFO -> HTTPS while verified WSS keeps receiving."""
from pathlib import Path
from typing import Any, cast
import base64
import contextlib
import errno
import hashlib
import json
import os
import select
import signal
import stat
import struct
import subprocess
import sys
import threading
import time
import unittest
import tempfile
from unittest import mock


class CheckpointObserverGuards(unittest.TestCase):
    """Native filesystem guards; injected scheduling/uid/clock is labelled."""

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=REPO / '.build')
        self.addCleanup(self.directory.cleanup)
        self.output = Path(self.directory.name) / 'out'
        self.output.write_bytes(b'expected')
        self.checkpoint = self.output.with_name('.gin.checkpoint')
        self.proc = mock.Mock()
        self.proc.poll.return_value = None
        self.publish()

    def publish(self, data=None):
        metadata = self.output.stat()
        if data is None:
            data = b'GINCP\x00\x00\x01' + struct.pack('>6Q', 0, 1,
                metadata.st_dev, metadata.st_ino, 8, 1) + b'k'
        temporary = self.checkpoint.with_name('replacement')
        temporary.write_bytes(data)
        temporary.chmod(0o600)
        temporary.replace(self.checkpoint)

    def observe(self, timeout=.15):
        wait_checkpoint(self, self.proc, self.output, b'expected', timeout)

    def test_real_rename_retries_retired_descriptor(self):
        # Inject scheduling only: fstat and rename use actual filesystem state.
        original = os.fstat
        calls = []
        def replacing(fd):
            if not calls:
                self.publish()
            result = original(fd)
            calls.append(result.st_nlink)
            return result
        with mock.patch.object(os, 'fstat', side_effect=replacing):
            self.observe()
        self.assertIn(0, calls, 'observer never pinned the retired generation')
        self.assertIn(1, calls, 'replacement was not independently observed')

    def test_unsafe_native_metadata_rejected_without_fifo_hang(self):
        for kind in ('hardlink', 'symlink', 'fifo', 'mode'):
            with self.subTest(kind=kind):
                self.checkpoint.unlink()
                self.publish()
                extra = self.checkpoint.with_name('extra')
                if kind == 'hardlink':
                    os.link(self.checkpoint, extra)
                elif kind == 'symlink':
                    self.checkpoint.rename(extra)
                    self.checkpoint.symlink_to(extra)
                elif kind == 'fifo':
                    self.checkpoint.unlink()
                    os.mkfifo(self.checkpoint, 0o600)
                else:
                    self.checkpoint.chmod(0o644)
                if kind == 'fifo':
                    # A parent deadline catches a blocking-open regression; elapsed
                    # time measured after observe() returns cannot detect a hang.
                    script = (
                        'import sys,unittest; from pathlib import Path; from unittest import mock; '
                        'from test_session_outbound import wait_checkpoint; '
                        'proc=mock.Mock(); proc.poll.return_value=None\n'
                        'try: wait_checkpoint(unittest.TestCase(),proc,Path(sys.argv[1]),b"expected",.15)\n'
                        'except (AssertionError,OSError): sys.exit(0)\n'
                        'sys.exit(1)\n')
                    checked = subprocess.run([sys.executable, '-c', script, str(self.output)],
                                             cwd=REPO / 'tests', capture_output=True, timeout=3)
                    self.assertEqual((checked.returncode, checked.stdout, checked.stderr), (0, b'', b''))
                else:
                    with self.assertRaises((AssertionError, OSError)):
                        self.observe()
                if extra.exists():
                    extra.unlink()

    def test_rename_after_read_metadata_retries(self):
        original = os.fstat
        links = []
        def replacing(fd):
            result = original(fd)
            links.append(result.st_nlink)
            if len(links) == 2:
                self.publish()  # Retire after capturing the post-read metadata.
            return result
        with mock.patch.object(os, 'fstat', side_effect=replacing):
            self.observe()
        self.assertIn(0, links, 'late retirement was not independently verified')

    def test_snapshot_descriptors_closed(self):
        original = os.open
        descriptors = []
        def opening(*args, **kwargs):
            fd = original(*args, **kwargs)
            descriptors.append(fd)
            return fd
        with mock.patch.object(os, 'open', side_effect=opening):
            self.observe()
        self.assertGreaterEqual(len(descriptors), 2)
        for fd in descriptors:
            with self.assertRaises(OSError) as caught:
                os.fstat(fd)
            self.assertEqual(caught.exception.errno, errno.EBADF)

    def test_owner_injection_rejected(self):
        with mock.patch.object(os, 'geteuid', return_value=os.geteuid() + 1):
            with self.assertRaises(AssertionError):
                self.observe()

    def test_bad_records_and_output_rejected(self):
        good = self.checkpoint.read_bytes()
        for data in (b'bad', b'x' * 8193, b'BADMAGIC' + good[8:],
                     good[:8] + struct.pack('>6Q', 1, 1, 0, 0, 8, 1) + b'k',
                     good[:-1] + startup.TOKEN.encode(),
                     good[:48] + struct.pack('>Q', 2) + b'k',
                     good[:24] + struct.pack('>Q', 0) + good[32:]):
            with self.subTest(data_length=len(data)):
                self.publish(data)
                with self.assertRaises(AssertionError):
                    self.observe()
        self.publish(good)
        self.output.write_bytes(b'not same')
        with self.assertRaises(AssertionError):
            self.observe()

    def test_replacement_retries_keep_deadline_and_owner_checks(self):
        original = os.fstat
        for stopped in (False, True):
            with self.subTest(owner_exit=stopped):
                self.proc.poll.reset_mock()
                self.publish()
                calls = []
                def replacing(fd):
                    self.publish()
                    calls.append(fd)
                    return original(fd)
                self.proc.poll.side_effect = (lambda: 1 if len(calls) >= 2 else None) if stopped else None
                # Clock injection bounds retry count without relying on CPU speed.
                ticks = iter(i * .01 for i in range(100))
                with mock.patch.object(os, 'fstat', side_effect=replacing), \
                     mock.patch.object(time, 'monotonic', side_effect=lambda: next(ticks)):
                    with self.assertRaisesRegex(AssertionError, 'owner stopped' if stopped else 'not published'):
                        self.observe(.08)
                self.assertGreaterEqual(len(calls), 2)
                # Bound lifetime checks, not an incidental number of fstat calls.
                self.assertGreaterEqual(self.proc.poll.call_count, 2)
                self.assertLessEqual(self.proc.poll.call_count, 8)

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests'))
import test_session as startup
from ws_fixture_frames import server_frame, take_client_frame

TEXT = 'hello from FIFO'
PREFIX = b'existing prefix\n'
BEFORE = b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="before-fifo"\n'
AFTER = b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="after-fifo"\n'


BOOT = ['/api/config', '/me', '/boot', '/api/config', '/me',
        '/socket/websocket?vsn=2.0.0&client_proto=36', '/api/config', '/me',
        '/networks/NetA/channels/%23Room/messages?limit=200&after=0']


def wait_checkpoint(test, proc, output, expected, timeout):
    # Pin one generation: a pathname stat and a later open are not a snapshot.
    checkpoint = output.with_name('.gin.checkpoint')
    deadline = time.monotonic() + timeout
    seen = False
    replacement = None

    def identity(metadata):
        return metadata.st_dev, metadata.st_ino

    def safe(metadata, links=1):
        test.assertTrue(stat.S_ISREG(metadata.st_mode))
        test.assertEqual(stat.S_IMODE(metadata.st_mode), 0o600)
        test.assertEqual(metadata.st_uid, os.geteuid())
        test.assertEqual(metadata.st_nlink, links)

    def generation(metadata):
        return (identity(metadata), metadata.st_mode, metadata.st_uid,
                metadata.st_size, metadata.st_mtime_ns, metadata.st_ctime_ns)

    def check_lifetime():
        test.assertIsNone(proc.poll(), 'owner stopped before checkpoint publication')
        test.assertLess(time.monotonic(), deadline, 'successful output checkpoint was not published')

    def current_generation():
        # A pathname lstat can itself return a just-retired inode with nlink=0.
        # Pin the current entry too; churn may retire these probes, but none is
        # accepted until a distinct independently safe linked generation exists.
        retired_identity = None
        while True:
            check_lifetime()
            probe = os.open(checkpoint, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
            try:
                current = os.fstat(probe)
                safe(current, links=0 if current.st_nlink == 0 else 1)
                if retired_identity is not None:
                    test.assertNotEqual(identity(current), retired_identity,
                                        'current checkpoint reopened retired generation')
                if current.st_nlink == 1:
                    return current
                retired_identity = identity(current)
            finally:
                os.close(probe)

    while True:
        # Replacement retries share the original deadline and owner lifetime.
        check_lifetime()
        try:
            fd = os.open(checkpoint, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        except FileNotFoundError:
            test.assertFalse(seen, 'checkpoint disappeared after observation')
            time.sleep(.01)
            continue
        seen = True
        try:
            before = os.fstat(fd)
            # A retired inode may have zero links, but is never accepted.
            safe(before, links=0 if before.st_nlink == 0 else 1)
            if replacement is not None:
                test.assertNotEqual(identity(before), replacement,
                                    'replacement retry reopened retired generation')
            chunks = bytearray()
            while len(chunks) < 8193:
                check_lifetime()
                chunk = os.read(fd, 8193 - len(chunks))
                if not chunk:
                    break
                chunks.extend(chunk)
            data = bytes(chunks)
            after = os.fstat(fd)
            safe(after, links=0 if after.st_nlink == 0 else 1)
            test.assertEqual((identity(before), before.st_mode, before.st_uid,
                              before.st_size, before.st_mtime_ns),
                             (identity(after), after.st_mode, after.st_uid,
                              after.st_size, after.st_mtime_ns))
            test.assertEqual(len(data), after.st_size)
            test.assertGreaterEqual(len(data), 56)
            test.assertLessEqual(len(data), 8192)
            test.assertEqual(data[:8], b'GINCP\x00\x00\x01')
            anchor, unknown, device, inode, size, key_size = struct.unpack_from('>6Q', data, 8)
            test.assertEqual((anchor, unknown), (0, 1))
            test.assertEqual(len(data), 56 + key_size)
            test.assertNotIn(startup.TOKEN.encode(), data)
            output_metadata = output.stat()
            test.assertEqual((device, inode), (output_metadata.st_dev, output_metadata.st_ino))
            test.assertLessEqual(size, len(expected), 'checkpoint advanced beyond expected output')
            current = current_generation()  # no symlink following, including FIFO
            if identity(current) != identity(after):
                # Rename may occur after the post-read fstat, before the probe.
                retired = os.fstat(fd)
                safe(retired, links=0)
                test.assertEqual(generation(after)[:-1], generation(retired)[:-1],
                                 'retired checkpoint changed beyond unlink metadata')
                replacement = identity(after)
                continue
            test.assertEqual(before.st_nlink, 1)
            test.assertEqual(after.st_nlink, 1)
            test.assertEqual(generation(before), generation(after), 'checkpoint changed during read')
            test.assertEqual(generation(after), generation(current), 'checkpoint path changed during read')
            replacement = None
            if size == len(expected):
                test.assertEqual(output.read_bytes(), expected)
                test.assertIsNone(proc.poll(), 'owner stopped before checkpoint publication')
                test.assertLess(time.monotonic(), deadline, 'successful output checkpoint was not published')
                return
        finally:
            os.close(fd)
        time.sleep(.01)


def budget(native):
    return 90 if startup.PREFIX else native


class OutboundServer(startup.Server):
    posts: list[bytes]
    posted: threading.Event
    release_after: threading.Event


class Handler(startup.Handler):
    def do_POST(self):
        srv = cast(Any, self.server)
        assert self.path == '/networks/NetA/channels/%23Room/messages', 'wrong wire destination'
        assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
        length = int(self.headers['Content-Length'])
        assert 0 < length <= 65536
        raw = self.rfile.read(length)
        assert json.loads(raw) == {'body': TEXT}, 'wrong outgoing body'
        srv.paths.append(self.path)
        srv.posts.append(raw)
        body = json.dumps({'id': 42, 'network': 'NetA'}).encode()
        self.send_response(201)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()
        srv.posted.set()

    def upgrade(self):
        srv = cast(Any, self.server)
        assert self.headers.get('Authorization') is None
        assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+'
        key = self.headers['Sec-WebSocket-Key']
        accept = base64.b64encode(hashlib.sha1(
            (key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        self.send_response(101)
        for name, value in [('Upgrade', 'websocket'), ('Connection', 'Upgrade'),
                            ('Sec-WebSocket-Accept', accept), ('Sec-WebSocket-Protocol', 'phoenix')]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.2)
        buffered = bytearray()

        def receive():
            deadline = time.monotonic() + budget(8)
            while time.monotonic() < deadline and not srv.stopped.is_set():
                frame = take_client_frame(buffered)
                if frame is not None:
                    return frame
                try:
                    data = self.connection.recv(16384)
                except TimeoutError:
                    continue
                if not data:
                    return None
                buffered.extend(data)
            return None

        try:
            topic = ''
            for index in range(3):
                item = receive()
                assert item is not None, 'closed before required subscription'
                opcode, final, raw = item
                assert opcode == 1 and final
                jr, ref, topic, event, payload = json.loads(raw)
                assert event == 'phx_join' and jr == ref and payload == {}
                srv.joins.append(topic)
                response = {'protocol_version': 36} if index == 0 else {}
                reply = [jr, ref, topic, 'phx_reply', {'status': 'ok', 'response': response}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
            assert topic == 'grappa:user:Alice/network:NetA/channel:#room'

            def incoming(text):
                row = {'network': 'NetA', 'channel': '#room', 'kind': 'action',
                       'sender': 'Nick', 'server_time': 0, 'body': '\x01ACTION ' + text}
                frame = [None, None, topic, 'event', {'kind': 'message', 'message': row}]
                self.connection.sendall(server_frame(json.dumps(frame).encode()))

            incoming('before-fifo')
            assert srv.posted.wait(budget(8)), 'message POST not observed'
            if srv.stopped.is_set():
                return
            assert srv.release_after.wait(budget(8)), 'after-message barrier expired'
            if srv.stopped.is_set():
                return
            incoming('after-fifo')
            while not srv.stopped.is_set():
                item = receive()
                if item is None:
                    return
                assert item[0] in (8, 10), 'unexpected application socket operation'
        finally:
            self.close_connection = True


class OutboundTracer(unittest.TestCase):
    certdir: Path
    tls = startup.StartupTracer.tls

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    def command(self, *args):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        return [*startup.PREFIX, binary, *map(str, args)]

    @contextlib.contextmanager
    def fixture(self):
        srv = OutboundServer(('127.0.0.1', 0), Handler)
        srv.paths, srv.joins, srv.errors, srv.posts = [], [], [], []
        for name in ['stopped', 'posted', 'release_after']:
            setattr(srv, name, threading.Event())
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.posted.set()
            srv.release_after.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'positive fixture handler failed')

    def wait_output(self, proc, path, marker):
        deadline = time.monotonic() + budget(8)
        while marker not in path.read_bytes():
            self.assertIsNone(proc.poll(), 'connected process exited before incoming projection')
            self.assertLess(time.monotonic(), deadline, 'incoming projection deadline')
            time.sleep(.02)

    def test_literal_fifo_send_preserves_live_receive_without_local_echo(self):
        with self.fixture() as (srv, origin):
            root = self.certdir / 'outbound-root'
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                         capture_output=True, timeout=budget(10))
            self.assertEqual(initialized.returncode, 0)
            outputs = list(root.rglob('out'))
            self.assertEqual(len(outputs), 1)
            output = outputs[0]
            output.write_bytes(PREFIX)
            inode = output.stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            original_credential = credential.read_bytes()
            ready_read, ready_write = os.pipe()
            os.set_blocking(ready_read, False)
            os.set_blocking(ready_write, False)
            operation_read, operation_write = os.pipe()
            os.set_blocking(operation_read, False)
            os.set_blocking(operation_write, False)
            self.operation_data = b''
            settings = self.certdir / 'outbound-settings'
            settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n' if startup.PREFIX else '')
            settings.chmod(0o600)
            proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                '-R', ready_write, '-O', operation_write), pass_fds=(ready_write, operation_write), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(ready_write)
            os.close(operation_write)
            writer = -1
            try:
                self.assertTrue(select.select([ready_read], [], [], budget(8))[0], 'no correlated READY')
                self.assertEqual(os.read(ready_read, 128), b'READY\n')
                self.wait_output(proc, output, BEFORE)
                wait_checkpoint(self, proc, output, PREFIX + BEFORE, budget(8))
                try:
                    writer = os.open(output.with_name('in'), os.O_WRONLY | os.O_NONBLOCK)
                except OSError as error:
                    if error.errno == errno.ENXIO:
                        self.fail('SEMANTIC_RED: connected CLI receives but has no selected-FIFO reader')
                    raise
                line = (TEXT + '\n').encode()
                self.assertLessEqual(len(line), os.fpathconf(writer, 'PC_PIPE_BUF'))
                self.assertEqual(os.write(writer, line), len(line))
                self.assertTrue(srv.posted.wait(budget(8)), 'real CLI did not issue the message POST')
                self.assertNotIn(TEXT.encode(), output.read_bytes(), 'input was echoed without a server event')
                srv.release_after.set()
                self.wait_output(proc, output, AFTER)
                wait_checkpoint(self, proc, output, PREFIX + BEFORE + AFTER, budget(8))
                data = output.read_bytes()
                self.assertTrue(data.startswith(PREFIX + BEFORE))
                self.assertLess(data.index(BEFORE), data.index(AFTER))
                self.assertNotIn(TEXT.encode(), data, 'outgoing input became a fake incoming record')
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual(list(root.rglob('out')), [output], 'nonselected output fan-out')
                self.assertEqual(len(srv.posts), 1, 'outgoing operation replayed')
                self.assertEqual(srv.paths, BOOT + ['/api/config', '/me',
                    '/networks/NetA/channels/%23Room/messages'])
                # Live reception may precede HTTP outcome classification. A
                # server write or incoming event is not a terminal SEND receipt.
                receipt_deadline = time.monotonic() + budget(8)
                while self.operation_data.count(b'\n') < 2:
                    remaining = receipt_deadline - time.monotonic()
                    self.assertGreater(remaining, 0, 'terminal SEND receipt deadline')
                    self.assertTrue(select.select([operation_read], [], [], remaining)[0],
                                    'terminal SEND receipt missing before stop')
                    chunk = os.read(operation_read, 4096)
                    self.assertTrue(chunk, repr(self.operation_data))
                    self.operation_data += chunk
                self.assertEqual(self.operation_data,
                    b'OP1 1 SEND PENDING 0\nOP1 1 SEND ACCEPTED_PERSISTED 0\n')
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=budget(12))
                self.assertEqual(proc.returncode, 0)
                self.assertEqual(stdout + stderr, b'')
                self.assertEqual(os.read(operation_read, 4096), b'', 'duplicate terminal operation')
                self.assertEqual(credential.read_bytes(), original_credential)
            finally:
                srv.stopped.set()
                srv.posted.set()
                srv.release_after.set()
                if writer >= 0:
                    os.close(writer)
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=budget(10))
                os.close(ready_read)
                os.close(operation_read)


if __name__ == '__main__':
    unittest.main()
