#!/usr/bin/env python3
"""Synthetic TLS auth transactions through the real C API."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import shlex
import subprocess
import threading
import unittest
from typing import Optional, cast
from test_http import HTTPTests

ROOT = Path(__file__).resolve().parents[1]
TOKEN = "fixture-token.DO-NOT-LOG"
IDENTITY = {"kind": "user", "id": "12345678-1234-1234-1234-123456789abc", "name": "Alice"}


class AuthServer(http.server.ThreadingHTTPServer):
    records: list
    stateful: bool
    revoked: bool
    status: int
    body: bytes
    redirect: Optional[str]
    partial: bool


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def handle_request(self):
        self.connection.settimeout(3)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        srv = cast(AuthServer, self.server)
        srv.records.append((self.command, self.path, dict(self.headers), body))
        if srv.stateful:
            if self.command == "POST":
                status, response = 200, json.dumps({"token": TOKEN, "subject": IDENTITY}).encode()
            elif self.command == "DELETE":
                status, response = srv.status, b""
                if status == 204:
                    srv.revoked = True
            elif srv.revoked:
                status, response = 401, b'{}'
            else:
                status, response = 200, json.dumps(IDENTITY).encode()
        else:
            status, response = srv.status, srv.body
        self.send_response(status)
        self.send_header("Content-Length", str(len(response) + (16 if srv.partial else 0)))
        if srv.redirect:
            self.send_header("Location", srv.redirect)
        self.send_header("Set-Cookie", "ambient=forbidden")
        self.end_headers()
        self.wfile.write(response)
        if srv.partial:
            self.close_connection = True

    do_GET = do_POST = do_DELETE = handle_request


class AuthTests(HTTPTests):
    @contextlib.contextmanager
    def fixture(self, status=200, body=None, redirect=None, cert="good", stateful=False, partial=False):
        srv = AuthServer(("127.0.0.1", 0), Handler)
        srv.records = []
        srv.stateful = stateful
        srv.partial = partial
        srv.revoked = False
        srv.status = status
        srv.body = json.dumps(body).encode() if not isinstance(body, bytes) else body
        srv.redirect = redirect
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        t = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.02})
        t.start()
        try:
            yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally:
            srv.shutdown(); srv.server_close(); t.join(timeout=3)
            self.assertFalse(t.is_alive())

    def auth(self, mode, origin, ca="ca.pem", secret=TOKEN, env=None, root=None):
        path = self.certdir / "secret"
        path.write_text(secret)
        path.chmod(0o600)
        p = subprocess.run(shlex.split(os.environ.get("GIN_AUTH_PREFIX", "")) +
                           [str(ROOT / ".build/auth_driver"), mode, origin,
                            str(self.certdir / ca), str(path)] + ([str(root)] if root else []),
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=15, env=env)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace"))
        self.assertEqual(p.stderr, b"")
        self.assertNotIn(secret.encode(), p.stdout + p.stderr)
        return p.stdout.decode().strip()

    def test_login_wire(self):
        with self.fixture(body={"token": TOKEN, "subject": IDENTITY}) as (srv, origin):
            self.assertEqual(self.auth("login", origin), "ok")
            self.assertEqual(len(srv.records), 1)
            method, path, headers, body = srv.records[0]
            self.assertEqual((method, path), ("POST", "/auth/login"))
            self.assertEqual(headers["Content-Type"], "application/json")
            self.assertNotIn("Authorization", headers)
            self.assertEqual(json.loads(body), {"identifier": 'a"b\\c', "password": TOKEN})

    def test_password_json_escaping(self):
        password = 'password"\\\n\u00e9'
        with self.fixture(body={"token": TOKEN, "subject": IDENTITY}) as (srv, origin):
            self.assertEqual(self.auth("passwordlogin", origin, secret=password), "ok")
            self.assertEqual(json.loads(srv.records[0][3]),
                             {"identifier": 'a"b\\c', "password": password})
            self.assertNotIn("Authorization", srv.records[0][2])

    def test_valid_additions_and_escaped_duplicates(self):
        for token in [TOKEN, "fixture-token=="]:
            body = ('{"subject":{"name":"Al\\u0069ce","extra":[[true]],"id":"' +
                    IDENTITY["id"] + '","kind":"user"},"token":"' + token + '"}').encode()
            with self.fixture(body=body) as (srv, origin):
                self.assertEqual(self.auth("login", origin, secret=token), "ok")
        for body in [b'{"kind":"user","name":"Alice","\\u006eame":"Alice"}',
                     json.dumps({"token": TOKEN, "subject": IDENTITY}).encode() + b'{}',
                     b'{"unknown":' + b'[' * 65 + b'0' + b']' * 65 + b'}',
                     json.dumps({"token": TOKEN, "subject": {**IDENTITY, "name": "A\0lice"}}).encode()]:
            with self.fixture(body=body) as (srv, origin):
                self.assertEqual(self.auth("login", origin), "malformed-response")
        leaf = '"]}[{\\"\\\\'
        for wrappers, expected in [(31, "ok"), (32, "malformed-response")]:
            unknown = leaf
            for _ in range(wrappers):
                unknown = [unknown]
            body = json.dumps({"token": TOKEN, "subject": IDENTITY, "unknown": unknown}).encode()
            self.assertEqual(json.loads(body), {"token": TOKEN, "subject": IDENTITY, "unknown": unknown})
            with self.fixture(body=body) as (srv, origin):
                self.assertEqual(self.auth("login", origin), expected)
        for size, expected in [(65536, "ok"), (65537, "limit")]:
            pad = size - len(json.dumps({"token": TOKEN, "subject": IDENTITY, "pad": ""}).encode())
            body = json.dumps({"token": TOKEN, "subject": IDENTITY, "pad": "x" * pad}).encode()
            self.assertEqual(len(body), size)
            self.assertEqual(json.loads(body)["subject"], IDENTITY)
            with self.fixture(body=body) as (srv, origin):
                self.assertEqual(self.auth("login", origin), expected)

    def test_identity_and_logout_states(self):
        with self.fixture(body={**IDENTITY, "home_data": {"networks": []}}) as (srv, origin):
            self.assertEqual(self.auth("resume", origin), "ok")
            self.assertEqual(self.auth("identity", origin), "ok")
            self.assertEqual(len(srv.records), 2)
            for method, path, headers, body in srv.records:
                self.assertEqual((method, path, body), ("GET", "/me", b""))
                self.assertEqual(headers["Authorization"], "Bearer " + TOKEN)
                self.assertNotIn("Cookie", headers)
            self.assertEqual(self.auth("mismatch", origin), "invalid-settings")
            self.assertEqual(len(srv.records), 2)
        for status, body, expected in [(204, b"", "ok"),
                (401, {"error": "unauthorized"}, "unauthorized"),
                (503, {"error": "db_unavailable"}, "remote-status"),
                (403, {"error": "client_token_scope"}, "client-token-scope")]:
            with self.subTest(status=status), self.fixture(status, body) as (srv, origin):
                self.assertEqual(self.auth("logout", origin), expected)
                self.assertEqual(len(srv.records), 1)
                self.assertEqual(srv.records[0][:2], ("DELETE", "/auth/logout"))

    def test_stateful_revocation_and_explicit_persistence(self):
        for status, expected in [(204, "ok"), (503, "remote-status")]:
            with self.fixture(status, b"", stateful=True) as (srv, origin):
                root = self.certdir / ("root-" + str(status))
                self.assertEqual(self.auth("cycle", origin, root=root), expected)
                self.assertEqual([r[:2] for r in srv.records],
                                 [("POST", "/auth/login"), ("GET", "/me"),
                                  ("DELETE", "/auth/logout"), ("GET", "/me")])
                self.assertEqual(srv.revoked, status == 204)
                self.assertEqual((root / ".gin.credentials").exists(), status != 204)

    def test_fail_closed_responses(self):
        cases = [(401, b"{}", "unauthorized"), (429, b"{}", "throttled"),
                 (403, b'{"error":"client_token_scope"}', "client-token-scope"),
                 (403, b'{"error":"other"}', "forbidden"),
                 (503, b"{}", "remote-status"),
                 (202, b'{"two_factor_required":true,"challenge_token":"private"}', "unsupported-interactive-auth"),
                 (202, b'{"two_factor_required":"true"}', "malformed-response")]
        bad = [b"{", b'{} trailing', b'{"token":"x","token":"y"}',
               b'{"subject":[],"token":"x"}', b'{"token":42,"subject":{}}',
               b'{"token":"x\\u0000y","subject":{}}', b'{"bad":"\xff"}',
               b'{"unknown":' + b'[' * 2048 + b'0' + b']' * 2048 + b'}',
               json.dumps({"token": "x" * 4097, "subject": IDENTITY}).encode(),
               json.dumps({"token": TOKEN, "subject": {**IDENTITY, "id": 1}}).encode(),
               json.dumps({"token": TOKEN, "subject": {**IDENTITY, "id": "not-a-uuid"}}).encode(),
               json.dumps({"token": TOKEN, "subject": {**IDENTITY, "name": "a\n"}}).encode(),
               json.dumps({"token": TOKEN, "subject": {**IDENTITY, "name": "a" * 256}}).encode()]
        cases.extend((200, body, "malformed-response") for body in bad)
        cases.append((200, json.dumps({"token": TOKEN, "subject": {"kind": "visitor"}}).encode(),
                      "unsupported-or-changed-subject"))
        cases.append((200, b'x' * 65537, "limit"))
        for status, body, expected in cases:
            with self.subTest(status=status, expected=expected), self.fixture(status, body) as (srv, origin):
                self.assertEqual(self.auth("login", origin), expected)
                self.assertEqual(len(srv.records), 1)
        for body in [{"subject": IDENTITY}, {**IDENTITY, "id": "aaaaaaaa-1234-1234-1234-123456789abc"}]:
            with self.fixture(body=body) as (srv, origin):
                self.assertEqual(self.auth("identity", origin), "malformed-response" if "subject" in body
                                 else "unsupported-or-changed-subject")

    def test_uncertain_mutations_are_not_retried(self):
        for mode in ["login", "logout"]:
            with self.fixture(200, b'{}', partial=True) as (srv, origin):
                self.assertEqual(self.auth(mode, origin), "transport")
                self.assertEqual(len(srv.records), 1)
        with self.fixture(302, b'') as (srv, origin):
            srv.redirect = origin + "/me"
            self.assertEqual(self.auth("resume", origin), "remote-status")
            self.assertEqual(len(srv.records), 1)

    def test_tls_redirects_and_no_ambient_auth(self):
        for cert, ca in [("good", "bad.pem"), ("wrong", "ca.pem")]:
            with self.fixture(body=IDENTITY, cert=cert) as (srv, origin):
                self.assertEqual(self.auth("resume", origin, ca=ca), "tls")
                self.assertEqual(srv.records, [])
        with self.fixture(body=IDENTITY) as (target, destination):
            for redirect in [destination + "/me", destination.replace("https:", "http:") + "/me"]:
                with self.fixture(302, b"", redirect=redirect) as (srv, origin):
                    self.assertEqual(self.auth("resume", origin), "remote-status")
                    self.assertEqual(len(srv.records), 1)
            self.assertEqual(target.records, [])
        with self.fixture(body=IDENTITY) as (srv, origin):
            env = dict(os.environ, HTTPS_PROXY="http://127.0.0.1:1", ALL_PROXY="http://127.0.0.1:1", NO_PROXY="")
            self.assertEqual(self.auth("resume", origin, env=env), "ok")
            for bad in [origin + "/", origin + "?x", origin + "#x", origin.replace("https:", "http:"),
                        origin.replace("//", "//user:pass@")]:
                self.assertEqual(self.auth("resume", bad), "invalid-settings")
            self.assertEqual(len(srv.records), 1)


if __name__ == "__main__":
    unittest.main(verbosity=2)
