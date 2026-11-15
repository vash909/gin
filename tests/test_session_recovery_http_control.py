"""Held recovery HTTPS after real ONLINE traffic, with one historical READY."""
import contextlib
import json
import os
import select
import signal
import socket
import subprocess
import sys
import threading
import time
import traceback
import unittest
from typing import cast
import test_session as startup
import test_session_liveness as live
import test_session_reconnect as reconnect
import test_session_checkpoint_catchup as catchup

BASE_PATHS = ['/api/config', '/me', '/boot', '/api/config', '/me',
              '/socket/websocket?vsn=2.0.0&client_proto=36', '/api/config', '/me',
              '/networks/NetA/channels/%23Room/messages?limit=200&after=0']
PHASES = ['/api/config', '/me', '/boot']


class Server(reconnect.Server):
    hold_path: str
    hold_armed: threading.Event
    request_seen: threading.Event
    release_response: threading.Event
    initial_delay_seen: threading.Event
    handler_tracebacks: list[str]


class Handler(reconnect.Handler):
    def do_GET(self):
        srv = cast(Server, self.server)
        if srv.hold_armed.is_set() and self.path == srv.hold_path:
            expected_auth = None if self.path == '/api/config' else 'Bearer ' + startup.TOKEN
            assert self.headers.get('Authorization') == expected_auth
            assert srv.ws_attempts == 1
            assert srv.paths == BASE_PATHS + PHASES[:PHASES.index(self.path)]
            srv.paths.append(self.path)
            srv.request_seen.set()
            # Never send headers/body, even at teardown. The child must be
            # killed/reaped before this response barrier is released.
            srv.release_response.wait(90)
            self.close_connection = True
            return
        super().do_GET()


class SlowInitialHistoryHandler(Handler):
    def do_GET(self):
        srv = cast(Server, self.server)
        if (self.path == '/api/config' and len(srv.paths) == 6 and srv.ws_attempts == 1
                and not srv.ready_observed.is_set() and not srv.hold_armed.is_set()):
            # Initial catch-up scheduling must not prevent the recovery test's setup.
            srv.initial_delay_seen.set()
            time.sleep(.65)
        super().do_GET()


