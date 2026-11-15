"""Actual same-PID Native socket-epoch history rearm; quiet second transport.

Initial empty REST history does not turn live ID17 into a durable prefix.
Two fresh heartbeat requests prove progress beyond the first new ACK, not
application receipt. Only output plus matching checkpoint is a receipt barrier.
"""
import hashlib
import json
import os
from pathlib import Path
import select
import shlex
import signal
import socket
import subprocess
import sys
import time
import unittest
from typing import cast

import test_session_checkpoint_catchup as catchup
import test_session_reconnect as reconnect


class EpochHistory(reconnect.ReconnectFixture):
    handler_class = catchup.Handler
    checkpoint = catchup.CheckpointCatchup.checkpoint

    def until(self, proc, predicate, message, seconds=3):
        deadline = time.monotonic() + (90 if reconnect.startup.PREFIX else seconds)
        while not predicate():
            self.assertIsNone(proc.poll(), 'SETUP/OWNER BLOCKER: ' + message)
            self.assertLess(time.monotonic(), deadline, 'SETUP/PHASE BLOCKER: ' + message)
            time.sleep(.01)

    def test_native_reconnect_rearms_quiet_history_in_same_pid(self):
        live = reconnect.live
        instrumented = bool(reconnect.startup.PREFIX)
        stop_budget = 60 if instrumented else 5
        binary = Path(os.environ.get('GIN_SESSION_BINARY', str(reconnect.REPO / '.build/gin')))
        digest = hashlib.sha256(binary.read_bytes()).hexdigest()
        self.assertEqual(digest, hashlib.sha256((reconnect.REPO / '.build/gin').read_bytes()).hexdigest(),
                         'SETUP: byte-identical qualified ordinary gin required')
        with self.fixture('positive') as (srv, origin):
            state = catchup.HistoryState(generation=1, instrumented=instrumented)
            catchup.Handler.states[srv] = state
            self.addCleanup(catchup.Handler.states.pop, srv, None)
            root = self.certdir / 'same-pid-history-epoch'
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                         capture_output=True, timeout=stop_budget)
            self.assertEqual((initialized.returncode, initialized.stdout, initialized.stderr), (0, b'', b''))
            output = root / '%4Eet%41' / 'room' / 'out'
            self.assertEqual(output.read_bytes(), b'')
            output.write_bytes(live.PREFIX_BYTES)
            cp = output.with_name('.gin.checkpoint')
            self.assertFalse(cp.exists())
            output_identity = (output.stat().st_dev, output.stat().st_ino)
            lock = root / '.gin.lock'
            lock_identity = (lock.stat().st_dev, lock.stat().st_ino)
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps(dict(version=1, origin=origin, token=reconnect.startup.TOKEN,
                                                 subject_id=reconnect.startup.UUID, subject_name='Alice')))
            credential.chmod(0o600)
            credential_identity = (credential.stat().st_dev, credential.stat().st_ino)
            credential_bytes = credential.read_bytes()
            other = self.certdir / 'unrelated-root'
            other.mkdir()
            sentinel = other / 'preserved'
            sentinel.write_bytes(b'unrelated root must remain unchanged\n')
            sentinel_identity = (sentinel.stat().st_dev, sentinel.stat().st_ino, sentinel.read_bytes())
            settings = self.certdir / 'same-pid-history-settings'
            policy = ('heartbeat_interval_ms=1000\nheartbeat_timeout_ms=3000\n' if instrumented else
                      'heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\n')
            # Match the established bounded reconnect fixture; this case tests
            # history rearming, not a long-backoff timeout.
            policy += 'retry_base_ms=20\nretry_cap_ms=40\nmax_retries=1\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
            policy += f'startup_ms={reconnect.startup.POSITIVE_SETUP_MS}\n'
            settings.write_text(policy)
            settings.chmod(0o600)
            srv.persisted_id = 17
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = None
            expected = live.PREFIX_BYTES
            saved_cp = None
            try:
                command = self.command('connect', '-f', settings, '-d', root, '-n', 'NetA', '-c', '#Room',
                                       '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                print('SAME-PID ACTUAL CLI: ' + shlex.join(command), flush=True)
                print('QUALIFIED BINARY SHA256=' + digest, flush=True)
                proc = subprocess.Popen(command, pass_fds=(rw,), stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                pid = proc.pid
                os.close(rw)
                rw = -1
                self.assertTrue(select.select([rr], [], [], 90 if instrumented else 5)[0],
                                'SETUP: first READY unavailable')
                self.assertEqual(os.read(rr, 128), b'READY\n', 'SETUP: first READY missing')
                self.assertEqual(srv.joins, live.TOPICS)
                self.assertEqual(srv.ws_attempts, 1)
                history_path = '/networks/NetA/channels/%23Room/messages?limit=200&after=0'
                self.assertEqual(srv.paths[-3:], ['/api/config', '/me', history_path])
                self.assertEqual(output.read_bytes(), expected)
                srv.ready_observed.set()
                srv.release_app.set()
                expected += live.LINE
                self.wait_output(proc, srv, output, expected, 30 if instrumented else 3)
                self.until(proc, lambda: self.checkpoint(output, 0), 'actual live17 output and CP0 publication')
                saved_cp = cp.read_bytes()
                print(f'FIRST RECEIPT PID={pid}; live17 output received; CP0 unknown=1; '
                      f'output_dev_ino_size={(output.stat().st_dev, output.stat().st_ino, output.stat().st_size)}', flush=True)
                # Only now change independent REST state. No new application frame
                # is released on the second transport, and no checkpoint is forged.
                state.generation = 2
                srv.release_app.clear()
                old_heartbeats = len(srv.heartbeat_refs)
                srv.client_close_expected.set()
                srv.active_ws.shutdown(socket.SHUT_RDWR)
                self.until(proc, lambda: srv.attempt_events[1].is_set() and srv.joins == live.TOPICS * 2,
                           'second actual Native transport and three fresh joins', 5)
                self.assertTrue(srv.finished[0].is_set(), 'old handler teardown barrier missing')
                self.assertEqual(srv.ws_attempts, 2)
                self.assertEqual(proc.pid, pid)
                self.assertIsNone(proc.poll(), 'same PID not alive after rejoin')
                # Handler owns teardown and reset ordering; do not clear its flags
                # ourselves. Its per-attempt counter is reset before second joins.
                self.until(proc, lambda: state.history_requested.is_set() or srv.heartbeats_after_ready >= 2,
                           'focused history GET or two fresh heartbeat requests', 3)
                print(f'SECOND PHASE PID={proc.pid}; alive={proc.poll() is None}; '
                      f'ws_attempts={srv.ws_attempts}; joins={srv.joins!r}; '
                      f'fresh_HB={srv.heartbeats_after_ready}; '
                      f'new_HB_refs={srv.heartbeat_refs[old_heartbeats:]!r}; '
                      f'after0_GET={state.history_requested.is_set()}; paths={srv.paths!r}', flush=True)
                self.assertFalse(select.select([rr], [], [], 0)[0], 'second READY published')
                self.assertEqual(output.read_bytes(), expected)
                self.assertEqual(cp.read_bytes(), saved_cp)
                self.assertFalse(srv.release_app.is_set())
                self.assertFalse(srv.app_emitted.is_set(), 'second application frame emitted unexpectedly')
                # Probe the held root lock from a distinct process, not by opening
                # or closing a process-associated lock descriptor in this owner.
                probe = subprocess.run([sys.executable, '-c',
                    'import fcntl,sys; f=open(sys.argv[1],"rb+")\n'
                    'try: fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)\n'
                    'except BlockingIOError: sys.exit(0)\n'
                    'sys.exit(1)', str(lock)], capture_output=True, timeout=stop_budget)
                self.assertEqual((probe.returncode, probe.stdout, probe.stderr), (0, b'', b''),
                                 'same owner no longer holds root lock')
                self.assertTrue(state.history_requested.is_set(),
                                'MISSING EPOCH HISTORY: rejoined Native transport serviced heartbeats without after=0 catchup')
                # Keep nonpublication observation below the original finite
                # startup episode; the independent handler stays held until
                # explicit release. A quiet interval must not consume the budget.
                time.sleep(.1 if instrumented else .05)
                self.assertIsNone(proc.poll(), 'owner stopped during held history observation')
                self.assertFalse(state.history_emitted.is_set())  # Emission is not receipt.
                self.assertEqual(output.read_bytes(), expected, 'output changed while REST response held')
                self.assertEqual(cp.read_bytes(), saved_cp, 'checkpoint changed while REST response held')
                self.assertFalse(select.select([rr], [], [], 0)[0], 'READY republished while history held')
                state.release_history.set()
                expected += b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="recovered18"\n'
                self.wait_output(proc, srv, output, expected, 30 if instrumented else 3)
                self.until(proc, lambda: self.checkpoint(output, 18), 'REST18 output and CP18 unknown=1 publication')
                self.assertEqual(proc.pid, pid)
                self.assertIsNone(proc.poll())
                self.assertFalse(select.select([rr], [], [], 0)[0], 'second READY after REST18')
                self.assertEqual(srv.joins, live.TOPICS * 2)
                self.assertEqual(srv.ws_attempts, 2)
                print(f'REST18 RECEIVED PID={pid}; CP18 unknown=1; no second live frame or READY', flush=True)
            finally:
                # On RED too: SIGTERM and reap before releasing held HTTP.
                srv.client_close_expected.set()
                stop_failed = False
                stdout = stderr = b''
                try:
                    if proc is not None:
                        if proc.poll() is None:
                            proc.send_signal(signal.SIGTERM)
                        try:
                            stdout, stderr = proc.communicate(timeout=stop_budget)
                        except subprocess.TimeoutExpired:
                            proc.kill()
                            stdout, stderr = proc.communicate(timeout=5)
                            stop_failed = True
                        print(f'FINAL OWNER PID={proc.pid}; exit={proc.returncode}; stdout={stdout!r}; stderr={stderr!r}; '
                              f'history_GET={state.history_requested.is_set()}; paths={srv.paths!r}', flush=True)
                finally:
                    srv.stopped.set()
                    state.release_history.set()
                    os.close(rr)
                    if rw >= 0:
                        os.close(rw)
                self.assertFalse(stop_failed, 'SETUP/CLEANUP BLOCKER: SIGTERM stop timed out')
                if proc is not None:
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                self.assertEqual(output.read_bytes(), expected)
                self.assertEqual((output.stat().st_dev, output.stat().st_ino), output_identity)
                self.assertEqual((lock.stat().st_dev, lock.stat().st_ino), lock_identity)
                self.assertEqual((credential.stat().st_dev, credential.stat().st_ino), credential_identity)
                self.assertEqual(credential.read_bytes(), credential_bytes)
                self.assertEqual((sentinel.stat().st_dev, sentinel.stat().st_ino, sentinel.read_bytes()), sentinel_identity)
                if saved_cp is not None and not state.history_emitted.is_set():
                    self.assertEqual(cp.read_bytes(), saved_cp)
                reacquired = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                            capture_output=True, timeout=stop_budget)
                self.assertEqual((reacquired.returncode, reacquired.stdout, reacquired.stderr), (0, b'', b''),
                                 'root lock not released after reap')
                self.assertEqual((lock.stat().st_dev, lock.stat().st_ino), lock_identity)
                print('CLEANUP: reaped before held-HTTP release; output/credentials/lock inodes and unrelated root preserved; root reacquired', flush=True)


class SlowInitialHistoryHandler(catchup.Handler):
    delayed_servers: set[reconnect.Server] = set()

    def do_GET(self):
        srv = cast(reconnect.Server, self.server)
        if self.path == '/api/config' and len(srv.paths) == 6:
            assert srv.ws_attempts == 1
            assert srv.joins == reconnect.live.TOPICS
            assert not srv.ready_observed.is_set()
            assert srv not in self.delayed_servers
            self.delayed_servers.add(srv)
            # Delay only the initial history preflight, never rearm or heartbeat.
            time.sleep(.650)
            if srv.stopped.is_set():
                self.close_connection = True
                return
        super().do_GET()


class SlowInitialHistoryEpoch(EpochHistory):
    handler_class = SlowInitialHistoryHandler

    def test_native_reconnect_rearms_quiet_history_in_same_pid(self):
        # Inherited class setup supplies this subclass its own certificate/root tree.
        self.addCleanup(SlowInitialHistoryHandler.delayed_servers.clear)
        try:
            super().test_native_reconnect_rearms_quiet_history_in_same_pid()
        finally:
            self.assertEqual(len(SlowInitialHistoryHandler.delayed_servers), 1,
                             'slow initial history preflight was not exercised')


if __name__ == '__main__':
    unittest.main(verbosity=2)
