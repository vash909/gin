"""Heartbeat ACK processing and actual append while a mutation reply is held."""
import json
import os
import select
import signal
import subprocess
import threading
import time
import unittest
from typing import cast
import test_session as startup
import test_session_liveness as live
import test_session_outbound as outbound
import test_session_reconnect as reconnect


class Server(reconnect.Server):
    posted: threading.Event
    release_post: threading.Event
    posts: list[bytes]
    held_ref_count: int


class Handler(reconnect.Handler):
    mutation_method = 'POST'
    mutation_path = '/networks/NetA/channels/%23Room/messages'
    mutation_body = {'body': outbound.TEXT}
    response_body = {'id': 42, 'network': 'NetA'}
    response_status = 201

    def do_POST(self):
        self.mutation()

    def do_DELETE(self):
        self.mutation()

    def mutation(self):
        srv = cast(Server, self.server)
        assert self.command == self.mutation_method
        assert self.path == self.mutation_path
        assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
        length = int(self.headers.get('Content-Length', '0'))
        assert 0 <= length <= 65536
        raw = self.rfile.read(length)
        assert len(raw) == length
        if self.mutation_method == 'DELETE':
            assert length == 0 and self.headers.get('Transfer-Encoding') is None
        else:
            assert length > 0 and json.loads(raw) == self.mutation_body
        srv.paths.append(self.path)
        srv.posts.append(raw)
        srv.held_ref_count = len(srv.heartbeat_refs)
        srv.posted.set()
        assert srv.release_post.wait(90), 'held POST barrier expired'
        if srv.stopped.is_set():
            self.close_connection = True
            return
        body = json.dumps(self.response_body).encode()
        self.send_response(self.response_status)
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()


