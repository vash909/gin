"""Publish a lost QUERY's terminal receipt before fresh recovery HTTP."""
import contextlib
import fcntl
import json
import os
from pathlib import Path
import select
import signal
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
import test_session_query as query
import test_session_outbound as outbound


class Server(query.QueryServer, live.Server):
    ws_attempts: int
    op_reader: int
    terminal: bytes
    reader_transferred: threading.Event
    loss_armed: threading.Event
    recovery_checked: threading.Event
    first_finished: threading.Event

    def handle_error(self, request, client_address):
        error = sys.exc_info()[1]
        self.errors.append(f'{type(error).__name__}: {error}')


class Handler(query.QueryHandler, live.Handler):
    attempt = 0

    def do_GET(self):
        srv = cast(Server, self.server)
        if srv.ws_attempts == 1 and srv.loss_armed.is_set() and not srv.recovery_checked.is_set():
            assert self.path == '/api/config', 'recovery skipped fresh protocol bootstrap'
            assert srv.reader_transferred.is_set(), 'operation reader not handed off before loss'
            # No wait/retry: the complete atomic terminal record must ALREADY be
            # readable at first recovery-handler entry, before serving HTTP.
            try:
                srv.terminal = os.read(srv.op_reader, 193)
            except BlockingIOError:
                raise AssertionError('terminal QUERY receipt unavailable at first recovery HTTP entry') from None
            assert srv.terminal == b'OP1 1 QUERY UNCERTAIN 11\n', repr(srv.terminal)
            srv.recovery_checked.set()
        super().do_GET()

    def upgrade(self):
        srv = cast(Server, self.server)
        srv.ws_attempts += 1
        self.attempt = srv.ws_attempts
        assert self.attempt <= 2, 'retry budget unexpectedly extended'
        if self.attempt == 1:
            query.QueryHandler.upgrade(self)
        else:
            assert srv.first_finished.wait(query.budget(5)), 'old WSS handler leaked'
            live.Handler.upgrade(self)

    def finish(self):
        try:
            super().finish()
        finally:
            if self.attempt == 1:
                cast(Server, self.server).first_finished.set()


