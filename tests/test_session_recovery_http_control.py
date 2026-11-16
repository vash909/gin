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
from owner_clock_fixture import OwnerClock

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
    clock_negative: bool
    warmup_seen: threading.Event
    release_warmup: threading.Event
    cancel_warmup: threading.Event


class Handler(reconnect.Handler):
    def do_GET(self):
        srv = cast(Server, self.server)
        if (srv.hold_armed.is_set() and srv.clock_negative and srv.hold_path != '/api/config'
                and self.path == '/api/config'):
            assert srv.ready_observed.is_set(), 'warmup preceded actual initial READY'
            assert self.headers.get('Authorization') is None
            assert srv.ws_attempts == 1 and srv.paths == BASE_PATHS
            assert not srv.warmup_seen.is_set(), 'duplicate recovery config warmup'
            srv.paths.append(self.path)
            srv.warmup_seen.set()
            assert srv.release_warmup.wait(90), 'recovery config warmup was not released'
            if srv.cancel_warmup.is_set():
                self.close_connection = True
                return
            raw = json.dumps({'server': 'grappa', 'protocol_version': 36,
                              'min_protocol_version': 1}).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            self.wfile.flush()
            return
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
    def held_fixture(self, path, clock_negative=False):
        srv = None
        try:
            with self.fixture('positive') as (base, origin):
                srv = cast(Server, base)
                srv.hold_path = path
                srv.clock_negative = clock_negative
                srv.hold_armed, srv.request_seen, srv.release_response = [threading.Event() for _ in range(3)]
                srv.warmup_seen, srv.release_warmup, srv.cancel_warmup = [threading.Event() for _ in range(3)]
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
                    srv.cancel_warmup.set()
                    srv.release_warmup.set()
        finally:
            # Handler errors may arrive during the base fixture's shutdown.
            if srv is not None and srv.errors:
                print('RECOVERY HANDLER FAILURE: ' + json.dumps({
                    'paths': srv.paths, 'ready': srv.ready_observed.is_set(),
                    'hold_armed': srv.hold_armed.is_set(), 'traces': srv.handler_tracebacks}), flush=True)

    def run_held(self, path='/api/config', stop=False):
        instrumented = bool(startup.PREFIX)
        with self.held_fixture(path, clock_negative=not stop) as (srv, origin):
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
            # Fixed virtual episode; ordinary stop cases retain real clocks.
            # Setup no longer races a short native/JIT wall budget.
            budget = 180000
            interval, heartbeat_timeout = (1000, 3000) if instrumented else (150, 500)
            config = self.certdir / ('recovery-http-settings-' + label)
            config.write_text(f'startup_ms={budget}\nconnect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
                              f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={heartbeat_timeout}\n'
                              'retry_base_ms=20\nretry_cap_ms=40\nmax_retries=2\n')
            config.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc, clock = None, None
            try:
                command = self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                if not stop:
                    clock = OwnerClock(self)
                    command[len(startup.PREFIX)] = os.environ.get(
                        'GIN_OWNER_BINARY', str(reconnect.REPO / '.build/gin_owner_fault'))
                proc = subprocess.Popen(command, env=clock.environment() if clock else None,
                    pass_fds=(rw, *(clock.child_fds if clock else ())), stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if clock:
                    clock.parent_after_spawn()
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
                if clock and path != '/api/config':
                    self.assertTrue(srv.warmup_seen.wait(15 if instrumented else 3),
                                    'real recovery config warmup was not entered')
                    self.assertFalse(srv.release_warmup.is_set())
                    self.assertFalse(srv.request_seen.is_set(), 'target phase preceded clock warmup')
                    clock.advance(proc, 'A', 30 if instrumented else 5)
                    self.assertLess(time.monotonic() - recovery_started, 60,
                                    'setup exhausted the original virtual warmup margin')
                    self.assertIsNone(proc.poll(), 'owner expired during pre-deadline warmup')
                    self.assertFalse(select.select([rr], [], [], 0)[0], 'warmup published READY or exited')
                    self.assertFalse(srv.request_seen.is_set())
                    srv.release_warmup.set()
                self.assertTrue(srv.request_seen.wait(15 if instrumented else 3),
                                'fresh recovery HTTPS phase was never entered')
                self.assertFalse(srv.release_response.is_set())
                if stop:
                    proc.send_signal(signal.SIGTERM)
                else:
                    assert clock is not None
                    clock.advance(proc, 'E' if path == '/api/config' else 'B', 30 if instrumented else 5)
                # ACK commits virtual time, not termination. Bound exit separately
                # with the real parent clock, never a new app allowance per phase.
                termination_limit = time.monotonic() + (6 if instrumented else 2)
                try:
                    stdout, stderr = proc.communicate(timeout=max(.001, termination_limit - time.monotonic()))
                except subprocess.TimeoutExpired:
                    self.fail('owner remained blocked in recovery HTTPS after stop' if stop else
                              'owner remained blocked beyond recovery episode deadline while HTTPS response was held')
                self.assertFalse(srv.release_response.is_set())
                self.assertEqual(proc.returncode, 0 if stop else 1, repr(stderr))
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'' if stop else b'gin: startup timeout\n')
                if not stop:
                    self.assertLess(time.monotonic(), termination_limit, 'owner ignored virtual episode expiry')
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
                if clock:
                    clock.close()
                srv.cancel_warmup.set()  # Cleanup release only after owner kill/reap.
                srv.release_warmup.set()
                srv.release_response.set()
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
            check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                   capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(check.returncode, 0, 'root lock not released')
            return srv.initial_delay_seen.is_set()

    def test_delayed_initial_catchup_keeps_ordinary_recovery_stop_responsive(self):
        self.handler_class = SlowInitialHistoryHandler
        self.assertTrue(self.run_held('/me', stop=True), 'delayed initial history preflight was not exercised')

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
