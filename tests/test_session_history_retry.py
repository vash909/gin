"""Real same-PID retry during held initial history, before first READY."""
import hashlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import ssl
import subprocess
import sys
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock
from typing import Any, cast

import test_session_checkpoint_catchup as catchup
import test_session_reconnect as reconnect


class HeldRequest:
    def __init__(self, anchor):
        self.anchor = anchor
        self.entered, self.release, self.finished = (threading.Event() for _ in range(3))
        self.cancelled = threading.Event()
        self.thread = None
        self.emitted = False


class Handler(reconnect.Handler):
    def do_GET(self):
        srv = cast(Any, self.server)
        if self.path.startswith('/networks/NetA/channels/%23Room/messages'):
            assert self.path == '/networks/NetA/channels/%23Room/messages?limit=200&after=0'
            assert self.headers.get('Authorization') == 'Bearer ' + reconnect.startup.TOKEN
            assert srv.paths[-2:] == ['/api/config', '/me'], 'missing fresh identity preflight'
            index = len(srv.history)
            assert index < 2, 'unexpected third history request'
            request = srv.requests[index]  # Immutable per-request generation, never shared release state.
            srv.history.append(request)
            srv.paths.append(self.path)
            request.thread = threading.current_thread()
            request.entered.set()
            try:
                assert request.release.wait(25), 'held HTTP fixture deadline'
                if srv.stopped.is_set():
                    return
                raw = json.dumps([dict(id=request.anchor, server_time=0, network='NetA',
                    channel='#Room', kind='action', sender='Nick',
                    body=f'\x01ACTION recovered{request.anchor}')]).encode()
                self.send_response(200)
                self.send_header('Content-Length', str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)
                self.wfile.flush()
                request.emitted = True  # Not a client receipt.
            except (BrokenPipeError, ConnectionResetError):
                assert index == 0 or srv.stopped.is_set(), 'new history transport failed'
            except ssl.SSLEOFError:
                if index != 0 or not request.cancelled.is_set():
                    raise
            finally:
                self.close_connection = True
                request.finished.set()
            return
        super().do_GET()

    def do_POST(self):
        cast(Any, self.server).posts.append(self.path)
        raise AssertionError('unexpected outbound application POST')


class HeldRequestTransportGuard(unittest.TestCase):
    def exercise(self, index, cancelled, error, allowed=False, strict=False):
        requests = [HeldRequest(18), HeldRequest(20)]
        request = requests[index]
        if cancelled:
            request.cancelled.set()
        request.release.set()
        server = SimpleNamespace(requests=requests, history=requests[:index],
            paths=['/api/config', '/me'], stopped=threading.Event())
        handler = cast(Any, Handler.__new__(Handler))
        handler.server = server
        handler.path = '/networks/NetA/channels/%23Room/messages?limit=200&after=0'
        handler.headers = {'Authorization': 'Bearer ' + reconnect.startup.TOKEN}
        handler.request_version = 'HTTP/1.1'
        handler.requestline = 'GET ' + handler.path + ' HTTP/1.1'
        handler.command = 'GET'
        handler.wfile = Mock()
        handler.wfile.write.side_effect = error
        if allowed:
            handler.do_GET()
        elif strict:
            with self.assertRaisesRegex(AssertionError, 'new history transport failed'):
                handler.do_GET()
        else:
            with self.assertRaises(type(error)) as caught:
                handler.do_GET()
            self.assertIs(caught.exception, error)
        handler.wfile.write.assert_called_once()  # Real end_headers/flush_headers path.
        self.assertTrue(request.entered.is_set())
        self.assertTrue(request.finished.is_set())
        self.assertTrue(handler.close_connection)
        self.assertFalse(request.emitted)

    def test_cancelled_first_tls_eof_allowed(self):
        self.exercise(0, True, ssl.SSLEOFError('injected EOF'), allowed=True)

    def test_cancellation_marker_is_request_local_and_initially_clear(self):
        first, second = HeldRequest(18), HeldRequest(20)
        self.assertFalse(first.cancelled.is_set())
        self.assertFalse(second.cancelled.is_set())
        first.cancelled.set()
        self.assertFalse(second.cancelled.is_set())

    def test_first_tls_eof_before_marker_fails(self):
        self.exercise(0, False, ssl.SSLEOFError('injected EOF'))

    def test_second_tls_eof_fails_even_if_marked(self):
        for marked in (False, True):
            with self.subTest(marked=marked):
                self.exercise(1, marked, ssl.SSLEOFError('injected EOF'))

    def test_unrelated_errors_fail_even_if_cancelled(self):
        for error in (ssl.SSLError('other TLS'), OSError('other IO'), ValueError('other failure')):
            with self.subTest(error=type(error).__name__):
                self.exercise(0, True, error)

    def test_existing_first_broken_transport_allowed_without_marker(self):
        for error in (BrokenPipeError('pipe'), ConnectionResetError('reset')):
            with self.subTest(error=type(error).__name__):
                self.exercise(0, False, error, allowed=True)

    def test_existing_second_broken_transport_still_fails(self):
        for error in (BrokenPipeError('pipe'), ConnectionResetError('reset')):
            with self.subTest(error=type(error).__name__):
                self.exercise(1, True, error, strict=True)


