"""Native libcurl WSS against one synthetic HTTPS authority, never live Grappa."""
import base64
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import shlex
import socket
import subprocess
import threading
import unittest
import test_http
from ws_fixture_frames import server_frame, take_client_frame
ROOT = Path(__file__).resolve().parents[1]
TOKEN = "aa~"
CONFIG = {"server": "grappa", "protocol_version": 7, "min_protocol_version": 1}
class Server(http.server.ThreadingHTTPServer):
    mode: str
    records: list
    joins: list
    upgrades: int
    daemon_threads = True
    def handle_error(self, request, client_address):
        pass
class Handler(http.server.BaseHTTPRequestHandler):
    server: Server
    protocol_version = "HTTP/1.1"
    def log_message(self, format, *args): pass
    def do_GET(self):
        srv = self.server
        srv.records.append((self.path, dict(self.headers)))
        if self.path == "/api/config":
            body = dict(CONFIG)
            if srv.mode == "fresh" and srv.upgrades: body["min_protocol_version"] = 9
        elif self.path == "/me":
            body = {"kind": "user", "id": "12345678-1234-1234-1234-123456789abc", "name": "Alice"}
            if srv.mode == "uuid": body["id"] = "22345678-1234-1234-1234-123456789abc"
        else:
            self.upgrade(); return
        raw = json.dumps(body).encode()
        self.send_response(200); self.send_header("Content-Length", str(len(raw))); self.end_headers()
        self.wfile.write(raw)
    def upgrade(self):
        srv = self.server; srv.upgrades += 1
        if srv.mode in ("403", "426", "302"):
            self.send_response(int(srv.mode)); self.send_header("Content-Length", "0")
            self.send_header("Location", "http://127.0.0.1:1/leak"); self.end_headers(); return
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101); self.send_header("Upgrade", "websocket"); self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        if srv.mode != "missing": self.send_header("Sec-WebSocket-Protocol", "wrong" if srv.mode == "wrong" else "phoenix")
        if srv.mode == "duplicate": self.send_header("Sec-WebSocket-Protocol", "phoenix")
        self.end_headers(); self.wfile.flush()
        self.connection.settimeout(3)
        buf = bytearray()
        try:
            for index in range(3):
                while True:
                    item = take_client_frame(buf)
                    if item is not None: break
                    data = self.connection.recv(16384)
                    if not data: return
                    buf.extend(data)
                opcode, final, payload = item
                if opcode == 10:
                    while True:
                        item = take_client_frame(buf)
                        if item is not None: break
                        data = self.connection.recv(16384)
                        if not data: return
                        buf.extend(data)
                    opcode, final, payload = item
                assert opcode == 1 and final
                frame = json.loads(payload); srv.joins.append(frame)
                jr, ref, topic, event, body = frame
                assert event == "phx_join" and body == {} and jr == ref
                response = {"protocol_version": 7} if index == 0 else {}
                reply = [jr, ref, topic, "phx_reply", {"status": "ok", "response": response}]
                raw = json.dumps(reply).encode()
                if srv.mode == "stale" and srv.upgrades > 1 and index == 0:
                    old = srv.joins[0]
                    stale = [old[0], old[1], old[2], "phx_reply", {"status":"error", "response":{}}]
                    self.connection.sendall(server_frame(json.dumps(stale).encode()))
                if srv.mode == "silent":
                    self.connection.recv(1024); return
                if srv.mode == "root-floor" and index == 0:
                    reply[4]["response"]["protocol_version"] = 3
                    raw = json.dumps(reply).encode()
                if srv.mode in ("reject-root", "reject-child", "unknown") and index == (1 if srv.mode == "reject-child" else 0):
                    reply[4]["status"] = "error" if srv.mode != "unknown" else "future"
                    raw = json.dumps(reply).encode()
                if srv.mode == "binary":
                    self.connection.sendall(server_frame(raw, opcode=2)); return
                if srv.mode == "close":
                    self.connection.sendall(server_frame(b"", opcode=8)); return
                if srv.mode == "correlated":
                    for slot, value in [(0, None), (0, "stale"), (1, "wrong"), (2, "unrelated")]:
                        wrong = list(reply); wrong[slot] = value
                        self.connection.sendall(server_frame(json.dumps(wrong).encode()))
                    # A complete valid JSON prefix is NOT a complete WS message.
                    self.connection.sendall(server_frame(raw, final=False))
                    self.connection.sendall(server_frame(b"ping", opcode=9))
                    self.connection.settimeout(.05)
                    try:
                        premature = self.connection.recv(4096)
                        # Automatic pong is permitted, a child join is not.
                        extra = bytearray(premature)
                        while extra:
                            control = take_client_frame(extra)
                            if control is None: break
                            assert control[0] == 10, "premature child join"
                    except TimeoutError: pass
                    self.connection.settimeout(3)
                    self.connection.sendall(server_frame(b"", opcode=0))
                else:
                    self.connection.sendall(server_frame(raw))
            if srv.mode == "events":
                topic = srv.joins[0][2]; jr = srv.joins[0][0]
                frames = [["stale", None, topic, "event", {}],
                          [None, None, "unjoined", "event", {}],
                          [jr, "1", topic, "phx_reply", {"status":"ok", "response":{}}],
                          [None, None, topic, "event", {"text":"café"}],
                          [jr, None, topic, "event", {"text":"escape\\tail"}]]
                for f in frames:
                    raw = json.dumps(f, ensure_ascii=False).encode()
                    split = raw.index(b"\xc3") + 1 if b"\xc3" in raw else len(raw) // 2
                    self.connection.sendall(server_frame(raw[:split], final=False) + server_frame(b"p", opcode=9) + server_frame(raw[split:], opcode=0))
            # Keep socket alive through pongs until the explicit owner close.
            while self.connection.recv(1024):
                pass
        except (OSError, ValueError): pass
        finally: self.close_connection = True
