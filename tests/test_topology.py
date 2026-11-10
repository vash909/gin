#!/usr/bin/env python3
"""Native, synthetic-only authenticated BOOT discovery over verified TLS."""
import contextlib
import http.server
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import threading
import unittest
from typing import Optional, cast
import test_http
from test_auth import IDENTITY, TOKEN

ROOT = Path(__file__).resolve().parents[1]
CONFIG = {"server": "grappa", "protocol_version": 7, "min_protocol_version": 3}
BOOT = {"networks": [{"id": 3000000001, "slug": "NetA", "nick": "OwnA"},
                     {"id": 2, "slug": "net_b", "nick": "OwnB"}],
        "channels": {"NetA": [{"name": "#Same", "joined": True, "source": "joined"}],
                     "net_b": [{"name": "#Same", "joined": False, "source": "autojoin"}]},
        "heads": {"NetA": {"#Same": [{"private": "never-render-this-head"}],
                            "Peer": [{"text": "not-DM-evidence"}]}}}
OK, INVALID, MALFORMED, CONFLICT, LIMIT, NOMEM, WRONG_SERVER, CLIENT_OLD, SERVER_OLD, UNAUTHORIZED, SCOPE, FORBIDDEN, THROTTLED, SUBJECT, TLS, TIMEOUT, TRANSPORT, REMOTE = range(18)


class TopologyServer(http.server.ThreadingHTTPServer):
    records: list
    responses: dict
    reconfigure: Optional[dict]
    redirect: Optional[str]
    partial_path: Optional[str]
    delay_path: Optional[str]
    header_path: Optional[str]
    delay_seconds: float
    stopped: threading.Event


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        self.connection.settimeout(2)
        srv = cast(TopologyServer, self.server)
        body = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        srv.records.append((self.command, self.path, dict(self.headers), body))
        status, response = srv.responses.get(self.path, (404, b""))
        if self.path == "/boot" and srv.reconfigure:
            srv.responses["/api/config"] = (200, encode(srv.reconfigure))
        if self.path == srv.delay_path:
            srv.stopped.wait(srv.delay_seconds)
        try:
            self.send_response(status)
            self.send_header("Content-Length", str(len(response) + (1 if self.path == srv.partial_path else 0)))
            self.send_header("Set-Cookie", "ambient=forbidden")
            if srv.redirect:
                self.send_header("Location", srv.redirect)
            if self.path == srv.header_path:
                for _ in range(70):
                    self.send_header("X-Pad", "x" * 1000)
            self.end_headers()
            self.wfile.write(response)
        except (OSError, TimeoutError):
            pass
        self.close_connection = True


def encode(value):
    return value if isinstance(value, bytes) else json.dumps(value).encode()