class RecoveryHttpControl(reconnect.ReconnectFixture):
    checkpoint = catchup.CheckpointCatchup.checkpoint
    handler_class = Handler

    @contextlib.contextmanager
    def held_fixture(self, path):
        srv = None
        try:
            with self.fixture('positive') as (base, origin):
                srv = cast(Server, base)
                srv.hold_path = path
                srv.hold_armed, srv.request_seen, srv.release_response = [threading.Event() for _ in range(3)]
                srv.initial_delay_seen = threading.Event()
                srv.handler_tracebacks = []
                original_error = srv.handle_error

                def record_error(request, client_address):
                    assert srv is not None
                    srv.handler_tracebacks.append(traceback.format_exc().replace(startup.TOKEN, '<redacted>'))
                    original_error(request, client_address)  # Retain the strict server-error collector.

                srv.handle_error = record_error
                try:
                    yield srv, origin
                finally:
                    srv.release_response.set()
        finally:
            # Handler errors may arrive during the base fixture's shutdown.
            if srv is not None and srv.errors:
                print('RECOVERY HANDLER FAILURE: ' + json.dumps({
                    'paths': srv.paths, 'ready': srv.ready_observed.is_set(),
                    'hold_armed': srv.hold_armed.is_set(), 'traces': srv.handler_tracebacks}), flush=True)

    def run_held(self, path='/api/config', stop=False):
        instrumented = bool(startup.PREFIX)
        with self.held_fixture(path) as (srv, origin):
            label = self._testMethodName + '-' + path.strip('/').replace('/', '-') + ('-stop' if stop else '-deadline')
            root = self.certdir / ('recovery-http-' + label)
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output, = root.rglob('out')
            output.write_bytes(live.PREFIX_BYTES)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            # This configured budget applies to BOTH initial startup and recovery.
            # Keep native VM setup out of a 500 ms scheduling benchmark; recovery
            # must still expire within its own configured absolute episode.
            budget = 180000 if stop else 3000 if instrumented else 2000
            interval, heartbeat_timeout = (1000, 3000) if instrumented else (150, 500)
            config = self.certdir / ('recovery-http-settings-' + label)
            config.write_text(f'startup_ms={budget}\nconnect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
                              f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={heartbeat_timeout}\n'
                              'retry_base_ms=20\nretry_cap_ms=40\nmax_retries=2\n')
            config.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = None
            try:
                proc = subprocess.Popen(self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw),
                    pass_fds=(rw,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                rw = -1
                self.assertTrue(select.select([rr], [], [], 30 if instrumented else 5)[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                self.assertEqual(srv.joins, live.TOPICS)
                srv.ready_observed.set()
                self.assertTrue(srv.heartbeat_seen.wait(15 if instrumented else 3))
                srv.release_app.set()
                self.wait_output(proc, srv, output, live.PREFIX_BYTES + live.LINE, 30 if instrumented else 3)
                # An append alone does not establish checkpoint publication.
                deadline = time.monotonic() + (30 if instrumented else 3)
                while not self.checkpoint(output, 0):
                    self.assertIsNone(proc.poll(), 'owner stopped before checkpoint publication')
                    self.assertLess(time.monotonic(), deadline, 'checkpoint publication deadline')
                    srv.stopped.wait(.02)
                self.assertEqual(srv.paths, BASE_PATHS)
                old_tree = {str(p.relative_to(root)): p.stat().st_ino for p in root.rglob('*')}
                srv.hold_armed.set()
                srv.client_close_expected.set()
                recovery_started = time.monotonic()
                srv.active_ws.shutdown(socket.SHUT_RDWR)
                self.assertTrue(srv.request_seen.wait(15 if instrumented else 3),
                                'fresh recovery HTTPS phase was never entered')
                self.assertFalse(srv.release_response.is_set())
                if stop:
                    proc.send_signal(signal.SIGTERM)
                # Bound the complete recovery, not another full allowance measured
                # from entry into whichever held HTTP phase is selected.
                recovery_limit = recovery_started + budget / 1000 + (3 if instrumented else 1)
                remaining = (6 if instrumented else 2) if stop else max(.001, recovery_limit - time.monotonic())
                try:
                    stdout, stderr = proc.communicate(timeout=remaining)
                except subprocess.TimeoutExpired:
                    self.fail('owner remained blocked in recovery HTTPS after stop' if stop else
                              'owner remained blocked beyond recovery episode deadline while HTTPS response was held')
                self.assertFalse(srv.release_response.is_set())
                self.assertEqual(proc.returncode, 0 if stop else 1, repr(stderr))
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'' if stop else b'gin: startup timeout\n')
                if not stop:
                    self.assertLess(time.monotonic(), recovery_limit, 'original recovery episode budget exceeded')
                self.assertTrue(select.select([rr], [], [], 0)[0])
                self.assertEqual(os.read(rr, 128), b'', 'additional READY was published during recovery')
                self.assertEqual(srv.paths, BASE_PATHS + PHASES[:PHASES.index(path) + 1])
                self.assertEqual(srv.ws_attempts, 1)
                self.assertEqual(srv.joins, live.TOPICS)
                self.assertEqual(output.read_bytes(), live.PREFIX_BYTES + live.LINE)
                self.assertEqual(credential.read_bytes(), old_credential)
                self.assertEqual({str(p.relative_to(root)): p.stat().st_ino for p in root.rglob('*')}, old_tree)
            finally:
                semantic_failure = sys.exc_info()[0] is not None
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    stdout, stderr = proc.communicate(timeout=90 if instrumented else 10)
                    if semantic_failure:
                        print('RECOVERY OWNER FAILURE: ' + repr({
                            'exit': proc.returncode, 'stdout': stdout.replace(startup.TOKEN.encode(), b'<redacted>'),
                            'stderr': stderr.replace(startup.TOKEN.encode(), b'<redacted>'), 'paths': srv.paths,
                            'ready': srv.ready_observed.is_set(), 'hold_armed': srv.hold_armed.is_set()}), flush=True)
                srv.release_response.set()
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
            check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                   capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(check.returncode, 0, 'root lock not released')
            return srv.initial_delay_seen.is_set()

    def test_delayed_initial_catchup_preserves_recovery_deadline(self):
        self.handler_class = SlowInitialHistoryHandler
        self.assertTrue(self.run_held('/me'), 'delayed initial history preflight was not exercised')

    def test_held_recovery_config_obeys_original_episode_deadline(self):
        self.run_held()


    def test_held_recovery_identity_obeys_original_episode_deadline(self):
        self.run_held('/me')

    def test_held_recovery_boot_obeys_original_episode_deadline(self):
        self.run_held('/boot')

    def test_signal_during_held_recovery_config_stops_before_next_request(self):
        self.run_held(stop=True)

    def test_signal_during_held_recovery_identity_stops_before_next_request(self):
        self.run_held('/me', stop=True)

    def test_signal_during_held_recovery_boot_stops_before_next_request(self):
        self.run_held('/boot', stop=True)


if __name__ == '__main__':
    unittest.main()