class ConnectTests(unittest.TestCase):
    certdir: Path
    @classmethod
    def setUpClass(cls): test_http.HTTPTests.setUpClass.__func__(cls)
    @classmethod
    def tearDownClass(cls): test_http.HTTPTests.tearDownClass.__func__(cls)
    tls = test_http.HTTPTests.tls
    @contextlib.contextmanager
    def fixture(self, mode="ok", cert="good"):
        srv = Server(("127.0.0.1", 0), Handler)
        srv.mode = mode; srv.records = []; srv.joins = []; srv.upgrades = 0
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": .02}); thread.start()
        try: yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally: srv.shutdown(); srv.server_close(); thread.join(3); self.assertFalse(thread.is_alive())
    def request(self, origin, mode="ready", ca="ca.pem"):
        secret = self.certdir / "connect-secret"
        secret.touch(mode=0o600); secret.write_text(TOKEN); secret.chmod(0o600)
        binary = os.environ.get("GIN_CONNECT_DRIVER", str(ROOT / ".build/connect_driver"))
        p = subprocess.run(shlex.split(os.environ.get("GIN_CONNECT_PREFIX", "")) + [binary, origin,
            str(self.certdir / ca), str(secret), mode], capture_output=True, timeout=15)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace")); self.assertEqual(p.stderr, b"")
        self.assertNotIn(TOKEN.encode(), p.stdout + p.stderr)
        return p.stdout.decode().splitlines()
    def test_root_before_network_before_window(self):
        with self.fixture() as (srv, origin):
            self.assertEqual(self.request(origin), ["ok 101 3 1"])
            self.assertEqual([x[0] for x in srv.records], ["/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=4"])
            self.assertNotIn("Authorization", srv.records[0][1])
            self.assertEqual(srv.records[1][1]["Authorization"], "Bearer " + TOKEN)
            self.assertEqual(srv.records[2][1]["Sec-WebSocket-Protocol"], "phoenix,base64url.bearer.phx.YWF+")
            self.assertNotIn("Authorization", srv.records[2][1])
            self.assertEqual([x[2] for x in srv.joins], ["grappa:user:Alice", "grappa:user:Alice/network:Net", "grappa:user:Alice/network:Net/channel:#room"])
    def test_joined_application_events_and_idle_cpu(self):
        for mode in ["events", "idle"]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.request(origin, mode), ["ok 101 3 1"])
    def test_correlated_replies_and_delayed_fin(self):
        with self.fixture("correlated") as (srv, origin):
            self.assertEqual(self.request(origin), ["ok 101 3 1"])
            self.assertEqual(len(srv.joins), 3)
    def test_reconnect_fresh_preflight_and_monotonic_refs(self):
        with self.fixture("stale") as (srv, origin):
            self.assertEqual(self.request(origin, "twice"), ["ok 101 3 1", "ok 101 3 2"])
            self.assertEqual([int(x[1]) for x in srv.joins], list(range(1, 7)))
            self.assertEqual([x[0] for x in srv.records].count("/api/config"), 2)
    def test_handshake_status_and_selected_protocol(self):
        for mode, expected in [("403", "forbidden 403"), ("426", "upgrade-required 426"),
                               ("302", "handshake 302"), ("missing", "handshake 101"),
                               ("wrong", "handshake 101"), ("duplicate", "handshake 101")]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.request(origin, "failure"), [expected + " 4 1"])
                self.assertEqual(srv.joins, [])
    def test_all_or_nothing_join_and_binary_close(self):
        for mode, code in [("reject-root", "join-rejected"), ("reject-child", "join-rejected"),
                           ("unknown", "join-rejected"), ("root-floor", "preflight"),
                           ("binary", "malformed"), ("close", "closed"), ("silent", "timeout")]:
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                self.assertEqual(self.request(origin, "failure"), [code + " 101 4 1"])
                self.assertEqual(len(srv.joins), 2 if mode == "reject-child" else 1)
    def test_trust_uuid_and_fresh_policy(self):
        for cert, ca in [("good", "bad.pem"), ("wrong", "ca.pem")]:
            with self.fixture(cert=cert) as (srv, origin):
                self.assertEqual(self.request(origin, "failure", ca), ["preflight 0 4 1"])
                self.assertEqual(srv.records, [])
        with self.fixture("uuid") as (srv, origin):
            self.assertEqual(self.request(origin, "failure"), ["unauthorized 0 4 1"])
            self.assertEqual([x[0] for x in srv.records], ["/api/config", "/me"])
        with self.fixture("fresh") as (srv, origin):
            self.assertEqual(self.request(origin, "fresh"), ["ok 101 3 1", "preflight 0 4 2"])
            self.assertEqual([x[0] for x in srv.records], ["/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=4", "/api/config"])
if __name__ == "__main__": unittest.main(verbosity=2)
