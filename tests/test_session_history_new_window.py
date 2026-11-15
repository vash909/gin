"""Quiet new selected window under a valid private existing network parent.

One actual CLI owner, no live application frame; after=0 must precede READY.
This does not cover missing/unsafe network parents or repair existing logs.
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

import test_session_history_initial as initial
import test_session_checkpoint_catchup as catchup
import test_session_reconnect as reconnect


class NewWindowHistory(reconnect.ReconnectFixture):
    handler_class = catchup.Handler
    until = initial.InitialHistory.until
    checkpoint = initial.InitialHistory.checkpoint

    def test_quiet_new_selected_window_history_precedes_ready(self):
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
            root = self.certdir / 'new-window-quiet-history'
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Other'),
                                         capture_output=True, timeout=stop_budget)
            self.assertEqual((initialized.returncode, initialized.stdout, initialized.stderr), (0, b'', b''))
            network = root / '%4Eet%41'
            output = network / 'room' / 'out'
            checkpoint = output.with_name('.gin.checkpoint')
            other = network / 'other' / 'out'
            self.assertTrue(network.is_dir())
            self.assertEqual(network.stat().st_mode & 0o777, 0o700)
            self.assertFalse(output.parent.exists(), 'SETUP: selected window must be absent')
            self.assertFalse(output.exists(), 'SETUP: selected output must be absent')
            self.assertEqual(other.read_bytes(), b'')
            other.write_bytes(live.PREFIX_BYTES)  # Seed only the empty, unselected old window.
            other_inode = other.stat().st_ino
            lock_inode = (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps(dict(version=1, origin=origin, token=reconnect.startup.TOKEN,
                                                 subject_id=reconnect.startup.UUID, subject_name='Alice')))
            credential.chmod(0o600)
            credential_bytes, credential_inode = credential.read_bytes(), credential.stat().st_ino
            settings = self.certdir / 'new-window-quiet-settings'
            policy = ('heartbeat_interval_ms=1000\nheartbeat_timeout_ms=3000\n' if instrumented else
                      'heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\n') + 'max_retries=0\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            settings.write_text(policy)
            settings.chmod(0o600)

            def contents():
                try:
                    return output.read_bytes()
                except FileNotFoundError:
                    return b''

            def preserved():
                self.assertEqual((other.read_bytes(), other.stat().st_ino), (live.PREFIX_BYTES, other_inode))
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual((credential.read_bytes(), credential.stat().st_ino),
                                 (credential_bytes, credential_inode))
                self.assertFalse(srv.app_emitted.is_set())

            rr, rw = os.pipe()
            os.set_blocking(rr, False)
            os.set_blocking(rw, False)
            proc = None
            try:
                command = self.command('connect', '-f', settings, '-d', root, '-n', 'NetA', '-c', '#Room',
                                       '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                print('NEW WINDOW ACTUAL CLI: ' + shlex.join(command), flush=True)
                print(f'QUALIFIED BINARY SHA256={qualified_hash}; NetA private parent; Room ABSENT; Other sentinel',
                      flush=True)
                proc = subprocess.Popen(command, pass_fds=(rw,), stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                rw = -1
                self.until(proc, lambda: srv.joins == live.TOPICS, 'SETUP: three native joins', 5)
                self.assertEqual(srv.ws_attempts, 1)
                self.assertFalse(srv.errors, repr(srv.errors))
                self.until(proc, lambda: state.history_requested.is_set() or bool(select.select([rr], [], [], 0)[0]),
                           'MISSING NEW-WINDOW HISTORY: neither after=0 GET nor readiness within budget')
                ready = os.read(rr, 128) if select.select([rr], [], [], 0)[0] else b''
                self.assertIn(ready, (b'', b'READY\n'), 'SETUP: invalid/closed readiness pipe')
                self.assertIsNone(proc.poll(), 'SETUP: owner exited before history/readiness phase')
                print(f'NEW WINDOW PHASE: PID={proc.pid}; joins={srv.joins!r}; '
                      f'after0_GET={state.history_requested.is_set()}; READY={ready!r}; '
                      f'output={contents()!r}; selected_exists={output.exists()}; '
                      f'application_emitted={srv.app_emitted.is_set()}; paths={srv.paths!r}', flush=True)
                self.assertNotEqual(ready, b'READY\n',
                                    'MISSING NEW-WINDOW HISTORY: READY escaped before quiet selected-window catchup')
                self.assertTrue(state.history_requested.is_set(), 'MISSING NEW-WINDOW HISTORY: no after=0 GET')
                self.assertEqual(contents(), b'')
                self.assertFalse(srv.release_app.is_set())
                preserved()
                time.sleep(1 if instrumented else .35)
                self.assertFalse(select.select([rr], [], [], 0)[0], 'READY escaped while history held')
                self.assertFalse(state.history_emitted.is_set())
                self.assertEqual(contents(), b'')
                self.assertFalse(checkpoint.exists(), 'checkpoint published before history completion')
                preserved()
                state.release_history.set()
                expected = b'[1970-01-01T00:00:00.000Z] action sender="Nick" body="recovered18"\n'
                self.until(proc, lambda: contents() == expected, 'REST18-only new-window output',
                           30 if instrumented else 3)
                self.until(proc, lambda: output.exists() and self.checkpoint(output, 18),
                           'REST18 CP18 unknown=1 matching output device/inode/size')
                self.assertTrue(select.select([rr], [], [], 90 if instrumented else 3)[0],
                                'READY missing after history output')
                self.assertEqual(os.read(rr, 128), b'READY\n')
                srv.ready_observed.set()
                self.assertNotEqual(output.stat().st_ino, other_inode, 'selected output aliases Other')
                preserved()
                print('NEW WINDOW COMPLETE: REST18 only; CP18 unknown=1 device/inode/size; READY after history',
                      flush=True)
            finally:
                # Always reap before unblocking a held HTTP response, even on RED.
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
                        print(f'FINAL OWNER: returncode={proc.returncode}, stdout={stdout!r}, stderr={stderr!r}',
                              flush=True)
                finally:
                    srv.stopped.set()
                    state.release_history.set()
                    os.close(rr)
                    if rw >= 0:
                        os.close(rw)
                # No lock-file reopen while the owner is alive.
                self.assertFalse(stop_failed, 'SETUP/CLEANUP BLOCKER: owner did not stop on SIGTERM')
                if proc is not None:
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                    self.assertTrue(srv.finished[0].wait(30 if instrumented else 3),
                                    'SETUP/CLEANUP BLOCKER: WS handler teardown missing')
                preserved()
                selected_before_probe = output.parent.exists(), output.exists(), contents()
                reacquired = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Other'),
                                            capture_output=True, timeout=stop_budget)
                self.assertEqual((reacquired.returncode, reacquired.stdout, reacquired.stderr), (0, b'', b''),
                                 'root lock not released')
                self.assertEqual((output.parent.exists(), output.exists(), contents()), selected_before_probe,
                                 'Other-only reacquire changed selected window')
                preserved()
                self.assertFalse(srv.errors, repr(srv.errors))
                print('CLEANUP: SIGTERM owner reaped; held handler/FDs released; Other bytes/inode, credentials '
                      'bytes/inode, root-lock inode preserved; Other-only root reacquired; no live frame', flush=True)
        self.assertEqual(hashlib.sha256(selected.read_bytes()).hexdigest(), qualified_hash)


if __name__ == '__main__':
    unittest.main(verbosity=2)
