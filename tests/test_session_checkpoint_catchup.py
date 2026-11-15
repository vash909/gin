"""Actual three-PID checkpoint restart characterization, not retroactive TDD RED.

Independent snapshots supply ID18 then ID20 while live ID19 then ID21 is staged.
The third retained visible history is only ID20: no historical-gap, durability,
exactly-once, or complete-history claim is made.
"""
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import shlex
import signal
import stat
import struct
import subprocess
import threading
import time
from typing import cast
import unittest
from urllib.parse import parse_qs, urlsplit

import test_session_reconnect as reconnect
import test_session_liveness as live


@dataclass
class HistoryState:
    generation: int = 1
    instrumented: bool = False
    history_requested: threading.Event = field(default_factory=threading.Event)
    release_history: threading.Event = field(default_factory=threading.Event)
    history_emitted: threading.Event = field(default_factory=threading.Event)


class Handler(reconnect.Handler):
    states: dict[reconnect.Server, HistoryState] = {}

    def do_GET(self):
        srv = cast(reconnect.Server, self.server)
        parsed = urlsplit(self.path)
        if parsed.path == '/networks/NetA/channels/%23Room/messages':
            state = self.states[srv]
            assert self.headers.get('Authorization') == 'Bearer ' + live.startup.TOKEN
            query = parse_qs(parsed.query, strict_parsing=True)
            assert set(query) == {'limit', 'after'}
            assert query['after'] == (['18'] if state.generation == 3 else ['0'])
            assert len(query['limit']) == 1 and 1 <= int(query['limit'][0]) <= 200
            assert srv.paths[-2:] == ['/api/config', '/me'], 'history lacks fresh identity preflight'
            srv.paths.append(self.path)
            if state.generation == 1:
                rows = []
            else:
                assert not state.history_requested.is_set(), 'unexpected repeated history GET'
                state.history_requested.set()
                assert state.release_history.wait(120 if state.instrumented else 8), 'held history fixture budget exhausted'
                if srv.stopped.is_set():
                    self.close_connection = True
                    return
                anchor = 18 if state.generation == 2 else 20
                rows = [dict(id=anchor, server_time=0, network='NetA', channel='#Room',
                             kind='action', sender='Nick', body=f'\x01ACTION recovered{anchor}')]
            raw = json.dumps(rows).encode()
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Content-Length', str(len(raw)))
            self.end_headers()
            self.wfile.write(raw)
            self.wfile.flush()
            if state.generation > 1:
                state.history_emitted.set()  # Server emission, NOT client receipt.
            return
        super().do_GET()


