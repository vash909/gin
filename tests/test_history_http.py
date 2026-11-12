#!/usr/bin/env python3
"""Real native HTTPS focused history; synthetic credentials, no server writes."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import shlex
import socket
import ssl
from types import SimpleNamespace
from typing import Any, cast, Optional
import subprocess
import threading
import unittest
import test_http
from test_auth import IDENTITY, TOKEN

ROOT = Path(__file__).resolve().parents[1]
CONFIG = {"server": "grappa", "protocol_version": 63, "min_protocol_version": 1}
PATH = "/networks/NetA/channels/%23MiX%C3%A9/messages?limit=2&after=0"
QUERY = "/networks/NetA/channels/PeEr/messages?limit=2&after=0"
STATUS = "/networks/NetA/channels/%24server/messages?limit=2&after=0"
OK, IGNORE, INVALID, MALFORMED, LIMIT, NOMEM, ROUTE, UNSUPPORTED, CONFLICT, STALE, DEADLINE, PREFLIGHT, AUTH, TLS, TIMEOUT, TRANSPORT, REMOTE = range(17)


def encode(v):
    return v if isinstance(v, bytes) else json.dumps(v).encode()


class Server(http.server.ThreadingHTTPServer):
    records: list
    errors: list
    responses: dict
    reject_tls: bool
    reconfigure: Optional[dict]
    partial: Optional[str]
    delay: Optional[str]
    headers: Optional[str]
    stopped: threading.Event
    def handle_error(self, request, client_address):
        # Do not globally silence fixture errors. Handler owns the narrow
        # deliberately rejected TLS / first-read disconnect exception below.
        import sys
        self.errors.append(sys.exc_info()[1])


class Handler(http.server.BaseHTTPRequestHandler):
    raw_requestline: bytes
    def log_message(self, format, *args):
        pass

    def handle_one_request(self):
        try:
            super().handle_one_request()
        except (ConnectionResetError, BrokenPipeError):
            if not (cast(Server, self.server).reject_tls and not hasattr(self, "raw_requestline")):
                raise
            self.close_connection = True

    def do_GET(self):
        srv = cast(Server, self.server)
        srv.records.append((self.command, self.path, dict(self.headers)))
        status, body = srv.responses.get(self.path, (404, b""))
        if self.path not in ("/api/config", "/me") and srv.reconfigure is not None:
            srv.responses["/api/config"] = (200, encode(srv.reconfigure))
        if self.path == srv.delay:
            srv.stopped.wait(1.0)
        try:
            self.send_response(status)
            self.send_header("Content-Length", str(len(body) + (self.path == srv.partial)))
            if self.path == srv.headers:
                self.send_header("X-Huge", "x" * 66000)
            self.end_headers()
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            # Only intentional deadline/limit client abort after an HTTP request.
            if self.path != srv.delay and self.path != srv.headers and len(body) <= 1048576:
                raise
        self.close_connection = True


class HistoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    tls = test_http.HTTPTests.tls

    @contextlib.contextmanager
    def fixture(self, config=CONFIG, me=IDENTITY, page=b"[]", status=200,
                me_status=200, cert="good", reject_tls=False, reconfigure=None,
                partial=None, delay=None, headers=None):
        srv = Server(("127.0.0.1", 0), Handler)
        srv.records, srv.errors = [], []
        srv.reject_tls = reject_tls
        srv.responses = {"/api/config": (200, encode(config)), "/me": (me_status, encode(me))}
        for path in (PATH, QUERY, STATUS, PATH.replace("&after=0", "&before=99"), PATH.replace("&after=0", "")):
            srv.responses[path] = (status, encode(page))
        srv.partial, srv.delay, srv.headers = partial, delay, headers
        srv.reconfigure = reconfigure
        srv.stopped = threading.Event()
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.01})
        thread.start()
        try:
            yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally:
            srv.stopped.set(); srv.shutdown(); srv.server_close(); thread.join(3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(srv.errors, [])

    def request(self, origin, mode="ok", calls=1, ca="ca.pem"):
        credential = self.certdir / "history-credential.json"
        credential.write_text(json.dumps(dict(origin=origin, token=TOKEN,
            subject_id=IDENTITY["id"], subject_name=IDENTITY["name"])))
        credential.chmod(0o600)
        binary = os.environ.get("GIN_HISTORY_DRIVER", str(ROOT / ".build/history_http_driver"))
        prefix = shlex.split(os.environ.get("GIN_HISTORY_PREFIX", ""))
        p = subprocess.run(prefix + [binary, origin, str(self.certdir / ca),
            str(credential), mode, str(calls)], capture_output=True, timeout=25)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace"))
        self.assertEqual(p.stderr, b"")
        self.assertNotIn(TOKEN.encode(), p.stdout + p.stderr)
        self.assertNotIn(b"owned fixture body", p.stdout + p.stderr)
        return [list(map(int, line.split())) for line in p.stdout.splitlines()]

    def flow(self, srv, paths):
        self.assertEqual([(r[0], r[1]) for r in srv.records], [("GET", p) for p in paths])
        for _, path, headers in srv.records:
            self.assertNotIn("Cookie", headers)
            self.assertNotIn("Proxy-Authorization", headers)
            self.assertNotIn(TOKEN, path)
            if path == "/api/config":
                self.assertNotIn("Authorization", headers)
            else:
                self.assertEqual(headers.get("Authorization"), "Bearer " + TOKEN)

    def test_exact_original_path_and_owned_page(self):
        row = dict(id=42, server_time=1700000000123, network="NetA", channel="#mixé",
                   kind="privmsg", sender="Sender", body="owned fixture body")
        with self.fixture(page=[row]) as (srv, origin):
            self.assertEqual(self.request(origin), [[OK, 0, 0, 200, 1, 1, 1]])
            self.flow(srv, ["/api/config", "/me", PATH])

    def test_all_local_rejection_before_io(self):
        cases = {"unknown-mode": UNSUPPORTED, "encrypted": UNSUPPORTED,
                 "wrong-id": ROUTE, "wrong-slug": ROUTE, "wrong-nick": ROUTE,
                 "missing-channel": ROUTE, "missing-peer": ROUTE, "unknown-queries": ROUTE}
        for mode in ["wrong-origin", "wrong-uuid", "bad-credential", "credential-origin", "bad-uuid",
                     "zero-epoch", "bad-limit", "bad-anchor", "bad-timeout", "bad-policy", "occupied",
                     "null-settings", "null-policy", "null-credential", "null-topology", "null-scope", "null-request", "null-out"]:
            cases[mode] = INVALID
        with self.fixture() as (srv, origin):
            for mode, code in cases.items():
                with self.subTest(mode=mode):
                    r = self.request(origin, mode)[0]
                    self.assertEqual(r[:6], [code, 1, 1, 0, 0, 0])
            self.assertEqual(srv.records, [])

    def test_role_floor_and_cursor_paths(self):
        for mode, path, floor in [("ok", PATH, 4), ("query", QUERY, 36), ("status", STATUS, 4),
                                 ("before", PATH.replace("&after=0", "&before=99"), 4),
                                 ("latest", PATH.replace("&after=0", ""), 4)]:
            with self.subTest(mode=mode), self.fixture(config={**CONFIG, "protocol_version": floor}) as (srv, origin):
                self.assertEqual(self.request(origin, mode)[0][0], OK)
                self.flow(srv, ["/api/config", "/me", path])
            with self.fixture(config={**CONFIG, "protocol_version": floor-1}) as (srv, origin):
                r = self.request(origin, mode)[0]
                self.assertEqual(r[:6], [PREFLIGHT, 5, 1, 0, 0, 0])
                self.flow(srv, ["/api/config"])

    def test_fresh_preflight_same_process(self):
        with self.fixture(reconfigure={**CONFIG, "min_protocol_version": 64}) as (srv, origin):
            r = self.request(origin, calls=2)
            self.assertEqual([v[0] for v in r], [OK, PREFLIGHT])
            self.flow(srv, ["/api/config", "/me", PATH, "/api/config"])
        with self.fixture() as (srv, origin):
            self.assertEqual([r[0] for r in self.request(origin, calls=2)], [OK, OK])
            self.flow(srv, ["/api/config", "/me", PATH] * 2)

    def test_bound_identity_and_scope_prevent_history(self):
        for status, me in [(401, IDENTITY), (403, {"error": "client_token_scope"}),
                           (200, {**IDENTITY, "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"})]:
            with self.fixture(me_status=status, me=me) as (srv, origin):
                r = self.request(origin)[0]
                self.assertEqual(r[0], AUTH); self.assertEqual(r[4:6], [1, 0])
                self.flow(srv, ["/api/config", "/me"])

    def test_status_and_incomplete_transfer_precedence(self):
        for status in (201, 204, 302, 401, 403, 429, 500):
            with self.subTest(status=status), self.fixture(status=status, page=b"{") as (srv, origin):
                r = self.request(origin)[0]
                self.assertEqual(r[0], AUTH if status in (401, 403) else REMOTE)
                self.assertEqual(r[3], status)
                self.flow(srv, ["/api/config", "/me", PATH])
        for status in (200, 401, 403, 429, 500):
            with self.fixture(status=status, partial=PATH) as (srv, origin):
                r = self.request(origin)[0]
                self.assertEqual(r[0], TRANSPORT); self.assertEqual(r[3], status)
            with self.fixture(status=status, page=b" " * 1048577) as (srv, origin):
                r = self.request(origin)[0]
                self.assertEqual(r[0], LIMIT); self.assertEqual(r[3], status)

    def test_malformed_page_and_exact_size(self):
        for page, code in [(b"[", MALFORMED), ({}, MALFORMED), (b"[]\x00", MALFORMED),
                           (b'[{"id":1,"id":2}]', MALFORMED),
                           (b"[]" + b" " * 1048574, OK),
                           (b"[" * 33 + b"0" + b"]" * 33, LIMIT)]:
            with self.fixture(page=page) as (srv, origin):
                self.assertEqual(self.request(origin)[0][0], code)
                self.flow(srv, ["/api/config", "/me", PATH])

    def test_narrow_first_read_disconnect_policy(self):
        class FaultReader:
            def readline(self, size):
                raise ConnectionResetError("deliberate first-read fixture")
        for rejected, has_line, swallowed in [(True, False, True),
                                               (False, False, False), (True, True, False)]:
            h = Handler.__new__(Handler)
            h.server = cast(Server, SimpleNamespace(reject_tls=rejected))
            h.rfile = cast(Any, FaultReader())
            if has_line:
                h.raw_requestline = b"GET / HTTP/1.1\r\n"
            if swallowed:
                h.handle_one_request()
                self.assertTrue(h.close_connection)
            else:
                with self.assertRaises(ConnectionResetError):
                    h.handle_one_request()

    def test_tls_unknown_ca_and_san_mismatch(self):
        for cert, ca in [("good", "bad.pem"), ("wrong", "ca.pem")]:
            with self.fixture(cert=cert, reject_tls=True) as (srv, origin):
                self.assertEqual(self.request(origin, ca=ca)[0][:6], [TLS, 6, 1, 0, 0, 0])
                self.assertEqual(srv.records, [])

    def test_timeout_and_limit_each_phase(self):
        for path in ("/api/config", "/me", PATH):
            with self.fixture(delay=path) as (srv, origin):
                self.assertEqual(self.request(origin, "deadline")[0][0], TIMEOUT)
            with self.fixture(headers=path) as (srv, origin):
                self.assertEqual(self.request(origin)[0][0], LIMIT)
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0)); port = reserved.getsockname()[1]
        self.assertEqual(self.request(f"https://127.0.0.1:{port}")[0][0], TRANSPORT)


if __name__ == "__main__":
    unittest.main(verbosity=2)
