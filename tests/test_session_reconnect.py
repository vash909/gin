"""Same-process transport recovery; no history or uncertain-send replay claim."""
import contextlib
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import threading
import time
import unittest
from typing import cast

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / 'tests'))
import test_session as startup
import test_session_liveness as live


class Server(live.Server):
    ws_attempts: int
    initial_failures: int
    active_ws: socket.socket
    finished: list[threading.Event]
    attempt_events: list[threading.Event]


class Handler(live.Handler):
    attempt = 0

    def do_GET(self):
        srv = cast(Server, self.server)
        if (self.path == '/api/config' and
                ((srv.case == 'initial_close' and srv.paths.count('/api/config') == 1) or
                 (srv.case == 'topology_close' and not srv.paths) or
                 (srv.case == 'topology_sixteen' and srv.paths.count('/api/config') < 16))):
            # Drop exactly the selected initial owner or native preflight,
            # before any WSS exists. Recording the path prevents repetition.
            assert self.headers.get('Authorization') is None
            srv.paths.append(self.path)
            srv.initial_failures += 1
            self.close_connection = True
            return
        if self.path == '/boot' and srv.ws_attempts and srv.case in ('removed', 'wrong_network'):
            assert self.headers.get('Authorization') == 'Bearer ' + startup.TOKEN
            srv.paths.append(self.path)
            network_id = 3000000002 if srv.case == 'wrong_network' else 3000000001
            channels = [] if srv.case == 'removed' else [
                {'name': '#Room', 'joined': True, 'source': 'joined'}]
            body = {'networks': [{'id': network_id, 'slug': 'NetA', 'nick': 'OwnA'}],
                    'channels': {'NetA': channels}, 'heads': {}}
            raw = json.dumps(body).encode()
            self.send_response(200)
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            return
        super().do_GET()

    def upgrade(self):
        srv = cast(Server, self.server)
        srv.ws_attempts += 1
        self.attempt = srv.ws_attempts
        assert self.attempt <= 3, 'unexpected additional transport attempt'
        if self.attempt == 1 and srv.case == 'joining_close':
            srv.initial_failures += 1
        if self.attempt > 1:
            if srv.case == 'joining_close':
                srv.case = 'positive'
            assert srv.finished[self.attempt - 2].wait(5), 'old handler not torn down before reconnect'
            srv.client_close_expected.clear()
            srv.heartbeat_seen.clear()
            srv.heartbeats_after_ready = 0
            srv.app_emitted.clear()
        srv.active_ws = self.connection
        srv.attempt_events[self.attempt - 1].set()
        super().upgrade()

    def finish(self):
        try:
            super().finish()
        finally:
            srv = cast(Server, self.server)
            if 0 < self.attempt <= len(srv.finished):
                srv.finished[self.attempt - 1].set()