class LiveHttpTracer(reconnect.ReconnectFixture):
    handler_class = Handler

    def read_operation(self, fd, timeout):
        self.assertTrue(select.select([fd], [], [], timeout)[0], 'operation receipt missing')
        return os.read(fd, 4096)

    def run_held(self, stop=False, join=False, part=False, preflight=None):
        instrumented = bool(startup.PREFIX)
        self.assertFalse(join and part)
        self.assertTrue(preflight is None or (stop and preflight in ('/api/config', '/me')))
        kind = b'PART' if part else b'JOIN' if join else b'SEND'
        with self.fixture('positive') as (base, origin):
            srv = cast(Server, base)
            srv.posted, srv.release_post = threading.Event(), threading.Event()
            srv.posts, srv.held_ref_count = [], 0
            root = self.certdir / ('live-held-' + kind.decode().lower() + ('-stop' if stop else '') +
                                   ('-preflight-' + preflight.strip('/').replace('/', '-') if preflight else ''))
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(initialized.returncode, 0, repr(initialized.stderr))
            output, = root.rglob('out')
            output.write_bytes(live.PREFIX_BYTES)
            inode = output.stat().st_ino
            lock_inode = (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            interval, timeout = (1000, 3000) if instrumented else (150, 500)
            settings = self.certdir / 'live-held-send-settings'
            settings.write_text(f'startup_ms={3000 if instrumented else 500}\nconnect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
                f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={timeout}\nmax_retries=0\n')
            settings.chmod(0o600)
            rr, rw = os.pipe()
            op_read, op_write = os.pipe()
            for fd in (rr, rw, op_read, op_write):
                os.set_blocking(fd, False)
            writer, proc = -1, None
            try:
                proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                    '-R', rw, '-O', op_write), pass_fds=(rw, op_write), stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                os.close(op_write)
                rw = op_write = -1
                self.assertTrue(select.select([rr], [], [], 30 if instrumented else 5)[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                srv.ready_observed.set()
                self.assertTrue(srv.heartbeat_seen.wait(15 if instrumented else 3))
                self.assertEqual(output.read_bytes(), live.PREFIX_BYTES)
                writer = os.open(output.with_name('in'), os.O_WRONLY | os.O_NONBLOCK)
                line = b'/part #Other\n' if part else b'/join #Other\n' if join else (outbound.TEXT + '\n').encode()
                self.assertLessEqual(len(line), os.fpathconf(writer, 'PC_PIPE_BUF'))
                self.assertEqual(os.write(writer, line), len(line))
                self.assertEqual(self.read_operation(op_read, 15 if instrumented else 3), b'OP1 1 ' + kind + b' PENDING 0\n')
                self.assertTrue(srv.posted.wait(15 if instrumented else 3), 'held HTTP phase was not entered')
                self.assertFalse(srv.release_post.is_set())
                if stop:
                    srv.client_close_expected.set()
                    proc.send_signal(signal.SIGTERM)
                    stdout, stderr = proc.communicate(timeout=15 if instrumented else 3)
                    self.assertEqual(proc.returncode, 0, repr(stderr))
                    self.assertEqual(stdout + stderr, b'')
                    self.assertEqual(self.read_operation(op_read, 1), b'OP1 1 ' + kind +
                                     (b' NOT_ATTEMPTED ' if preflight else b' UNCERTAIN ') +
                                     (b'13\n' if join or part else b'11\n'))
                    self.assertFalse(srv.release_post.is_set(), 'stop depended on held response release')
                else:
                    srv.release_app.set()
                    deadline = time.monotonic() + (10 if instrumented else 2)
                    while len(srv.heartbeat_refs) < srv.held_ref_count + 2:
                        self.assertIsNone(proc.poll(), 'owner exited during held mutation')
                        self.assertLess(time.monotonic(), deadline,
                            f'owner did not process heartbeat ACKs while {self.handler_class.mutation_method} response was held')
                        srv.stopped.wait(.02)
                    # Fresh subsequent refs prove ACK processing, not just fixture emission.
                    deadline = time.monotonic() + (10 if instrumented else 2)
                    while output.read_bytes() != live.PREFIX_BYTES + live.LINE:
                        self.assertIsNone(proc.poll(), 'owner exited before held-POST incoming receipt')
                        self.assertLess(time.monotonic(), deadline,
                            'incoming application record not appended while POST response was held')
                        srv.stopped.wait(.02)
                    self.assertFalse(srv.release_post.is_set())
                    self.assertFalse(select.select([op_read], [], [], 0)[0], 'terminal receipt preceded held response')
                    srv.release_post.set()
                    self.assertEqual(self.read_operation(op_read, 15 if instrumented else 3),
                                     b'OP1 1 ' + kind + (b' ACCEPTED_PENDING 0\n' if join or part else b' ACCEPTED_PERSISTED 0\n'))
                self.assertEqual(len(srv.posts), 0 if preflight else 1, 'unexpected mutation or replay')
                self.assertEqual(srv.joins, live.TOPICS)
                self.assertEqual(srv.ws_attempts, 1)
                baseline = ['/api/config', '/me', '/boot', '/api/config', '/me',
                            '/socket/websocket?vsn=2.0.0&client_proto=36',
                            '/api/config', '/me',
                            '/networks/NetA/channels/%23Room/messages?limit=200&after=0']
                suffix = (['/api/config'] if preflight == '/api/config' else ['/api/config', '/me']) if preflight else [
                    '/api/config', '/me', self.handler_class.mutation_path]
                self.assertEqual(srv.paths, baseline + suffix)
                if preflight:
                    self.assertEqual(output.read_bytes(), live.PREFIX_BYTES)
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual(credential.read_bytes(), old_credential)
                self.assertNotIn(outbound.TEXT.encode(), output.read_bytes(), 'local echo')
                if not stop:
                    srv.client_close_expected.set()
                    proc.send_signal(signal.SIGTERM)
                    stdout, stderr = proc.communicate(timeout=15 if instrumented else 3)
                    self.assertEqual(proc.returncode, 0, repr(stderr))
                    self.assertEqual(stdout + stderr, b'')
                self.assertEqual(os.read(op_read, 4096), b'', 'duplicate terminal operation')
                self.assertEqual(os.read(rr, 128), b'', 'duplicate READY')
            finally:
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    proc.communicate(timeout=90 if instrumented else 10)
                srv.stopped.set()
                srv.release_post.set()
                if writer >= 0:
                    os.close(writer)
                for fd in (rr, rw, op_read, op_write):
                    if fd >= 0:
                        os.close(fd)
            checked = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(checked.returncode, 0, 'root lock not released')


    def test_held_send_keeps_heartbeat_and_actual_incoming_append_live(self):
        self.run_held()

    def test_signal_after_complete_post_publishes_uncertain_before_graceful_stop(self):
        self.run_held(stop=True)


if __name__ == '__main__':
    unittest.main()
