#!/usr/bin/env python3
"""Real native protocol discovery and gated authentication over private TLS."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import threading
import time
import unittest
from typing import Optional, cast
import test_http
from test_auth import IDENTITY, TOKEN

ROOT = Path(__file__).resolve().parents[1]
CONFIG = {"server": "grappa", "protocol_version": 7, "min_protocol_version": 3}


class ProtocolServer(http.server.ThreadingHTTPServer):
    records: list
    status: int
    body: bytes
    auth_status: int
    redirect: Optional[str]
    reconfigure_body: Optional[bytes]
    delay: float
    headers: int
    partial: bool


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def handle_request(self):
        self.connection.settimeout(2)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        srv = cast(ProtocolServer, self.server)
        srv.records.append((self.command, self.path, dict(self.headers), body))
        if self.path == "/me" and srv.reconfigure_body is not None:
            srv.body, srv.reconfigure_body = srv.reconfigure_body, None
        if self.path == "/api/config":
            status, response = srv.status, srv.body
            if srv.delay:
                time.sleep(srv.delay)
        elif self.path == "/auth/login":
            status = srv.auth_status
            response = json.dumps({"token": TOKEN, "subject": IDENTITY}).encode()
            if status == 202:
                response = b'{"two_factor_required":true}'
        else:
            status, response = srv.auth_status, json.dumps(IDENTITY).encode()
        try:
            self.send_response(status)
            self.send_header("Content-Length", str(len(response) + (1 if srv.partial else 0)))
            self.send_header("Set-Cookie", "ambient=forbidden")
            if srv.redirect:
                self.send_header("Location", srv.redirect)
            for _ in range(srv.headers):
                self.send_header("X-Pad", "x" * 1000)
            self.end_headers()
            self.wfile.write(response)
        except (OSError, TimeoutError):
            pass
        self.close_connection = True

    do_GET = do_POST = handle_request


class ProtocolTests(unittest.TestCase):
    # Reuse certificate construction without inheriting/rerunning HTTP tests.
    certdir: Path

    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    tls = test_http.HTTPTests.tls

    @contextlib.contextmanager
    def fixture(self, body=CONFIG, status=200, cert="good", auth_status=200,
                redirect=None, delay=0, headers=0, partial=False, reconfigure_body=None):
        srv = ProtocolServer(("127.0.0.1", 0), Handler)
        srv.records = []
        srv.body = body if isinstance(body, bytes) else json.dumps(body).encode()
        srv.status, srv.auth_status = status, auth_status
        srv.redirect, srv.delay, srv.headers, srv.partial = redirect, delay, headers, partial
        srv.reconfigure_body = None if reconfigure_body is None else (
            reconfigure_body if isinstance(reconfigure_body, bytes) else json.dumps(reconfigure_body).encode())
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.02})
        thread.start()
        try:
            yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally:
            srv.shutdown(); srv.server_close(); thread.join(timeout=3)
            self.assertFalse(thread.is_alive())

    def request(self, mode, origin, client=5, required=4, ca="ca.pem", settings="ok", env=None):
        path = self.certdir / "protocol-secret"
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as stream:
            stream.write(TOKEN)
        path.chmod(0o600)
        prefix = shlex.split(os.environ.get("GIN_PROTOCOL_PREFIX", ""))
        if prefix and settings == "ok":
            settings = "instrumented"
        p = subprocess.run(prefix +
                           [str(ROOT / ".build/protocol_driver"), mode, origin,
                            str(self.certdir / ca), str(path), str(client), str(required), settings],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10, env=env)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace"))
        self.assertEqual(p.stderr, b"")
        self.assertNotIn(TOKEN.encode(), p.stdout + p.stderr)
        return p.stdout.decode().strip()

    def assert_discovery(self, records):
        self.assertEqual(records[0][:2], ("GET", "/api/config"))
        headers, body = records[0][2:]
        self.assertEqual(body, b"")
        for header in ["Authorization", "Cookie", "Proxy-Authorization"]:
            self.assertNotIn(header, headers)

    def test_gated_login_order(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.request("login", origin), "ok 1 ok")
            self.assertEqual([r[:2] for r in srv.records],
                             [("GET", "/api/config"), ("POST", "/auth/login")])
            self.assert_discovery(srv.records)
            self.assertEqual(json.loads(srv.records[1][3]), {"identifier": "Alice", "password": TOKEN})
            self.assertNotIn("Cookie", srv.records[1][2])
            self.assertNotIn("Authorization", srv.records[1][2])

    def test_rejected_discovery_never_sends_credentials(self):
        cases = [(b"", "malformed-config"), (b"{", "malformed-config"),
                 ({}, "malformed-config"), ({**CONFIG, "server": "other"}, "unexpected-server"),
                 ({**CONFIG, "protocol_version": 3}, "server-protocol-too-old"),
                 ({**CONFIG, "min_protocol_version": 6}, "client-protocol-too-old")]
        for field in ["protocol_version", "min_protocol_version"]:
            for value in [None, True, "7", 0, -1, 1.5, 2147483648, 10**80]:
                cases.append(({**CONFIG, field: value}, "malformed-config"))
            missing = dict(CONFIG)
            del missing[field]
            cases.append((missing, "malformed-config"))
        cases.extend([(b'{"server":"grappa","server":"grappa"}', "malformed-config"),
                      (b'{"server":"grappa","\\u0073erver":"grappa"}', "malformed-config"),
                      (b'{"server":"grappa","x":"\\u0000"}', "malformed-config"),
                      (b'{"server":"grappa","x":"\xff"}', "malformed-config"),
                      (json.dumps(CONFIG).encode() + b'\x00', "malformed-config"),
                      ({**CONFIG, "min_protocol_version": 8}, "malformed-config")])
        for body, expected in cases:
            for mode in ["login", "resume"]:
                with self.subTest(body=body, mode=mode), self.fixture(body) as (srv, origin):
                    self.assertEqual(self.request(mode, origin), expected + " 0 not-attempted")
                    self.assertEqual(len(srv.records), 1)
                    self.assert_discovery(srv.records)

    def test_independent_floors_and_diagnostic_additions(self):
        cases = [({**CONFIG, "protocol_version": 4, "min_protocol_version": 4}, 4, 4),
                 ({**CONFIG, "protocol_version": 99}, 5, 4),
                 ({**CONFIG, "protocol_version": 4}, 99, 4),
                 ({**CONFIG, "protocol_version": 20}, 3, 10)]
        for body, client, required in cases:
            with self.fixture(body) as (srv, origin):
                self.assertEqual(self.request("fetch", origin, client, required), "ok")
                self.assertEqual(len(srv.records), 1)
        for version in [None, True, 9, "", "not-a-release", "x" * 256, "bad\n", "bad\u0085"]:
            with self.fixture({**CONFIG, "version": version, "unknown": [True, {"x": "y"}],
                               "push_content_encoding": "not-an-e2ee-capability"}) as (srv, origin):
                self.assertEqual(self.request("fetch", origin), "ok")

    def test_resume_and_fresh_preflight_each_call(self):
        with self.fixture() as (srv, origin):
            for _ in range(2):
                self.assertEqual(self.request("resume", origin), "ok 1 ok")
            self.assertEqual([r[:2] for r in srv.records],
                             [("GET", "/api/config"), ("GET", "/me")] * 2)
            for i in [0, 2]:
                self.assert_discovery(srv.records[i:])
                self.assertEqual(srv.records[i + 1][2]["Authorization"], "Bearer " + TOKEN)
                self.assertNotIn("Cookie", srv.records[i + 1][2])
            srv.body = json.dumps({**CONFIG, "min_protocol_version": 6}).encode()
            self.assertEqual(self.request("resume", origin), "client-protocol-too-old 0 not-attempted")
            self.assertEqual(len(srv.records), 5)
        with self.fixture(reconfigure_body={**CONFIG, "min_protocol_version": 6}) as (srv, origin):
            self.assertEqual(self.request("resume-twice", origin),
                             "ok 1 ok\nclient-protocol-too-old 0 not-attempted")
            self.assertEqual([r[:2] for r in srv.records],
                             [("GET", "/api/config"), ("GET", "/me"), ("GET", "/api/config")])
            for i in [0, 2]:
                self.assert_discovery(srv.records[i:])
            self.assertEqual(srv.records[1][2]["Authorization"], "Bearer " + TOKEN)
            self.assertNotIn("Cookie", srv.records[1][2])

    def test_auth_failures_preserve_phase_and_clear_output(self):
        for mode, status, expected in [("login", 401, "unauthorized"),
                                      ("resume", 401, "unauthorized"),
                                      ("login", 202, "unsupported-interactive-auth")]:
            with self.fixture(auth_status=status) as (srv, origin):
                self.assertEqual(self.request(mode, origin), "ok 1 " + expected)
                self.assertEqual(len(srv.records), 2)
                self.assert_discovery(srv.records)

    def test_invalid_policy_origin_and_deadlines_have_no_requests(self):
        with self.fixture() as (srv, origin):
            for client, required in [(0, 4), (-1, 4), (5, 0), (5, -1)]:
                self.assertEqual(self.request("login", origin, client, required),
                                 "invalid-settings 0 not-attempted")
            for bad in [origin + "/", origin + "?x", origin + "#x",
                        origin.replace("https:", "http:"), origin.replace("//", "//user:pass@")]:
                self.assertEqual(self.request("resume", bad), "invalid-settings 0 not-attempted")
            self.assertEqual(self.request("login", origin, settings="bad-timeout"),
                             "invalid-settings 0 not-attempted")
            self.assertEqual(srv.records, [])

    def test_tls_status_redirects_and_ambient_isolation(self):
        for cert, ca in [("good", "bad.pem"), ("wrong", "ca.pem")]:
            with self.fixture(cert=cert) as (srv, origin):
                self.assertEqual(self.request("login", origin, ca=ca), "tls 0 not-attempted")
                self.assertEqual(srv.records, [])
        with self.fixture() as (target, destination):
            for redirect in [destination + "/api/config", destination.replace("https:", "http:")]:
                with self.fixture(status=302, redirect=redirect) as (srv, origin):
                    self.assertEqual(self.request("login", origin), "remote-status 0 not-attempted")
                    self.assertEqual(len(srv.records), 1)
            self.assertEqual(target.records, [])
        for status in [201, 202, 204, 304, 401, 404, 500]:
            with self.fixture(status=status) as (srv, origin):
                self.assertEqual(self.request("login", origin), "remote-status 0 not-attempted")
                self.assertEqual(len(srv.records), 1)
        home = self.certdir / "ambient-home"
        home.mkdir()
        (home / ".netrc").write_text("machine 127.0.0.1 login fixture password forbidden\n")
        (home / ".netrc").chmod(0o600)
        (home / ".curlrc").write_text("insecure\nuser = fixture:forbidden\n")
        env = dict(os.environ, HOME=str(home), CURL_HOME=str(home),
                   HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
        with self.fixture() as (srv, origin):
            self.assertEqual(self.request("resume", origin, env=env), "ok 1 ok")
            self.assert_discovery(srv.records)

    def test_body_depth_headers_deadline_and_transport_limits(self):
        for size, expected in [(65536, "ok"), (65537, "limit")]:
            base = json.dumps({**CONFIG, "pad": ""}).encode()
            body = json.dumps({**CONFIG, "pad": "x" * (size - len(base))}).encode()
            self.assertEqual(len(body), size)
            with self.fixture(body) as (srv, origin):
                self.assertEqual(self.request("fetch", origin), expected)
                self.assertEqual(len(srv.records), 1)
        for wrappers, expected in [(31, "ok"), (32, "malformed-config")]:
            value = '\"]}[{\\\"'
            for _ in range(wrappers):
                value = [value]
            with self.fixture({**CONFIG, "unknown": value}) as (srv, origin):
                self.assertEqual(self.request("fetch", origin), expected)
        for kwargs, expected in [(dict(headers=70), "limit"), (dict(delay=1), "timeout"),
                                 (dict(partial=True), "transport")]:
            with self.fixture(**kwargs) as (srv, origin):
                started = time.monotonic()
                self.assertEqual(self.request("login", origin), expected + " 0 not-attempted")
                self.assertLess(time.monotonic() - started, 3)
                self.assertEqual(len(srv.records), 1)
        reservation = socket.socket()
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
        reservation.close()
        self.assertEqual(self.request("resume", f"https://127.0.0.1:{port}"),
                         "transport 0 not-attempted")


if __name__ == "__main__":
    unittest.main(verbosity=2)