class TopologyTests(unittest.TestCase):
    certdir: Path

    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    tls = test_http.HTTPTests.tls

    @contextlib.contextmanager
    def fixture(self, config=CONFIG, boot=BOOT, me=IDENTITY, config_status=200,
                boot_status=200, me_status=200, cert="good", reconfigure=None,
                redirect=None, partial_path=None, delay_path=None, header_path=None, delay_seconds=1.2):
        srv = TopologyServer(("127.0.0.1", 0), Handler)
        srv.daemon_threads = False
        srv.records = []
        srv.responses = {"/api/config": (config_status, encode(config)),
                         "/me": (me_status, encode(me)), "/boot": (boot_status, encode(boot))}
        srv.reconfigure, srv.redirect = reconfigure, redirect
        srv.partial_path, srv.delay_path, srv.header_path = partial_path, delay_path, header_path
        srv.delay_seconds = delay_seconds
        srv.stopped = threading.Event()
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.02})
        thread.start()
        try:
            yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally:
            srv.stopped.set()
            srv.shutdown(); srv.server_close(); thread.join(timeout=3)
            self.assertFalse(thread.is_alive())

    def request(self, origin, client=5, required=4, mode="ok", calls=1, ca="ca.pem", env=None):
        path = self.certdir / "topology-private.json"
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        with os.fdopen(fd, "w") as stream:
            json.dump({"origin": origin, "token": TOKEN, "subject_id": IDENTITY["id"],
                       "subject_name": IDENTITY["name"]}, stream)
        path.chmod(0o600)
        prefix = shlex.split(os.environ.get("GIN_TOPOLOGY_PREFIX", ""))
        if prefix and mode in ("ok", "http-deadline"):
            mode = "instrumented"
        binary = os.environ.get("GIN_TOPOLOGY_DRIVER", str(ROOT / ".build/topology_driver"))
        p = subprocess.run(prefix + [binary, origin,
                           "-" if ca is None else str(self.certdir / ca), str(path),
                           str(client), str(required), mode, str(calls)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=20, env=env)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace"))
        self.assertEqual(p.stderr, b"")
        self.assertNotIn(TOKEN.encode(), p.stdout + p.stderr)
        self.assertNotIn(b"never-render-this-head", p.stdout + p.stderr)
        return p.stdout.decode().splitlines()

    def assert_flow(self, srv, origin, paths):
        self.assertEqual([r[:2] for r in srv.records], [("GET", path) for path in paths])
        for _, path, headers, body in srv.records:
            self.assertEqual(body, b"")
            self.assertEqual(headers["Host"], origin.split("//")[1])
            self.assertNotIn("Cookie", headers)
            self.assertNotIn("Proxy-Authorization", headers)
            if path == "/api/config":
                self.assertNotIn("Authorization", headers)
            else:
                self.assertEqual(headers.get("Authorization"), "Bearer " + TOKEN)

    def test_local_invalid_sends_nothing(self):
        with self.fixture() as (srv, origin):
            for client, required in [(0, 4), (-1, 4), (5, 0), (5, -1)]:
                with self.subTest(client=client, required=required):
                    self.assertEqual(self.request(origin, client, required), ["1 0 0 0"])
            for mode in ["bad-timeout", "bad-total", "bad-order", "bad-chantypes",
                         "null-chantypes", "bad-uuid", "bad-token", "bad-credential-origin",
                         "wrong-origin", "occupied", "null-out", "null-settings",
                         "null-policy", "null-credential"]:
                with self.subTest(mode=mode):
                    self.assertEqual(self.request(origin, mode=mode), ["1 0 0 0"])
            for bad in [origin + "/", origin + "?q", origin + "#f",
                        origin.replace("https:", "http:"), origin.replace("//", "//user:pass@")]:
                self.assertEqual(self.request(bad), ["1 0 0 0"])
            self.assertEqual(srv.records, [])

    def test_config_gate_and_feature_floor(self):
        cases = [({}, MALFORMED), (b"{", MALFORMED),
                 ({**CONFIG, "server": "other"}, WRONG_SERVER),
                 ({**CONFIG, "protocol_version": 3, "min_protocol_version": 1}, SERVER_OLD),
                 ({**CONFIG, "min_protocol_version": 6}, CLIENT_OLD)]
        for config, expected in cases:
            with self.subTest(expected=expected), self.fixture(config=config) as (srv, origin):
                self.assertEqual(self.request(origin, required=1), [f"{expected} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config"])
        for config, client, required in [({**CONFIG, "protocol_version": 99}, 5, 4),
                                         ({**CONFIG, "protocol_version": 4}, 99, 1),
                                         ({**CONFIG, "protocol_version": 20}, 3, 10)]:
            with self.fixture(config=config) as (srv, origin):
                self.assertEqual(self.request(origin, client, required)[0], "0 2 2 0")
                self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])

    def test_identity_failures_prevent_boot(self):
        for status, me, expected in [(401, IDENTITY, UNAUTHORIZED),
                                     (403, {"error": "client_token_scope"}, SCOPE),
                                     (403, {"error": "other"}, FORBIDDEN),
                                     (429, IDENTITY, THROTTLED),
                                     (200, {**IDENTITY, "id": "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"}, SUBJECT),
                                     (200, {**IDENTITY, "kind": "visitor"}, SUBJECT),
                                     (200, b"{", MALFORMED)]:
            with self.subTest(status=status, expected=expected), self.fixture(me=me, me_status=status) as (srv, origin):
                self.assertEqual(self.request(origin), [f"{expected} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config", "/me"])

    def test_fresh_preflight_same_process(self):
        with self.fixture(reconfigure={**CONFIG, "min_protocol_version": 6}) as (srv, origin):
            result = self.request(origin, calls=2)
            self.assertEqual(result[0], "0 2 2 0")
            self.assertEqual(result[-1], "7 0 0 0")
            self.assert_flow(srv, origin, ["/api/config", "/me", "/boot", "/api/config"])

    def test_completed_boot_status_classification(self):
        cases = [(201, BOOT, REMOTE), (204, b"", REMOTE), (302, b"", REMOTE),
                 (401, b"{", UNAUTHORIZED), (429, b"", THROTTLED), (500, BOOT, REMOTE),
                 (403, {"error": "client_token_scope"}, SCOPE),
                 (403, {"error": "other"}, FORBIDDEN), (403, b"{", FORBIDDEN),
                 (403, b'{"error":"client_token_scope","error":"other"}', FORBIDDEN),
                 (403, b'{"error":"client_token_scope","x":"\\u0000"}', FORBIDDEN),
                 (403, b'{"error":"client_token_scope"}\x00', FORBIDDEN),
                 (403, b'{"error":"client_token_scope","x":' + b'[' * 33 + b'0' + b']' * 33 + b'}', FORBIDDEN)]
        for status, boot, expected in cases:
            with self.subTest(status=status, expected=expected), self.fixture(boot=boot, boot_status=status) as (srv, origin):
                self.assertEqual(self.request(origin), [f"{expected} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])

    def test_boot_parser_failure_is_not_published(self):
        for body, expected in [(b"", MALFORMED), (b"{", MALFORMED),
                               ({}, MALFORMED), ({**BOOT, "networks": {}}, MALFORMED),
                               ({**BOOT, "heads": []}, MALFORMED),
                               (b'{"networks":[],"channels":{},"heads":{},"heads":{}}', MALFORMED),
                               (b'{"networks":[],"channels":{},"heads":{},"x":"\\xff"}', MALFORMED),
                               (b'{"networks":[],"channels":{},"heads":{},"x":"\xff"}', MALFORMED),
                               ({**BOOT, "networks": BOOT["networks"] * 2}, CONFLICT)]:
            with self.subTest(expected=expected), self.fixture(boot=body) as (srv, origin):
                self.assertEqual(self.request(origin), [f"{expected} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])

    def test_body_depth_and_response_limits(self):
        empty = b'{"networks":[],"channels":{},"heads":{}}'
        for body, expected in [(empty, OK),
                               (empty + b' ' * (1048576 - len(empty)), OK),
                               (empty + b' ' * (1048577 - len(empty)), LIMIT),
                               (b'{"networks":[],"channels":{},"heads":{},"x":' +
                                b'[' * 33 + b'0' + b']' * 33 + b'}', LIMIT),
                               (empty + b'\x00', MALFORMED)]:
            with self.subTest(size=len(body)), self.fixture(boot=body) as (srv, origin):
                self.assertEqual(self.request(origin), [f"{expected} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])
        for status in [200, 401, 403, 429, 500]:
            with self.fixture(boot_status=status, partial_path="/boot") as (srv, origin):
                self.assertEqual(self.request(origin), [f"{TRANSPORT} 0 0 0"])
        with self.fixture(header_path="/boot") as (srv, origin):
            self.assertEqual(self.request(origin), [f"{LIMIT} 0 0 0"])

    def test_config_status_prevents_identity(self):
        for status in [201, 204, 302, 401, 403, 429, 500]:
            with self.subTest(status=status), self.fixture(config_status=status) as (srv, origin):
                self.assertEqual(self.request(origin), [f"{REMOTE} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config"])

    def test_connect_deadline_during_tls_handshake(self):
        with test_http.raw_server(b"") as url:
            origin = url.rstrip("/").replace("http:", "https:")
            self.assertEqual(self.request(origin, mode="deadline"), [f"{TIMEOUT} 0 0 0"])

    def test_explicit_remote_role_not_local_classification(self):
        boot = {"networks": [{"id": 1, "slug": "net", "nick": "Own"}],
                "channels": {"net": [{"name": "Peer", "joined": True, "source": "joined"}]},
                "heads": {"net": {"Own": [{"private": "never-render-this-head"}]}}}
        with self.fixture(boot=boot) as (srv, origin):
            self.assertEqual(self.request(origin), ["0 1 1 0", "N 1 net 4F776E", "W 1 0 1 2 net/@peer"])
            self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])

    def test_tls_trust_hostname_and_no_credentials(self):
        for cert, ca in [("good", "bad.pem"), ("wrong", "ca.pem"), ("good", None),
                         ("good", "missing.pem")]:
            with self.subTest(cert=cert, ca=ca), self.fixture(cert=cert) as (srv, origin):
                self.assertEqual(self.request(origin, ca=ca), [f"{TLS} 0 0 0"])
                self.assertEqual(srv.records, [])

    def test_deadlines_and_refused_connection(self):
        delay_seconds = 6.0 if shlex.split(os.environ.get("GIN_TOPOLOGY_PREFIX", "")) else 1.2
        for path in ["/api/config", "/me", "/boot"]:
            with self.subTest(path=path), self.fixture(delay_path=path, delay_seconds=delay_seconds) as (srv, origin):
                self.assertEqual(self.request(origin, mode="http-deadline"), [f"{TIMEOUT} 0 0 0"])
                self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"][:["/api/config", "/me", "/boot"].index(path)+1])
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        self.assertEqual(self.request(f"https://127.0.0.1:{port}"), [f"{TRANSPORT} 0 0 0"])

    def test_redirects_and_ambient_isolation(self):
        with self.fixture() as (target, destination):
            for location in [destination + "/secret", destination.replace("https:", "http:") + "/secret"]:
                with self.fixture(boot_status=302, redirect=location) as (srv, origin):
                    self.assertEqual(self.request(origin), [f"{REMOTE} 0 0 0"])
                    self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])
            self.assertEqual(target.records, [])
        home = self.certdir / "ambient-home"
        home.mkdir(exist_ok=True)
        (home / ".netrc").write_text("machine 127.0.0.1 login ambient password forbidden\n")
        (home / ".netrc").chmod(0o600)
        (home / ".curlrc").write_text("user = ambient:forbidden\ninsecure\n")
        env = dict(os.environ, HOME=str(home), CURL_HOME=str(home),
                   ALL_PROXY="http://127.0.0.1:1", all_proxy="http://127.0.0.1:1",
                   http_proxy="http://127.0.0.1:1", HTTPS_PROXY="http://127.0.0.1:1",
                   https_proxy="http://127.0.0.1:1", NO_PROXY="", no_proxy="")
        with self.fixture() as (srv, origin):
            self.assertEqual(self.request(origin, env=env)[0], "0 2 2 0")
            self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])

    def test_gated_multi_network_boot(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.request(origin), ["0 2 2 0", "N 2 net_b 4F776E42", "N 3000000001 %4Eet%41 4F776E41",
                             "W 2 0 0 1 net_b/same", "W 3000000001 0 1 2 %4Eet%41/same"])
            self.assert_flow(srv, origin, ["/api/config", "/me", "/boot"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
