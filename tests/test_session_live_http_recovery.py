"""In-flight mutation loss: terminal receipt before recovery, never replay HTTP."""
import json
import os
import select
import signal
import socket
import stat
import struct
import subprocess
import sys
import threading
import time
import unittest
from typing import cast

import test_session as startup
import test_session_liveness as live
import test_session_live_http as held
import test_session_outbound as outbound
import test_session_reconnect as reconnect

BOOT = ['/api/config', '/me', '/boot', '/api/config', '/me',
        '/socket/websocket?vsn=2.0.0&client_proto=36', '/api/config', '/me',
        '/networks/NetA/channels/%23Room/messages?limit=200&after=0']


def trace(event, **values):
    print(json.dumps({'event': event, 'monotonic': time.monotonic(), **values}), flush=True)


class Server(held.Server):
    reader_transferred: threading.Event
    recovery_checked: threading.Event
    op_reader: int
    terminal: bytes
    forbid_recovery: bool
    baseline: list[str]
    expected_terminal: bytes


class Handler(held.Handler):
    def do_GET(self):
        srv = cast(Server, self.server)
        if srv.reader_transferred.is_set() and not srv.recovery_checked.is_set():
            # Entry barrier: no I/O, wait, response, or superclass work before read.
            assert not srv.forbid_recovery, 'recovery HTTP entered after a terminal stop'
            assert self.path == '/api/config', 'recovery skipped fresh config'
            assert srv.paths == srv.baseline, 'unexpected HTTP before fresh recovery'
            assert srv.paths.count('/api/config') == 4, 'not fifth config occurrence'
            try:
                srv.terminal = os.read(srv.op_reader, 193)
            except BlockingIOError:
                raise AssertionError('terminal SEND unavailable at first recovery HTTP entry') from None
            assert srv.terminal == srv.expected_terminal, repr(srv.terminal)
            trace('first_recovery_http_entry', path=self.path, previous_paths=srv.paths)
            assert not srv.release_post.is_set(), 'held POST response released before recovery'
            trace('terminal_at_handler_entry', terminal=srv.terminal.decode())
            srv.recovery_checked.set()
        super().do_GET()


class JoinHandler(Handler):
    mutation_path = '/networks/NetA/channels'
    mutation_body = {'name': '#Other'}
    response_body = {'ok': True}
    response_status = 202


class PartHandler(Handler):
    mutation_method = 'DELETE'
    mutation_path = '/networks/NetA/channels/%23Other'
    response_body = {'ok': True}
    response_status = 202


class SelectedPartHandler(PartHandler):
    mutation_path = '/networks/NetA/channels/%23Room'


