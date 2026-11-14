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
import subprocess
import sys
import threading
import time
import unittest

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests'))
import test_session as startup
from ws_fixture_frames import server_frame, take_client_frame

TEXT = 'hello from FIFO'
PREFIX = b'existing prefix\n'
BEFORE = b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="before-fifo"\n'
AFTER = b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="after-fifo"\n'


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
            settings = self.certdir / 'outbound-settings'
            settings.write_text('connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n' if startup.PREFIX else '')
            settings.chmod(0o600)
            proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                '-R', ready_write), pass_fds=(ready_write,), stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(ready_write)
            writer = -1
            try:
                self.assertTrue(select.select([ready_read], [], [], budget(8))[0], 'no correlated READY')
                self.assertEqual(os.read(ready_read, 128), b'READY\n')
                self.wait_output(proc, output, BEFORE)
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
                data = output.read_bytes()
                self.assertTrue(data.startswith(PREFIX + BEFORE))
                self.assertLess(data.index(BEFORE), data.index(AFTER))
                self.assertNotIn(TEXT.encode(), data, 'outgoing input became a fake incoming record')
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual(list(root.rglob('out')), [output], 'nonselected output fan-out')
                self.assertEqual(len(srv.posts), 1, 'outgoing operation replayed')
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                    '/socket/websocket?vsn=2.0.0&client_proto=36', '/api/config', '/me',
                    '/networks/NetA/channels/%23Room/messages'])
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=budget(12))
                self.assertEqual(proc.returncode, 0)
                self.assertEqual(stdout + stderr, b'')
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


if __name__ == '__main__':
    unittest.main()
