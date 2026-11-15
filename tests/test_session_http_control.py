"""Actual owner deadline/stop while the first verified HTTPS response is held."""
import contextlib
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import threading
import unittest
from typing import cast
import test_session as startup


class Server(startup.Server):
    request_seen: threading.Event
    release_response: threading.Event
    hold_path: str
    hold_occurrence: int
    path_counts: dict[str, int]


class Handler(startup.Handler):
    def do_GET(self):
        srv = cast(Server, self.server)
        srv.path_counts[self.path] = srv.path_counts.get(self.path, 0) + 1
        if self.path != srv.hold_path or srv.path_counts[self.path] != srv.hold_occurrence:
            super().do_GET()
            return
        anonymous = self.path == '/api/config' or self.path.startswith('/socket/websocket?')
        assert self.headers.get('Authorization') == (None if anonymous else 'Bearer ' + startup.TOKEN)
        if self.path.startswith('/socket/websocket?'):
            assert self.headers.get('Sec-WebSocket-Protocol') == 'phoenix,base64url.bearer.phx.YWF+'
        srv.paths.append(self.path)
        srv.request_seen.set()
        assert srv.release_response.wait(40), 'test did not release discovery handler'
        self.close_connection = True
        # No HTTP status/body is emitted before release. The tested owner must
        # already have terminated, so cleanup need not write to a closed peer.
        assert srv.stopped.is_set(), 'response barrier released before owner termination'


class HttpControl(unittest.TestCase):
    certdir: Path
    tls = startup.StartupTracer.tls
    command = startup.StartupTracer.command

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, held_path, occurrence):
        srv = Server(('127.0.0.1', 0), Handler)
        srv.paths, srv.joins, srv.errors = [], [], []
        srv.hold_path = held_path
        srv.hold_occurrence = occurrence
        srv.path_counts = {}
        srv.request_seen, srv.release_response, srv.stopped = [threading.Event() for _ in range(3)]
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={'poll_interval': .02})
        thread.start()
        try:
            yield srv, f'https://127.0.0.1:{srv.server_port}'
        finally:
            srv.stopped.set()
            srv.release_response.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), 'fixture listener leaked')
            self.assertFalse(srv.errors, 'strict held-response handler failed')

    def run_case(self, stop=False, held_path='/api/config', occurrence=1):
        instrumented = bool(startup.PREFIX)
        with self.fixture(held_path, occurrence) as (srv, origin):
            root = self.certdir / (('http-stop-' if stop else 'http-deadline-') + held_path.strip('/').replace('/', '-') + f'-{occurrence}')
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output, = root.rglob('out')
            output.write_bytes(b'existing prefix\n')
            inode, lock_inode = output.stat().st_ino, (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            old_tree = {str(p.relative_to(root)) for p in root.rglob('*')}
            config = self.certdir / ('http-stop-settings' if stop else 'http-deadline-settings')
            budget_ms = 180000 if stop else 3000 if instrumented else 500
            config.write_text(f'startup_ms={budget_ms}\nconnect_ms=10000\ntotal_ms=20000\n'
                              'join_ms=12000\nmax_retries=2\n')
            config.chmod(0o600)
            rr, rw = os.pipe()
            for fd in (rr, rw):
                os.set_blocking(fd, False)
            proc = None
            try:
                proc = subprocess.Popen(self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw),
                    pass_fds=(rw,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                rw = -1
                self.assertTrue(srv.request_seen.wait(30 if instrumented else 5),
                                'first verified HTTPS request was not received')
                self.assertIsNone(proc.poll())
                self.assertFalse(srv.release_response.is_set())
                if stop:
                    proc.send_signal(signal.SIGTERM)
                try:
                    stdout, stderr = proc.communicate(timeout=6 if instrumented else 2)
                except subprocess.TimeoutExpired:
                    self.fail('owner remained blocked in HTTPS after stop' if stop else
                              'owner remained blocked beyond its startup deadline while HTTPS response was held')
                self.assertFalse(srv.release_response.is_set(), 'owner required response release to terminate')
                self.assertEqual(proc.returncode, 0 if stop else 1)
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'' if stop else b'gin: startup timeout\n')
                self.assertTrue(select.select([rr], [], [], 0)[0])
                self.assertEqual(os.read(rr, 128), b'', 'READY preceded complete bootstrap')
                expected = {'/api/config': ['/api/config'], '/me': ['/api/config', '/me'],
                            '/boot': ['/api/config', '/me', '/boot'],
                            '/socket/websocket?vsn=2.0.0&client_proto=36':
                                ['/api/config', '/me', '/boot', '/api/config', '/me',
                                 '/socket/websocket?vsn=2.0.0&client_proto=36']}[held_path]
                if occurrence == 2:
                    expected = ['/api/config', '/me', '/boot'] + expected
                self.assertEqual(srv.paths, expected, 'later request escaped interrupted aggregate')
                self.assertEqual(srv.joins, [])
                self.assertEqual(output.read_bytes(), b'existing prefix\n')
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual(credential.read_bytes(), old_credential)
                self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, old_tree)
            finally:
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    proc.communicate(timeout=90 if instrumented else 10)
                srv.stopped.set()
                srv.release_response.set()
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
            check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=90 if instrumented else 5)
            self.assertEqual(check.returncode, 0, 'root lock not released')

    def test_first_held_https_response_obeys_original_owner_deadline(self):
        self.run_case()

    def test_held_bound_identity_response_obeys_original_owner_deadline(self):
        self.run_case(held_path='/me')

    def test_held_boot_response_obeys_original_owner_deadline(self):
        self.run_case(held_path='/boot')

    def test_signal_during_first_held_https_response_stops_before_next_request(self):
        self.run_case(stop=True)

    def test_native_initial_discovery_obeys_same_original_owner_deadline(self):
        self.run_case(occurrence=2)

    def test_native_bound_identity_obeys_same_original_owner_deadline(self):
        self.run_case(held_path='/me', occurrence=2)

    def test_signal_during_native_discovery_stops_before_identity_or_upgrade(self):
        self.run_case(stop=True, occurrence=2)

    def test_raw_wss_upgrade_obeys_original_owner_deadline(self):
        self.run_case(held_path='/socket/websocket?vsn=2.0.0&client_proto=36')

    def test_signal_during_raw_wss_upgrade_stops_without_accepting_headers(self):
        self.run_case(stop=True, held_path='/socket/websocket?vsn=2.0.0&client_proto=36')


if __name__ == '__main__':
    unittest.main()