class LiveHttpRecovery(reconnect.ReconnectFixture):
    handler_class = Handler

    def wait_checkpoint(self, proc, output, expected, budget):
        # An observed append is not a checkpoint-publication barrier. Require
        # the exact selected-window metadata snapshot before asserting the tree.
        checkpoint = output.with_name('.gin.checkpoint')
        deadline = time.monotonic() + budget
        while True:
            try:
                metadata = checkpoint.lstat()
            except FileNotFoundError:
                metadata = None
            if metadata is not None:
                self.assertTrue(stat.S_ISREG(metadata.st_mode))
                self.assertEqual(stat.S_IMODE(metadata.st_mode), 0o600)
                self.assertEqual(metadata.st_uid, os.geteuid())
                self.assertEqual(metadata.st_nlink, 1)
                data = checkpoint.read_bytes()
                self.assertGreaterEqual(len(data), 56)
                self.assertLessEqual(len(data), 8192)
                self.assertEqual(data[:8], b'GINCP\x00\x00\x01')
                anchor, unknown, device, inode, size, key_size = struct.unpack_from('>6Q', data, 8)
                self.assertEqual((anchor, unknown), (0, 1))
                self.assertEqual(len(data), 56 + key_size)
                self.assertNotIn(startup.TOKEN.encode(), data)
                output_metadata = output.stat()
                self.assertEqual((device, inode), (output_metadata.st_dev, output_metadata.st_ino))
                self.assertLessEqual(size, len(expected), 'checkpoint advanced beyond expected output')
                if size == len(expected):
                    self.assertEqual(output.read_bytes(), expected)
                    return
            self.assertIsNone(proc.poll(), 'owner stopped before checkpoint publication')
            self.assertLess(time.monotonic(), deadline, 'successful output checkpoint was not published')
            time.sleep(.01)

    def run_loss_case(self, receipt_closed=False, kind="SEND", selected_part=False):
        instrumented = bool(startup.PREFIX)
        self.assertFalse(selected_part and kind != 'PART')
        self.handler_class = SelectedPartHandler if selected_part else {
            'SEND': Handler, 'JOIN': JoinHandler, 'PART': PartHandler}[kind]
        baseline = BOOT + ['/api/config', '/me', self.handler_class.mutation_path]
        terminal = b'OP1 1 ' + kind.encode() + (b' UNCERTAIN 11\n' if kind == 'SEND' else b' UNCERTAIN 13\n')
        budget = 15 if instrumented else 3
        with self.fixture('positive') as (base, origin):
            srv = cast(Server, base)
            for name in ('posted', 'release_post', 'reader_transferred', 'recovery_checked'):
                setattr(srv, name, threading.Event())
            srv.posts, srv.held_ref_count = [], 0
            srv.op_reader, srv.terminal = -1, b''
            srv.forbid_recovery = receipt_closed or selected_part
            srv.baseline, srv.expected_terminal = baseline, terminal

            def handle_error(request, client_address):
                error = sys.exc_info()[1]
                srv.errors.append(f'{type(error).__name__}: {error}')
                trace('handler_error', error=srv.errors[-1])
            srv.handle_error = handle_error
            root = self.certdir / ('live-http-loss-' + kind.lower() + ('-selected' if selected_part else '') +
                                   ('-closed-pipe' if receipt_closed else '-recovery'))
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                         capture_output=True, timeout=budget)
            self.assertEqual(initialized.returncode, 0, repr(initialized.stderr))
            output, = root.rglob('out')
            output.write_bytes(live.PREFIX_BYTES)
            inode, lock_inode = output.stat().st_ino, (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            old_tree = {str(p.relative_to(root)) for p in root.rglob('*')}
            expected_tree = old_tree | {str(output.with_name('.gin.checkpoint').relative_to(root))}
            interval, timeout = (1000, 3000) if instrumented else (150, 500)
            settings = self.certdir / 'live-http-loss-settings'
            settings.write_text(f'startup_ms={3000 if instrumented else 500}\n'
                'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\n'
                f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={timeout}\n'
                'retry_base_ms=20\nretry_cap_ms=40\nmax_retries=2\n')
            settings.chmod(0o600)
            rr, rw = os.pipe()
            opr, opw = os.pipe()
            for fd in (rr, rw, opr, opw):
                os.set_blocking(fd, False)
            writer, proc = -1, None
            try:
                proc = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                    '-R', rw, '-O', opw), pass_fds=(rw, opw), stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                pid = proc.pid
                os.close(rw)
                os.close(opw)
                rw = opw = -1
                self.assertTrue(select.select([rr], [], [], budget)[0], 'initial READY missing')
                self.assertEqual(os.read(rr, 128), b'READY\n')
                trace('initial_ready', pid=pid)
                self.assertEqual(srv.joins, live.TOPICS)
                self.assertEqual(srv.paths, BOOT)
                srv.ready_observed.set()
                self.assertTrue(srv.heartbeat_seen.wait(budget), 'initial live heartbeat missing')
                self.assertGreaterEqual(srv.heartbeats_after_ready, 2)
                # Keep an actual old selected-room record and inode across recovery.
                srv.release_app.set()
                self.wait_output(proc, srv, output, live.PREFIX_BYTES + live.LINE, budget)
                self.wait_checkpoint(proc, output, live.PREFIX_BYTES + live.LINE, budget)
                self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, expected_tree)
                srv.release_app.clear()
                trace('old_application_received', heartbeats=len(srv.heartbeat_refs))
                writer = os.open(output.with_name('in'), os.O_WRONLY | os.O_NONBLOCK)
                line = (outbound.TEXT + '\n').encode() if kind == 'SEND' else (
                    b'/join #Other\n' if kind == 'JOIN' else b'/part #Room\n' if selected_part else b'/part #Other\n')
                self.assertLessEqual(len(line), os.fpathconf(writer, 'PC_PIPE_BUF'))
                self.assertEqual(os.write(writer, line), len(line))
                self.assertTrue(select.select([opr], [], [], budget)[0], 'SEND PENDING missing')
                self.assertEqual(os.read(opr, 4096), b'OP1 1 ' + kind.encode() + b' PENDING 0\n')
                trace('pending_consumed')
                self.assertTrue(srv.posted.wait(budget), 'complete POST body not received')
                self.assertEqual(srv.paths, baseline)
                self.assertEqual(len(srv.posts), 1)
                if kind == 'PART':
                    self.assertEqual(srv.posts[0], b'')
                else:
                    self.assertEqual(json.loads(srv.posts[0]), {'body': outbound.TEXT} if kind == 'SEND' else {'name': '#Other'})
                self.assertFalse(srv.release_post.is_set())
                self.assertFalse(select.select([opr], [], [], 0)[0])
                trace('full_mutation_held', kind=kind, paths=srv.paths)
                # Transfer the ONLY reader before forcing old Native WS loss.
                if receipt_closed:
                    os.close(opr)
                    opr = -1
                srv.op_reader = opr
                srv.reader_transferred.set()
                srv.client_close_expected.set()
                trace('reader_transferred_then_native_shutdown')
                srv.active_ws.shutdown(socket.SHUT_RDWR)
                if receipt_closed or selected_part:
                    stdout, stderr = proc.communicate(timeout=budget)
                    self.assertEqual(proc.returncode, 1)
                    self.assertEqual(stdout, b'')
                    self.assertEqual(stderr, b'gin: operation pipe failure\n' if receipt_closed else b'gin: connection failure\n')
                    if selected_part and not receipt_closed:
                        self.assertEqual(os.read(opr, 4096), terminal)
                        self.assertEqual(os.read(opr, 4096), b'')
                    self.assertFalse(srv.recovery_checked.is_set())
                    self.assertEqual(srv.paths, baseline, 'HTTP escaped failed terminal publication')
                    self.assertEqual(srv.ws_attempts, 1)
                    self.assertEqual(len(srv.posts), 1)
                    self.assertFalse(srv.release_post.is_set())
                    self.assertEqual(output.read_bytes(), live.PREFIX_BYTES + live.LINE)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                    self.assertEqual(credential.read_bytes(), old_credential)
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, expected_tree)
                    checked = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                             capture_output=True, timeout=budget)
                    self.assertEqual(checked.returncode, 0, 'root lock not released')
                    return
                deadline = time.monotonic() + budget
                while not srv.recovery_checked.wait(.02):
                    self.assertFalse(srv.errors, repr(srv.errors))
                    self.assertIsNone(proc.poll(),
                        'owner stopped instead of recovering after in-flight SEND Native loss')
                    self.assertLess(time.monotonic(), deadline, 'fresh recovery HTTP deadline')
                self.assertEqual(srv.terminal, terminal)
                self.assertTrue(srv.attempt_events[1].wait(budget), 'new Native WSS missing')
                self.assertTrue(srv.heartbeat_seen.wait(budget), 'recovered live heartbeat missing')
                srv.release_app.set()
                self.wait_output(proc, srv, output, live.PREFIX_BYTES + live.LINE * 2, budget)
                self.wait_checkpoint(proc, output, live.PREFIX_BYTES + live.LINE * 2, budget)
                trace('recovered_application_received')
                self.assertEqual(proc.pid, pid)
                self.assertIsNone(proc.poll())
                self.assertEqual(srv.ws_attempts, 2)
                self.assertEqual(srv.joins, live.TOPICS * 2)
                self.assertEqual(srv.paths, baseline + BOOT)
                self.assertEqual(len(srv.posts), 1, 'uncertain SEND replayed')
                self.assertFalse(srv.release_post.is_set(), 'recovery required old HTTP reply')
                self.assertFalse(select.select([rr], [], [], 0)[0], 'additional READY')
                self.assertFalse(select.select([opr], [], [], 0)[0], 'extra operation receipt')
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual(credential.read_bytes(), old_credential)
                self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, expected_tree)
                self.assertNotIn(outbound.TEXT.encode(), output.read_bytes(), 'local SEND echo')
                srv.client_close_expected.set()
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=budget)
                self.assertEqual(proc.returncode, 0, repr(stderr))
                self.assertEqual(stdout + stderr, b'')
                self.assertEqual(os.read(rr, 128), b'', 'READY republished')
                self.assertEqual(os.read(opr, 4096), b'', 'duplicate terminal receipt')
                checked = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                         capture_output=True, timeout=budget)
                self.assertEqual(checked.returncode, 0, 'root lock not released')
            finally:
                srv.client_close_expected.set()
                # Kill/reap BEFORE releasing held handler: never send a fake response
                # merely to unblock the old HTTP operation on a failed run.
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    stdout, stderr = proc.communicate(timeout=budget)
                    # Reader ownership returns only after owner reap; this diagnostic
                    # is NOT proof of terminal-before-first-recovery-entry ordering.
                    residual = b''
                    if opr>=0 and not srv.recovery_checked.is_set():
                        try:
                            residual = os.read(opr, 4096)
                        except BlockingIOError:
                            pass
                    trace('owner_reaped_before_held_handler_release', exit=proc.returncode,
                        stdout=stdout.decode(errors='replace'), stderr=stderr.decode(errors='replace'),
                        residual_terminal_not_entry_proof=residual.decode(errors='replace'),
                        paths=srv.paths, mutations=len(srv.posts), ws_attempts=srv.ws_attempts,
                        recovery_checked=srv.recovery_checked.is_set(),
                        old_records_preserved=output.read_bytes().startswith(live.PREFIX_BYTES + live.LINE),
                        output_inode_preserved=output.stat().st_ino == inode,
                        lock_inode_preserved=(root / '.gin.lock').stat().st_ino == lock_inode,
                        credential_preserved=credential.read_bytes() == old_credential,
                        tree_preserved={str(p.relative_to(root)) for p in root.rglob('*')} == expected_tree,
                        held_response_released=srv.release_post.is_set())
                srv.stopped.set()
                srv.release_post.set()
                if writer >= 0:
                    os.close(writer)
                for fd in (rr, rw, opr, opw):
                    if fd >= 0:
                        os.close(fd)


    def test_inflight_send_loss_publishes_uncertain_before_recovery_without_replay(self):
        self.run_loss_case()

    def test_terminal_send_pipe_failure_prevents_fresh_recovery_http(self):
        self.run_loss_case(receipt_closed=True)


    def test_inflight_join_loss_reports_uncertain_before_recovery_without_replay(self):
        self.run_loss_case(kind='JOIN')

    def test_inflight_nonselected_part_loss_reports_uncertain_before_recovery_without_replay(self):
        self.run_loss_case(kind='PART')

    def test_selected_part_loss_reports_uncertain_and_stops_without_recovery(self):
        self.run_loss_case(kind='PART', selected_part=True)

    def test_terminal_join_pipe_failure_prevents_fresh_recovery_http(self):
        self.run_loss_case(kind='JOIN', receipt_closed=True)

    def test_terminal_part_pipe_failure_prevents_fresh_recovery_http(self):
        self.run_loss_case(kind='PART', receipt_closed=True)


if __name__ == '__main__':
    unittest.main()