class HistoryRetry(reconnect.ReconnectFixture):
    handler_class = Handler
    checkpoint = catchup.CheckpointCatchup.checkpoint

    def test_native_loss_during_initial_history_retries(self):
        live = reconnect.live
        instrumented = bool(reconnect.startup.PREFIX)
        deadline = time.monotonic() + (120 if instrumented else 35)
        binary = Path(os.environ.get('GIN_SESSION_BINARY', reconnect.REPO / '.build/gin'))
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        self.assertEqual(digest, hashlib.sha256((reconnect.REPO / '.build/gin').read_bytes()).hexdigest())
        print('BINARY SHA256=' + digest, flush=True)
        with self.fixture('positive') as (server, origin):
            srv = cast(Any, server)
            srv.requests = [HeldRequest(18), HeldRequest(20)]
            srv.history, srv.posts = [], []
            first, second = srv.requests
            root = self.certdir / 'initial-history-retry'
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=5)
            self.assertEqual((init.returncode, init.stdout, init.stderr), (0, b'', b''))
            output = root / '%4Eet%41' / 'room' / 'out'
            self.assertEqual(output.read_bytes(), b'')
            output.write_bytes(live.PREFIX_BYTES)
            cp, lock = output.with_name('.gin.checkpoint'), root / '.gin.lock'
            self.assertFalse(cp.exists(), 'validated missing checkpoint required')
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps(dict(version=1, origin=origin, token=reconnect.startup.TOKEN,
                subject_id=reconnect.startup.UUID, subject_name='Alice')))
            credential.chmod(0o600)
            sentinel = self.certdir / 'unrelated-sentinel'
            sentinel.write_bytes(b'unchanged unrelated root sentinel\n')
            identities = {p: (p.stat().st_dev, p.stat().st_ino) for p in (root, output, lock, credential, sentinel)}
            saved_credential, saved_sentinel = credential.read_bytes(), sentinel.read_bytes()
            settings = self.certdir / 'initial-history-retry-settings'
            settings.write_text(('heartbeat_interval_ms=1000\nheartbeat_timeout_ms=3000\n'
                'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=12000\n' if instrumented else
                'heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\nstartup_ms=2000\n') +
                'retry_base_ms=20\nretry_cap_ms=40\nmax_retries=1\n')
            settings.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc, expected, positive = None, live.PREFIX_BYTES, False

            def until(predicate, message):
                assert proc is not None
                while not predicate():
                    self.assertIsNone(proc.poll(), message)
                    self.assertLess(time.monotonic(), deadline, message)
                    time.sleep(.005)

            try:
                proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root,
                    '-n', 'NetA', '-c', '#Room', '-e', origin, '-m', 'plain',
                    '-a', self.certdir / 'ca.pem', '-R', rw), pass_fds=(rw,),
                    stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                pid = proc.pid
                os.close(rw)
                rw = -1
                until(first.entered.is_set, 'SETUP: initial held history GET not entered')
                self.assertEqual(srv.joins, live.TOPICS)
                self.assertEqual(srv.ws_attempts, 1)
                self.assertFalse(select.select([rr], [], [], 0)[0], 'READY before initial history')
                self.assertEqual(output.read_bytes(), expected)
                self.assertFalse(cp.exists())
                probe = subprocess.run([sys.executable, '-c',
                    'import fcntl,sys; f=open(sys.argv[1],"rb+")\n'
                    'try: fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)\n'
                    'except BlockingIOError: sys.exit(0)\n'
                    'sys.exit(1)', str(lock)], capture_output=True, timeout=3)
                self.assertEqual((probe.returncode, probe.stdout, probe.stderr), (0, b'', b''))
                print(f'FIRST HELD PID={pid}; three joins; after0; CP absent; no READY; child lock probe blocked', flush=True)
                srv.client_close_expected.set()
                srv.active_ws.shutdown(socket.SHUT_RDWR)  # Real WSS loss, independent held HTTP untouched.
                self.assertTrue(srv.finished[0].wait(2), 'actual first Native handler close missing')
                print('FIRST NATIVE TRANSPORT CLOSED; old HTTP still held=' + str(not first.release.is_set()), flush=True)
                until(lambda: second.entered.is_set(),
                    'MISSING HISTORY RETRY: Native loss during held catchup stopped owner despite remaining retry budget')
                self.assertEqual(proc.pid, pid)
                self.assertEqual(srv.ws_attempts, 2)
                self.assertEqual(srv.joins, live.TOPICS * 2)
                self.assertFalse(first.finished.is_set(), 'old HTTP must remain independently held')
                self.assertEqual(output.read_bytes(), expected)
                self.assertFalse(cp.exists())
                self.assertFalse(select.select([rr], [], [], 0)[0])
                second.release.set()
                expected += b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="recovered20"\n'
                until(lambda: output.read_bytes() == expected and self.checkpoint(output, 20), 'REST20 receipt/CP20 missing')
                until(lambda: bool(select.select([rr], [], [], 0)[0]), 'first READY missing')
                self.assertEqual(os.read(rr, 128), b'READY\n')
                saved_cp = cp.read_bytes()
                # Only the superseded request may encounter the known TLS close.
                first.cancelled.set()
                first.release.set()
                self.assertTrue(first.finished.wait(2), 'old HTTP handler did not finish')
                time.sleep(.1)
                self.assertEqual(output.read_bytes(), expected, 'old REST18 escaped after new publication')
                self.assertEqual(cp.read_bytes(), saved_cp, 'stale response advanced checkpoint')
                self.assertFalse(select.select([rr], [], [], 0)[0], 'duplicate READY')
                self.assertFalse(srv.posts)
                self.assertFalse(srv.app_emitted.is_set())
                self.assertIsNone(proc.poll())
                positive = True
                print(f'RETRY PASS PID={pid}; two Native epochs; REST20/CP20 unknown1; one READY; stale18 rejected', flush=True)
            finally:
                semantic_failure = sys.exc_info()[0] is not None
                srv.client_close_expected.set()
                stdout = stderr = b''
                if proc is not None:
                    if proc.poll() is None:
                        proc.send_signal(signal.SIGTERM)
                    try:
                        stdout, stderr = proc.communicate(timeout=3)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        stdout, stderr = proc.communicate(timeout=3)
                    redacted = stderr.replace(reconnect.startup.TOKEN.encode(), b'<redacted>')
                    print(f'OWNER REAPED PID={proc.pid}; exit={proc.returncode}; stdout={stdout!r}; stderr={redacted!r}; paths={srv.paths!r}', flush=True)
                srv.stopped.set()
                for request in srv.requests:
                    request.release.set()
                    if request.thread is not None:
                        request.thread.join(3)
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
                preserved = (output.read_bytes() == expected and credential.read_bytes() == saved_credential
                    and sentinel.read_bytes() == saved_sentinel and all(
                    (p.stat().st_dev, p.stat().st_ino) == identity for p, identity in identities.items()))
                reacquired = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                           capture_output=True, timeout=3)
                joined = all(r.thread is None or not r.thread.is_alive() for r in srv.requests)
                print(f'CLEANUP: handlers_joined={joined}; identities/credentials/sentinel/output_preserved={preserved}; '
                      f'root_reacquired={reacquired.returncode == 0}; checkpoint_absent={not cp.exists()}', flush=True)
                if not semantic_failure:
                    assert proc is not None
                    self.assertTrue(positive and preserved and joined)
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                    self.assertEqual((reacquired.returncode, reacquired.stdout, reacquired.stderr), (0, b'', b''))


if __name__ == '__main__':
    unittest.main(verbosity=2)
