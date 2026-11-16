#!/usr/bin/env python3
"""Real held HTTPS history requests honor one borrowed aggregate controller."""
import contextlib
import json
import os
from pathlib import Path
import signal
import subprocess
import threading
from typing import cast
import unittest
import test_history_http as history

ROOT = Path(__file__).resolve().parents[1]


class Server(history.Server):
    held: str
    entered: threading.Event
    released: threading.Event


class Handler(history.Handler):
    def do_GET(self):
        server = cast(Server, self.server)
        if self.path != server.held:
            return super().do_GET()
        server.records.append((self.command, self.path, dict(self.headers)))
        server.entered.set()
        if not server.released.wait(30):
            raise AssertionError('parent did not release held history request')
        # Intentional hold never emits headers/body; client must terminate first.
        self.close_connection = True


class HistoryControl(unittest.TestCase):
    certdir: Path
    tls = history.HistoryTests.tls

    @classmethod
    def setUpClass(cls):
        history.test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        history.test_http.HTTPTests.tearDownClass.__func__(cls)

    @contextlib.contextmanager
    def fixture(self, held):
        server = Server(('127.0.0.1', 0), Handler)
        server.records, server.errors = [], []
        server.reject_tls = False
        server.reconfigure = server.partial = server.delay = server.headers = None
        server.stopped = threading.Event()
        server.held = held
        server.entered, server.released = threading.Event(), threading.Event()
        row = dict(id=42, server_time=1700000000123, network='NetA', channel='#mixé',
                   kind='privmsg', sender='Sender', body='owned fixture body')
        server.responses = {'/api/config': (200, history.encode(history.CONFIG)),
                            '/me': (200, history.encode(history.IDENTITY)),
                            history.PATH: (200, history.encode([row]))}
        server.socket = self.tls('good').wrap_socket(server.socket, server_side=True)
        thread = threading.Thread(target=server.serve_forever, kwargs={'poll_interval': 0.01})
        thread.start()
        try:
            yield server, f'https://127.0.0.1:{server.server_port}'
        finally:
            server.released.set()
            server.shutdown()
            server.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(server.errors, [])

    def run_case(self, phase, mode, calls=1):
        held = {'config': '/api/config', 'identity': '/me', 'messages': history.PATH}.get(phase, '')
        wrapper = history.wrapper_arguments()
        with self.fixture(held) as (server, origin):
            credential = self.certdir / 'controlled-history-credential.json'
            credential.write_text(json.dumps(dict(origin=origin, token=history.TOKEN,
                subject_id=history.IDENTITY['id'], subject_name=history.IDENTITY['name'])))
            credential.chmod(0o600)
            binary = os.environ.get('GIN_HISTORY_CONTROL_DRIVER', str(ROOT / '.build/history_control_driver'))
            process = subprocess.Popen(wrapper + [binary, origin, str(self.certdir / 'ca.pem'),
                str(credential), mode, str(calls)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                if held:
                    self.assertTrue(server.entered.wait(10 if wrapper else 2),
                                    'selected history request never entered held handler')
                    if mode == 'control-signal':
                        process.send_signal(signal.SIGTERM)
                try:
                    stdout, stderr = process.communicate(timeout=5 if wrapper else 1)
                except subprocess.TimeoutExpired:
                    self.fail('history aggregate did not observe cooperative cancellation before HTTP timeout')
                self.assertEqual(process.returncode, 0, stderr.decode(errors='replace'))
                self.assertEqual(stderr, b'')
                self.assertNotIn(history.TOKEN.encode(), stdout + stderr)
                self.assertNotIn(b'owned fixture body', stdout + stderr)
                rows = [list(map(int, line.split())) for line in stdout.splitlines()]
                self.assertEqual(len(rows), calls)
                if held:
                    self.assertTrue(all(row[0] == history.TRANSPORT and row[-1] == 0 for row in rows))
                    expected = ['/api/config', '/me', history.PATH]
                    expected = expected[:expected.index(held) + 1]
                    attempted = {'config': [0, 0], 'identity': [1, 0], 'messages': [1, 1]}[phase]
                    self.assertEqual(rows[0][4:6], attempted)
                elif mode == 'control-aborted':
                    self.assertEqual(rows[0][0], history.TRANSPORT)
                    self.assertEqual(rows[0][4:], [0, 0, 0])
                    expected = []
                else:
                    self.assertTrue(all(row[0] == history.OK and row[-1] == 1 for row in rows))
                    expected = ['/api/config', '/me', history.PATH] * calls
                self.assertEqual([record[1] for record in server.records], expected)
                for _, path, headers in server.records:
                    self.assertNotIn(history.TOKEN, path)
                    if path == '/api/config':
                        self.assertNotIn('Authorization', headers)
                    else:
                        self.assertEqual(headers.get('Authorization'), 'Bearer ' + history.TOKEN)
                    self.assertNotIn('Cookie', headers)
                    self.assertNotIn('Proxy-Authorization', headers)
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)
                # Reap before allowing the held handler to finish.
                server.released.set()

    def test_held_config_deadline(self):
        self.run_case('config', 'control-deadline')

    def test_held_identity_deadline(self):
        self.run_case('identity', 'control-deadline')

    def test_held_messages_deadline(self):
        self.run_case('messages', 'control-deadline')

    def test_held_config_signal(self):
        self.run_case('config', 'control-signal')

    def test_held_identity_signal(self):
        self.run_case('identity', 'control-signal')

    def test_held_messages_signal(self):
        self.run_case('messages', 'control-signal')

    def test_controlled_completed_owned_pages(self):
        self.run_case('', 'control-ok', calls=2)

    def test_null_controller_preserves_legacy_completed_pages(self):
        self.run_case('', 'control-null', calls=2)

    def test_already_aborted_never_starts_http(self):
        self.run_case('', 'control-aborted')


if __name__ == '__main__':
    unittest.main()