class CheckpointCatchup(reconnect.ReconnectFixture):
    handler_class = Handler

    def until(self, proc, predicate, message, seconds=3):
        deadline = time.monotonic() + (max(seconds, 90) if live.startup.PREFIX else seconds)
        while not predicate():
            self.assertIsNone(proc.poll(), 'SETUP/OWNER BLOCKER: ' + message)
            self.assertLess(time.monotonic(), deadline, message)
            time.sleep(.02)

    def checkpoint(self, output, anchor):
        path = output.with_name('.gin.checkpoint')
        if not path.exists():
            return False
        metadata = path.lstat()
        self.assertTrue(stat.S_ISREG(metadata.st_mode))
        self.assertEqual(stat.S_IMODE(metadata.st_mode), 0o600)
        self.assertEqual(metadata.st_nlink, 1)
        self.assertLessEqual(metadata.st_size, 8192)
        data = path.read_bytes()
        self.assertEqual(data[:8], b'GINCP\x00\x00\x01')
        self.assertGreaterEqual(len(data), 56)
        saved, unknown, device, inode, size, keylen = struct.unpack_from('>6Q', data, 8)
        self.assertEqual(len(data), 56 + keylen)
        current = output.stat()
        return ((saved, unknown) == (anchor, 1) and
                (device, inode, size) == (current.st_dev, current.st_ino, current.st_size))

    def test_actual_cli_restart_stages_live_until_unknown_prefix_catchup(self):
        binary = live.REPO / '.build/gin'
        selected = Path(os.environ.get('GIN_SESSION_BINARY', str(binary)))
        qualified_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
        self.assertEqual(hashlib.sha256(selected.read_bytes()).hexdigest(), qualified_hash,
                         'ordinary qualified gin bytes are required, including installed overrides')
        instrumented = bool(live.startup.PREFIX)
        stop_budget = 60 if instrumented else 5
        output_budget = 30 if instrumented else 3
        with self.fixture('positive') as (srv, origin):
            state = HistoryState(instrumented=instrumented)
            Handler.states[srv] = state
            proc = None
            try:
                srv.persisted_id = 17
                root = self.certdir / 'checkpoint-catchup'
                initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                             capture_output=True, timeout=stop_budget)
                self.assertEqual(initialized.returncode, 0, repr(initialized.stderr))
                output = root / '%4Eet%41' / 'room' / 'out'
                self.assertEqual(output.read_bytes(), b'')
                output.write_bytes(live.PREFIX_BYTES)  # Seed once, before all three owners.
                inode = output.stat().st_ino
                lock_inode = (root / '.gin.lock').stat().st_ino
                credential = root / '.gin.credentials'
                credential.write_text(json.dumps(dict(version=1, origin=origin, token=live.startup.TOKEN,
                                                     subject_id=live.startup.UUID, subject_name='Alice')))
                credential.chmod(0o600)
                credential_bytes, credential_inode = credential.read_bytes(), credential.stat().st_ino
                settings = self.certdir / 'checkpoint-catchup-settings'
                policy = ('heartbeat_interval_ms=1000\nheartbeat_timeout_ms=3000\n' if instrumented else
                          'heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\n')
                policy += 'max_retries=0\n'
                if instrumented:
                    policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
                settings.write_text(policy)
                settings.chmod(0o600)
                command = self.command('connect', '-f', settings, '-d', root, '-n', 'NetA', '-c', '#Room',
                                       '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem')
                expected = live.PREFIX_BYTES + live.LINE
                print('CHARACTERIZATION ACTUAL CLI: ' + shlex.join(command), flush=True)
                print(f'QUALIFIED BINARY SHA256={qualified_hash}', flush=True)
                proc = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                pids = [proc.pid]
                self.until(proc, lambda: srv.joins == live.TOPICS, 'SETUP: first required joins', 5)
                srv.release_app.set()
                self.wait_output(proc, srv, output, expected, output_budget)
                self.until(proc, lambda: self.checkpoint(output, 0), 'SETUP: first actual CP0 publication')
                print(f'FIRST CP0: PID={proc.pid}, ID17 output received; unknown=1', flush=True)
                for generation, previous_anchor, anchor, live_id in ((2, 0, 18, 19), (3, 18, 20, 21)):
                    srv.client_close_expected.set()
                    proc.send_signal(signal.SIGTERM)
                    stdout, stderr = proc.communicate(timeout=stop_budget)
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                    self.assertTrue(srv.finished[generation - 2].wait(30 if instrumented else 3),
                                    'SETUP: prior WS handler teardown missing')
                    self.assertIsNotNone(proc.poll(), 'SETUP: prior PID not reaped')
                    print(f'PID={proc.pid} EXIT=0 REAPED; WS HANDLER TEARDOWN VERIFIED', flush=True)
                    # Reset only after both prior-owner and handler teardown barriers.
                    for name in ('release_app', 'app_emitted', 'ready_observed', 'heartbeat_seen',
                                 'client_closed', 'client_close_expected'):
                        getattr(srv, name).clear()
                    srv.heartbeat_refs.clear()
                    srv.heartbeats_after_ready = 0
                    state.generation = generation
                    state.history_requested.clear()
                    state.release_history.clear()
                    state.history_emitted.clear()
                    srv.persisted_id = live_id
                    self.assertEqual(output.read_bytes(), expected)
                    self.assertTrue(self.checkpoint(output, previous_anchor))
                    proc = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    self.assertNotIn(proc.pid, pids)
                    pids.append(proc.pid)
                    self.until(proc, lambda: srv.joins == live.TOPICS * generation,
                               f'SETUP: generation {generation} required joins', 5)
                    self.assertEqual(srv.ws_attempts, generation)
                    self.assertFalse(srv.errors, repr(srv.errors))
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                    self.assertEqual((credential.read_bytes(), credential.stat().st_ino),
                                     (credential_bytes, credential_inode))
                    print(f'GENERATION={generation} PID={proc.pid} JOINED; same origin/root/inodes/credential; '
                          f'actual saved CP={previous_anchor}', flush=True)
                    srv.release_app.set()
                    self.until(proc, srv.app_emitted.is_set, f'SETUP: live ID{live_id} server emission')
                    deadline = time.monotonic() + (90 if instrumented else 3)
                    while not state.history_requested.is_set() and output.read_bytes() == expected:
                        self.assertIsNone(proc.poll(), 'SETUP: owner stopped before recovery phase')
                        self.assertLess(time.monotonic(), deadline,
                                        f'MISSING CATCHUP: no bounded after={previous_anchor} history GET')
                        time.sleep(.02)
                    print(f'RECOVERY: after={previous_anchor} history_GET={state.history_requested.is_set()}, '
                          f'ordinary_append={output.read_bytes() != expected}', flush=True)
                    self.assertEqual(output.read_bytes(), expected,
                                     f'MISSING CATCHUP: live ID{live_id} escaped before history GET/staging')
                    self.assertTrue(state.history_requested.is_set(), 'MISSING CATCHUP: no history GET')
                    time.sleep(.35)
                    self.assertFalse(state.history_emitted.is_set())
                    self.assertEqual(output.read_bytes(), expected,
                                     f'live ID{live_id} escaped while authenticated history response held')
                    state.release_history.set()
                    recovered = (f'[1970-01-01T00:00:00.000Z] action sender="Nick" '
                                 f'body="recovered{anchor}"\n').encode()
                    expected += recovered + live.LINE
                    self.wait_output(proc, srv, output, expected, output_budget)
                    self.until(proc, lambda: self.checkpoint(output, anchor),
                               f'safe checkpoint must use REST prefix{anchor}, not maximum live{live_id}')
                    print(f'CP={anchor} unknown=1; prior bytes unchanged + REST{anchor} + live{live_id}', flush=True)
                srv.client_close_expected.set()
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=stop_budget)
                self.assertEqual((proc.returncode, stdout, stderr), (0, b'', b''))
                self.assertTrue(srv.finished[2].wait(30 if instrumented else 3), 'third handler teardown missing')
                self.assertEqual(output.read_bytes(), expected)
                self.assertTrue(self.checkpoint(output, 20))
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual((credential.read_bytes(), credential.stat().st_ino),
                                 (credential_bytes, credential_inode))
                self.assertFalse(srv.errors, repr(srv.errors))
                print(f'THREE DISTINCT PIDS={pids}; all EXIT=0; final CP20 unknown=1', flush=True)
            finally:
                # Reap failure cleanup BEFORE unblocking any held HTTP response.
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    stdout, stderr = proc.communicate(timeout=stop_budget)
                    print(f'FINAL OWNER: returncode={proc.returncode}, stdout={stdout!r}, stderr={stderr!r}',
                          flush=True)
                srv.stopped.set()
                state.release_history.set()
                del Handler.states[srv]


if __name__ == '__main__':
    unittest.main(verbosity=2)
