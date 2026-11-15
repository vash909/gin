"""Initial validated missing-entry catchup on a quiet selected channel.

One real ordinary-CLI owner; no application frame is released. Missing checkpoint
entry under the existing initialized window is not missing-parent recovery.
"""
import hashlib
import json
import os
from pathlib import Path
import select
import shlex
import signal
import subprocess
import time
import unittest

import test_session_checkpoint_catchup as catchup
import test_session_reconnect as reconnect


class InitialHistory(reconnect.ReconnectFixture):
    handler_class = catchup.Handler
    checkpoint = catchup.CheckpointCatchup.checkpoint

    def until(self, proc, predicate, message, seconds=3):
        deadline = time.monotonic() + (90 if reconnect.startup.PREFIX else seconds)
        while not predicate():
            self.assertIsNone(proc.poll(), 'SETUP/OWNER BLOCKER: ' + message)
            self.assertLess(time.monotonic(), deadline, message)
            time.sleep(.01)

    def test_missing_validated_entry_catches_up_quiet_channel_before_ready(self):
        live = reconnect.live
        instrumented = bool(reconnect.startup.PREFIX)
        stop_budget = 60 if instrumented else 5
        selected = Path(os.environ.get('GIN_SESSION_BINARY', str(reconnect.REPO / '.build/gin')))
        qualified_hash = hashlib.sha256((reconnect.REPO / '.build/gin').read_bytes()).hexdigest()
        self.assertEqual(hashlib.sha256(selected.read_bytes()).hexdigest(), qualified_hash,
                         'SETUP: ordinary qualified gin bytes required')
        with self.fixture('positive') as (srv, origin):
            state = catchup.HistoryState(generation=2, instrumented=instrumented)
            catchup.Handler.states[srv] = state
            self.addCleanup(catchup.Handler.states.pop, srv, None)
            root = self.certdir / 'initial-quiet-history'
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                         capture_output=True, timeout=stop_budget)
            self.assertEqual((initialized.returncode, initialized.stdout, initialized.stderr), (0, b'', b''))
            output = root / '%4Eet%41' / 'room' / 'out'
            checkpoint = output.with_name('.gin.checkpoint')
            self.assertTrue(output.parent.is_dir())
            self.assertEqual(output.read_bytes(), b'')
            output.write_bytes(live.PREFIX_BYTES)  # Only seed an empty initialized output.
            self.assertFalse(checkpoint.exists(), 'SETUP: checkpoint entry must be missing before connect')
            inode = output.stat().st_ino
            lock_inode = (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps(dict(version=1, origin=origin, token=reconnect.startup.TOKEN,
                                                 subject_id=reconnect.startup.UUID, subject_name='Alice')))
            credential.chmod(0o600)
            credential_bytes, credential_inode = credential.read_bytes(), credential.stat().st_ino
            settings = self.certdir / 'initial-quiet-settings'
            policy = ('heartbeat_interval_ms=1000\nheartbeat_timeout_ms=3000\n' if instrumented else
                      'heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\n') + 'max_retries=0\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            settings.write_text(policy)
            settings.chmod(0o600)
            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = None
            try:
                command = self.command('connect', '-f', settings, '-d', root, '-n', 'NetA', '-c', '#Room',
                                       '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                print('INITIAL QUIET ACTUAL CLI: ' + shlex.join(command), flush=True)
                print(f'QUALIFIED BINARY SHA256={qualified_hash}; initialized parent; missing entry; no live application release',
                      flush=True)
                proc = subprocess.Popen(command, pass_fds=(rw,), stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                rw = -1
                self.until(proc, lambda: srv.joins == live.TOPICS, 'SETUP: three native joins', 5)
                self.assertEqual(srv.ws_attempts, 1)
                self.assertFalse(srv.errors, repr(srv.errors))
                # Race the actual readiness receipt against history, never wait for
                # READY first: a correct owner withholds it while HTTP is held.
                self.until(proc, lambda: state.history_requested.is_set() or bool(select.select([rr], [], [], 0)[0]),
                           'MISSING INITIAL HISTORY: neither after=0 history nor readiness within budget')
                ready = os.read(rr, 128) if select.select([rr], [], [], 0)[0] else b''
                self.assertIn(ready, (b'', b'READY\n'), 'SETUP: invalid/closed readiness pipe')
                self.assertIsNone(proc.poll(), 'SETUP: owner exited before history/readiness phase')
                self.assertEqual(output.read_bytes(), live.PREFIX_BYTES)
                self.assertFalse(srv.release_app.is_set())
                self.assertFalse(srv.app_emitted.is_set())
                print(f'INITIAL PHASE: PID={proc.pid}; joins={len(srv.joins)}; '
                      f'after0_GET={state.history_requested.is_set()}; READY={ready!r}; '
                      f'prefix_unchanged=True; application_emitted=False; paths={srv.paths!r}', flush=True)
                self.assertNotEqual(ready, b'READY\n',
                                    'MISSING INITIAL HISTORY: READY escaped before quiet-channel after=0 catchup')
                self.assertTrue(state.history_requested.is_set(), 'MISSING INITIAL HISTORY: no after=0 GET')
                # Authenticated GET is held by the independent REST fixture.
                time.sleep(.35 if not instrumented else 1)
                self.assertFalse(select.select([rr], [], [], 0)[0], 'READY escaped while history held')
                self.assertFalse(state.history_emitted.is_set())
                self.assertEqual(output.read_bytes(), live.PREFIX_BYTES)
                self.assertFalse(checkpoint.exists(), 'checkpoint published before history completion')
                self.assertFalse(srv.app_emitted.is_set())
                state.release_history.set()
                expected = live.PREFIX_BYTES + b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="recovered18"\n'
                self.wait_output(proc, srv, output, expected, 30 if instrumented else 3)
                self.until(proc, lambda: self.checkpoint(output, 18), 'REST18 CP18 unknown=1 publication')
                self.assertTrue(select.select([rr], [], [], 90 if instrumented else 3)[0], 'READY missing after history output')
                self.assertEqual(os.read(rr, 128), b'READY\n')
                srv.ready_observed.set()
                self.assertFalse(srv.app_emitted.is_set())
                self.assertEqual(output.stat().st_ino, inode)
                print('INITIAL CATCHUP COMPLETE: REST18 output; CP18 unknown=1 matching output device/inode/size; READY consumed',
                      flush=True)
            finally:
                # Stop and reap BEFORE unblocking a held response on failure.
                srv.client_close_expected.set()
                stop_failed = False
                stdout = stderr = b''
                if proc is not None:
                    if proc.poll() is None:
                        proc.send_signal(signal.SIGTERM)
                    try:
                        stdout, stderr = proc.communicate(timeout=stop_budget)
                    except subprocess.TimeoutExpired:
                        proc.kill()
                        stdout, stderr = proc.communicate(timeout=5)
                        stop_failed = True
                    print(f'FINAL OWNER: returncode={proc.returncode}, stdout={stdout!r}, stderr={stderr!r}', flush=True)
                # Release held handlers and descriptors before any assertion
                # about the stopped owner, including unexpected failure codes.
                srv.stopped.set()
                state.release_history.set()
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
                self.assertFalse(stop_failed, 'SETUP/CLEANUP BLOCKER: owner did not stop on SIGTERM')
                if proc is not None:
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual((credential.read_bytes(), credential.stat().st_ino),
                                 (credential_bytes, credential_inode))
                self.assertFalse(srv.app_emitted.is_set())
                reacquired = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                            capture_output=True, timeout=stop_budget)
                self.assertEqual((reacquired.returncode, reacquired.stdout, reacquired.stderr), (0, b'', b''),
                                 'root lock not released')
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                print('CLEANUP: owner reaped; credentials/output/root-lock inodes preserved; root reacquired; quiet channel',
                      flush=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