class QueryRecovery(unittest.TestCase):
    certdir: Path
    tls = query.SessionQueryTracer.tls
    command = query.SessionQueryTracer.command
    wait_output = query.SessionQueryTracer.wait_output
    read_op_lines = query.SessionQueryTracer.read_op_lines
    operation_buffer: bytearray

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self):
        srv = Server(('127.0.0.1', 0), Handler)
        for name in ['stopped', 'posted', 'release_after', 'query_seen', 'release_query',
                     'reader_transferred', 'recovery_checked', 'first_finished', 'loss_armed',
                     'ready_observed', 'heartbeat_seen', 'release_app', 'app_emitted',
                     'client_closed', 'client_close_expected', 'drop_observed']:
            setattr(srv, name, threading.Event())
        srv.paths, srv.joins, srv.errors, srv.posts = [], [], [], []
        srv.methods, srv.raw_bodies, srv.queries = [], [], []
        srv.heartbeat_refs, srv.dropped_refs = [], []
        srv.mode, srv.case = 'close', 'positive'
        srv.ws_attempts, srv.heartbeats_after_ready, srv.op_reader = 0, 0, -1
        srv.terminal = b''
        srv.release_app.set()
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.posted.set()
            srv.release_query.set()
            srv.release_after.set()
            srv.release_app.set()
            srv.client_close_expected.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'strict recovery fixture handler failure')

    def run_case(self, sink_failure=False):
        with self.fixture() as (srv, origin):
            root = self.certdir / ('query-recovery-root-sink' if sink_failure else 'query-recovery-root-ok')
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=query.budget(10))
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output, = root.rglob('out')
            output.write_bytes(query.PREFIX)
            inode, lock_inode = output.stat().st_ino, (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            old_tree = {str(p.relative_to(root)) for p in root.rglob('*')} | {
                str(output.with_name('.gin.checkpoint').relative_to(root))}
            config = self.certdir / 'query-recovery-settings'
            policy = ('heartbeat_interval_ms=60000\nheartbeat_timeout_ms=10000\n'
                      'retry_base_ms=20\nretry_cap_ms=100\nmax_retries=2\n')
            if startup.PREFIX:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            config.write_text(policy)
            config.chmod(0o600)
            rr, rw = os.pipe()
            opr, opw = os.pipe()
            for fd in (rr, rw, opr, opw):
                os.set_blocking(fd, False)
            flags = {fd: (fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD))
                     for fd in (rw, opw)}
            proc = None
            writer = -1
            try:
                proc = subprocess.Popen(self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem',
                    '-R', rw, '-O', opw), pass_fds=(rw, opw), stdin=subprocess.DEVNULL,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                pid = proc.pid
                self.assertTrue(select.select([rr], [], [], query.budget(8))[0])
                self.assertEqual(os.read(rr, 128), b'READY\n')
                self.wait_output(proc, output, query.BEFORE)
                outbound.wait_checkpoint(self, proc, output, query.PREFIX + query.BEFORE, query.budget(8))
                self.assertEqual(srv.paths, outbound.BOOT)
                self.assertEqual(output.read_bytes(), query.PREFIX + query.BEFORE)
                writer = os.open(output.with_name('in'), os.O_WRONLY | os.O_NONBLOCK)
                command = b'/query Bob\n'
                self.assertLessEqual(len(command), os.fpathconf(writer, 'PC_PIPE_BUF'))
                self.assertEqual(os.write(writer, command), len(command))
                self.operation_buffer = bytearray()
                pending, tail = self.read_op_lines(opr, 1, query.budget(8))
                self.assertEqual(pending, [b'OP1 1 QUERY PENDING 0\n'])
                self.assertEqual(tail, b'')
                self.assertTrue(srv.query_seen.wait(query.budget(8)))
                self.assertEqual(srv.queries, [['1', '4', live.TOPICS[0], 'open_query_window',
                    {'network_id': 3000000001, 'target_nick': 'Bob'}]])
                self.assertFalse(select.select([opr], [], [], 0)[0])
                # From here ONLY the first recovery HTTP handler reads opr. The
                # pending query remains unacknowledged until controlled CLOSE.
                srv.loss_armed.set()
                if sink_failure:
                    # Already-implemented publication-error policy: a closed
                    # reader must stop recovery, not silently discard a receipt.
                    os.close(opr)
                    opr = -1
                    srv.release_query.set()
                    stdout, stderr = proc.communicate(timeout=query.budget(12))
                    self.assertEqual(proc.returncode, 1)
                    self.assertEqual(stdout, b'')
                    self.assertEqual(stderr, b'gin: operation pipe failure\n')
                    self.assertFalse(srv.recovery_checked.is_set())
                    self.assertEqual(srv.ws_attempts, 1)
                    self.assertEqual(len(srv.paths), 9, 'HTTP occurred after receipt publication failure')
                    self.assertEqual(srv.methods, [])
                    self.assertEqual(srv.posts, [])
                    self.assertEqual(len(srv.queries), 1)
                    self.assertEqual(output.read_bytes(), query.PREFIX + query.BEFORE)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                    self.assertEqual(credential.read_bytes(), old_credential)
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, old_tree)
                else:
                    srv.op_reader = opr
                    srv.reader_transferred.set()
                    srv.release_query.set()
                    deadline = time.monotonic() + query.budget(12)
                    while not srv.recovery_checked.wait(.02):
                        self.assertIsNone(proc.poll(),
                            'owner stopped rather than consuming QUERY receipt and recovering')
                        self.assertLess(time.monotonic(), deadline, 'first recovery HTTP deadline')
                    self.assertEqual(srv.terminal, b'OP1 1 QUERY UNCERTAIN 11\n')
                    self.wait_output(proc, output, live.LINE)
                    outbound.wait_checkpoint(self, proc, output, query.PREFIX + query.BEFORE + live.LINE, query.budget(8))
                    self.assertEqual(proc.pid, pid)
                    self.assertIsNone(proc.poll())
                    self.assertEqual(srv.ws_attempts, 2)
                    self.assertEqual(srv.joins, live.TOPICS * 2)
                    self.assertEqual(len(srv.queries), 1, 'uncertain QUERY was replayed')
                    self.assertFalse(select.select([rr], [], [], 0)[0], 'second READY published')
                    self.assertFalse(select.select([opr], [], [], 0)[0], 'extra operation receipt')
                    self.assertEqual(srv.methods, [])
                    self.assertEqual(srv.posts, [])
                    self.assertEqual(srv.paths, outbound.BOOT * 2)
                    self.assertEqual(output.read_bytes(), query.PREFIX + query.BEFORE + live.LINE)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, old_tree)
                    self.assertEqual(credential.read_bytes(), old_credential)
                    for fd, original in flags.items():
                        self.assertEqual((fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)), original)
                    srv.client_close_expected.set()
                    proc.send_signal(signal.SIGTERM)
                    stdout, stderr = proc.communicate(timeout=query.budget(12))
                    self.assertEqual(proc.returncode, 0, repr(stderr))
                    self.assertEqual(stdout + stderr, b'')
            finally:
                srv.stopped.set()
                srv.release_query.set()
                srv.release_after.set()
                srv.posted.set()
                srv.client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    proc.communicate(timeout=query.budget(10))
                if writer >= 0:
                    os.close(writer)
                for fd in (rr, rw, opr, opw):
                    if fd >= 0:
                        os.close(fd)
            check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                   capture_output=True, timeout=query.budget(10))
            self.assertEqual(check.returncode, 0, 'root lock not released')

    def test_pending_query_terminal_is_available_before_fresh_http_without_replay(self):
        self.run_case()

    def test_terminal_receipt_sink_failure_stops_before_fresh_http(self):
        self.run_case(sink_failure=True)


if __name__ == '__main__':
    unittest.main()