class ReconnectFixture(unittest.TestCase):
    handler_class = Handler
    certdir: Path
    tls = live.SessionLivenessTest.tls

    @classmethod
    def setUpClass(cls):
        live.SessionLivenessTest.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        live.SessionLivenessTest.tearDownClass.__func__(cls)

    def command(self, *args):
        binary = os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))
        return [*startup.PREFIX, binary, *map(str, args)]

    @contextlib.contextmanager
    def fixture(self, case):
        srv = Server(('127.0.0.1', 0), self.handler_class)
        for name in ['final_requested', 'release_final', 'stopped', 'client_closed',
                     'client_close_expected', 'ready_observed', 'heartbeat_seen',
                     'release_app', 'app_emitted', 'drop_observed']:
            setattr(srv, name, threading.Event())
        srv.paths, srv.joins, srv.errors, srv.heartbeat_refs, srv.dropped_refs = [], [], [], [], []
        srv.heartbeats_after_ready, srv.ws_attempts, srv.case = 0, 0, case
        srv.initial_failures = 0
        srv.finished = [threading.Event() for _ in range(3)]
        srv.attempt_events = [threading.Event() for _ in range(3)]
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.client_close_expected.set()
            srv.release_app.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'listener leaked')
            self.assertFalse(srv.errors, 'strict recovery fixture handler failure')

    def wait_output(self, proc, srv, output, expected, timeout):
        deadline = time.monotonic() + timeout
        while output.read_bytes() != expected:
            self.assertIsNone(proc.poll(), 'owner stopped before application receipt')
            self.assertLess(time.monotonic(), deadline, 'recovery application receipt deadline')
            srv.stopped.wait(.02)

    def run_case(self, case):
        instrumented = bool(startup.PREFIX)
        with self.fixture(case) as (srv, origin):
            root = self.certdir / ('reconnect-' + case)
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=60 if instrumented else 5)
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output = root / '%4Eet%41' / 'room' / 'out'
            output.write_bytes(live.PREFIX_BYTES)
            inode, lock_inode = output.stat().st_ino, (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                                             'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            original_credential = credential.read_bytes()
            config = self.certdir / 'reconnect-settings'
            interval, timeout, retry = (1000, 3000, 1000) if instrumented else (150, 500, 200)
            backoff_control = case.startswith('backoff_')
            if backoff_control:
                retry = 5000
            retry_limit = 16 if case == 'topology_sixteen' else 2
            if case == 'topology_sixteen':
                retry = 1
            policy = f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={timeout}\n'
            policy += f'retry_base_ms={retry}\nretry_cap_ms={retry * 2}\nmax_retries={retry_limit}\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            config.write_text(policy)
            config.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = None
            eof_read = eof_write = -1
            child_env = dict(os.environ)
            child_env.pop('GIN_OWNER_FAULT', None)
            child_env.pop('GIN_OWNER_EOF_FD', None)
            private_binary = os.environ.get('GIN_OWNER_BINARY', str(REPO / '.build/gin_owner_fault'))
            if backoff_control:
                child_env['GIN_OWNER_FAULT'] = case.replace('_', '-')
                if case != 'backoff_stop':
                    eof_read, eof_write = os.pipe()
                    os.set_blocking(eof_read, False)
                    os.set_blocking(eof_write, False)
                    child_env['GIN_OWNER_EOF_FD'] = str(eof_write)
            try:
                command = self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                if backoff_control:
                    command[len(startup.PREFIX)] = private_binary
                if eof_read >= 0:
                    command.append('-E')
                proc = subprocess.Popen(command, env=child_env,
                    pass_fds=(rw, eof_write) if eof_write >= 0 else (rw,),
                    stdin=eof_read if eof_read >= 0 else subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if eof_read >= 0:
                    os.close(eof_read)
                    eof_read = -1
                if eof_write >= 0:
                    os.close(eof_write)
                    eof_write = -1
                os.close(rw)
                rw = -1
                self.assertTrue(select.select([rr], [], [], 90 if instrumented else 5)[0])
                self.assertEqual(os.read(rr, 128), b'READY\n',
                    f'startup failed before READY: case={case}, paths={srv.paths}, injected={srv.initial_failures}')
                self.assertIsNone(proc.poll())
                joining_extra = int(case == 'joining_close')
                self.assertEqual(srv.joins, live.TOPICS[:1] * joining_extra + live.TOPICS)
                srv.ready_observed.set()
                self.assertTrue(srv.heartbeat_seen.wait(15 if instrumented else 3))
                srv.release_app.set()
                self.wait_output(proc, srv, output, live.PREFIX_BYTES + live.LINE, 30 if instrumented else 3)
                expected = live.PREFIX_BYTES + live.LINE
                final_code, final_error = 0, b''
                terminal = case in ('removed', 'wrong_network', 'exhaust', 'initial_close', 'joining_close', 'topology_close', 'topology_sixteen') or backoff_control
                for loss in range(3 if case == 'exhaust' else 2 if case in ('initial_close', 'joining_close', 'topology_close') else 1):
                    srv.release_app.clear()
                    srv.client_close_expected.set()
                    srv.active_ws.shutdown(socket.SHUT_RDWR)
                    final_loss = (case in ('removed', 'wrong_network', 'topology_sixteen') or
                                  (case == 'exhaust' and loss == 2) or
                                  (case in ('initial_close', 'joining_close', 'topology_close') and loss == 1) or backoff_control)
                    if final_loss:
                        final_code = 1
                        final_error = (b'gin: selected route unavailable\n'
                                       if case in ('removed', 'wrong_network') else b'gin: connection failure\n')
                        if backoff_control:
                            final_code = 1 if case == 'backoff_input' else 0
                            final_error = b'gin: unsupported session input\n' if case == 'backoff_input' else b''
                        break
                    deadline = time.monotonic() + (90 if instrumented else 8)
                    while not srv.attempt_events[loss + 1 + joining_extra].wait(.02):
                        self.assertIsNone(proc.poll(),
                                          'owner stopped rather than reconnecting after controlled transport loss')
                        self.assertLess(time.monotonic(), deadline, 'bounded reconnect deadline')
                    self.assertTrue(srv.heartbeat_seen.wait(15 if instrumented else 3),
                                    'new attempt heartbeat missing')
                    self.assertIsNone(proc.poll(), 'reconnected owner not live')
                    srv.release_app.set()
                    expected += live.LINE
                    self.wait_output(proc, srv, output, expected, 30 if instrumented else 3)
                    self.assertFalse(select.select([rr], [], [], 0)[0],
                                     'READY-only pipe republished on reconnect')
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                if not terminal:
                    srv.client_close_expected.set()
                    proc.send_signal(signal.SIGTERM)
                finish_budget = (15 if instrumented else 3) if backoff_control else (60 if instrumented else 12)
                stdout, stderr = proc.communicate(timeout=finish_budget)
                self.assertEqual(proc.returncode, final_code, repr(stderr))
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, final_error)
                attempts = 3 if case in ('exhaust', 'joining_close') else 1 if case in ('removed', 'wrong_network', 'topology_sixteen') or backoff_control else 2
                refresh_only = int(case in ('removed', 'wrong_network'))
                initial_extra = int(case == 'initial_close')
                topology_extra = 16 if case == 'topology_sixteen' else int(case == 'topology_close')
                self.assertEqual(srv.initial_failures, initial_extra + joining_extra + topology_extra)
                self.assertEqual(srv.joins, live.TOPICS[:1] * joining_extra + live.TOPICS * (attempts - joining_extra))
                self.assertEqual(srv.paths.count('/boot'), attempts + refresh_only + initial_extra,
                                 'topology reused or retry budget bypassed')
                # Initial and successfully rejoined epochs each fetch focused
                # history; a failed JOIN never reaches that HTTP phase.
                history_attempts = attempts - joining_extra
                self.assertEqual(srv.paths.count('/networks/NetA/channels/%23Room/messages?limit=200&after=0'),
                                 history_attempts, 'missing or repeated focused history')
                self.assertEqual(srv.paths.count('/api/config'), attempts * 2 + refresh_only + initial_extra * 2 + topology_extra + history_attempts)
                self.assertEqual(srv.paths.count('/me'), attempts * 2 + refresh_only + initial_extra + history_attempts)
                self.assertEqual(srv.ws_attempts, attempts)
                self.assertEqual(len(srv.paths), attempts * 6 + refresh_only * 3 + initial_extra * 4 + topology_extra + history_attempts * 3,
                                 'unexpected fan-out or mutation')
                self.assertEqual(credential.read_bytes(), original_credential)
                check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                       capture_output=True, timeout=60 if instrumented else 5)
                self.assertEqual(check.returncode, 0, 'root lock not released')
                self.assertEqual(output.read_bytes(), expected)
                self.assertEqual(output.stat().st_ino, inode)
            finally:
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        try:
                            proc.terminate()
                        except ProcessLookupError:
                            pass
                    try:
                        proc.communicate(timeout=60 if instrumented else 8)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        proc.communicate(timeout=10)
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
                if eof_read >= 0:
                    os.close(eof_read)
                if eof_write >= 0:
                    os.close(eof_write)


class ReconnectTracer(ReconnectFixture):
    def test_transport_loss_refreshes_topology_and_resubscribes_under_one_owner(self):
        self.run_case('positive')

    def test_retry_budget_is_not_reset_by_successful_rejoins(self):
        self.run_case('exhaust')

    def test_removed_selected_channel_stops_before_transport_retry(self):
        self.run_case('removed')

    def test_changed_network_identity_stops_before_transport_retry(self):
        self.run_case('wrong_network')



if __name__ == '__main__':
    unittest.main()
