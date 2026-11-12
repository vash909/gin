"""Synthetic real native HTTPS + WSS history recovery and existing locked sink.

This is not connected CLI/live Grappa qualification. Visible drain is not a
complete archive. Checkpoints below are caller-owned in-memory values, not
persistent/atomic append acknowledgements. Fault-injected partial writes and
full planner cap matrices remain in the existing renderer/planner units.
"""
import base64
import contextlib
import hashlib
import http.server
import json
import os
from pathlib import Path
import queue
import selectors
import shlex
import socket
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from typing import cast

import test_http
from ws_fixture_frames import server_frame, take_client_frame

ROOT = Path(__file__).resolve().parents[1]
UUID = "12345678-1234-1234-1234-123456789abc"
TOKEN = "aa~"
UPGRADE = "/socket/websocket?vsn=2.0.0&client_proto=63"
OK, UNSUPPORTED, CONFLICT, STALE = 0, 7, 8, 9


def row(identifier, body=None, role="channel"):
    # Deliberately descending/equal timestamps: presentation order is numeric ID.
    r = dict(id=identifier, server_time=0 if identifier % 2 else 1000,
             network="Net", channel="OldSelf" if role == "query" else "#Room",
             kind="privmsg", sender='N"\\', body="same\n\x03" if body is None else body)
    if role == "query":
        r["dm_with"] = "Bob"
    return r


def golden(rows):
    # Independent fixed golden strings, not another general-purpose renderer.
    result = b"existing\n"
    for identifier in rows:
        stamp = "00.000" if identifier % 2 else "01.000"
        result += (f'[1970-01-01T00:00:{stamp}Z] privmsg '
                   'sender="N\\"\\\\" body="same\\x0A\\x03"\n').encode()
    return result


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, role, tls):
        super().__init__(("127.0.0.1", 0), Handler)
        self.role = role
        self.socket = tls.wrap_socket(self.socket, server_side=True)
        self.records, self.frames, self.errors = [], [], []
        self.actions = queue.Queue()
        self.stopped = threading.Event()
        self.wss_closed = threading.Event()
        self.during = None
        self.pages = {}
        self.topic = "grappa:user:Alice/network:Net/channel:" + ("oldself" if role == "query" else "#room")
        self.prefix = "/networks/Net/channels/" + ("Bob" if role == "query" else "%23Room") + "/messages?limit=2&after="

    def handle_error(self, request, client_address):
        import traceback
        self.errors.append(traceback.format_exc())

    def send(self, payload):
        sent = threading.Event()
        self.actions.put(([None, None, self.topic, "event", payload], sent))
        return sent


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        srv = cast(Server, self.server)
        srv.records.append((self.command, self.path, dict(self.headers)))
        if self.path == UPGRADE:
            self.upgrade()
            return
        if self.path == "/api/config":
            body = dict(server="grappa", protocol_version=63, min_protocol_version=1)
        elif self.path == "/me":
            body = dict(kind="user", id=UUID, name="Alice")
        else:
            assert self.path.startswith(srv.prefix), self.path
            anchor = int(self.path[len(srv.prefix):])
            assert anchor in srv.pages, ("unexpected REST cursor", anchor)
            if srv.during is not None:
                event, srv.during = srv.during, None
                assert srv.send(dict(kind="message", message=event)).wait(3)
            body = srv.pages[anchor]
        raw = json.dumps(body).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)
        self.close_connection = True

    def upgrade(self):
        srv = cast(Server, self.server)
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        for k, v in [("Upgrade", "websocket"), ("Connection", "Upgrade"),
                     ("Sec-WebSocket-Accept", accept), ("Sec-WebSocket-Protocol", "phoenix")]:
            self.send_header(k, v)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.02)
        srv.wss_closed.clear()
        buf = bytearray()
        joined = 0
        try:
            while not srv.stopped.is_set():
                try:
                    f, sent = srv.actions.get_nowait()
                except queue.Empty:
                    pass
                else:
                    raw = json.dumps(f).encode()
                    # Valid JSON prefix is NOT a boundary until explicit empty FIN.
                    self.connection.sendall(server_frame(raw, final=False) + server_frame(b"", opcode=0))
                    sent.set()
                item = take_client_frame(buf)
                if item is None:
                    try:
                        data = self.connection.recv(16384)
                    except socket.timeout:
                        continue
                    if not data:
                        return
                    buf.extend(data)
                    continue
                opcode, final, payload = item
                if opcode == 8:
                    return
                assert opcode == 1 and final
                f = json.loads(payload)
                srv.frames.append(f)
                jr, ref, topic, event, body = f
                assert event == "phx_join" and body == {} and jr == ref, "no outgoing operation replay"
                expected = ["grappa:user:Alice", "grappa:user:Alice/network:Net", srv.topic]
                assert topic == expected[joined]
                joined += 1
                response = {"protocol_version": 63} if joined == 1 else {}
                if joined == 3:
                    # An unrelated root event arrives while the child is still
                    # pending. Event count alone must never establish READY.
                    marker = [None, None, "grappa:user:Alice", "event", {"kind": "fixture_barrier"}]
                    self.connection.sendall(server_frame(json.dumps(marker).encode()))
                reply = [jr, ref, topic, "phx_reply", {"status": "ok", "response": response}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            # Only fixture teardown may swallow a valid-TLS socket failure.
            if not srv.stopped.is_set():
                raise
        finally:
            self.close_connection = True
            srv.wss_closed.set()


class Client:
    def __init__(self, test, origin, ca, root, role):
        self.test = test
        binary = os.environ.get("GIN_HISTORY_WSS_DRIVER", str(ROOT / ".build/history_driver"))
        self.p = subprocess.Popen(shlex.split(os.environ.get("GIN_HISTORY_PREFIX", "")) +
            [binary, origin, str(ca), str(root), role], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        assert self.p.stdin is not None and self.p.stdout is not None and self.p.stderr is not None
        self.receipt = self.read()

    def read(self):
        assert self.p.stdout is not None and self.p.stderr is not None
        with selectors.DefaultSelector() as sel:
            sel.register(self.p.stdout, selectors.EVENT_READ)
            self.test.assertTrue(sel.select(45 if os.environ.get("GIN_HISTORY_PREFIX") else 12), "native driver response deadline")
        raw = self.p.stdout.readline()
        if not raw:
            self.test.fail("native driver exited: " + self.p.stderr.read().decode(errors="replace"))
        self.test.assertNotIn(TOKEN.encode(), raw)
        return json.loads(raw)

    def command(self, command):
        assert self.p.stdin is not None
        self.p.stdin.write((command + "\n").encode())
        self.p.stdin.flush()
        result = self.read()
        self.test.assertEqual((result["query"], result["operation"]),
                              (self.receipt["query"], self.receipt["operation"]))
        return result

    def close(self):
        assert self.p.stdin is not None and self.p.stdout is not None and self.p.stderr is not None
        self.p.stdin.close()
        self.test.assertEqual(self.p.wait(5), 0, self.p.stderr.read().decode(errors="replace"))
        self.test.assertEqual(self.p.stderr.read(), b"")
        self.p.stdout.close()
        self.p.stderr.close()


class HistoryTests(unittest.TestCase):
    certdir: Path

    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    tls = test_http.HTTPTests.tls

    @contextlib.contextmanager
    def fixture(self, role="channel"):
        with tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()).resolve()) as tmp:
            root = Path(tmp).resolve() / "root"
            srv = Server(role, self.tls())
            thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": .01})
            thread.start()
            client = None
            try:
                client = Client(self, f"https://127.0.0.1:{srv.server_port}", self.certdir / "ca.pem", root, role)
                self.assertEqual(client.command("start")["ready"], 3)  # actual GIN_CONNECT_READY
                yield srv, client, root
            finally:
                srv.stopped.set()
                if client is not None:
                    if client.p.poll() is None:
                        client.close()
                srv.shutdown()
                srv.server_close()
                thread.join(3)
                self.assertFalse(thread.is_alive())
                self.assertEqual(srv.errors, [])

    def lock_held(self, root):
        # Separate exec process: never reopen/close the lock in its owner.
        probe = "import fcntl,sys; f=open(sys.argv[1],'r+'); fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)"
        p = subprocess.run([os.sys.executable, "-c", probe, str(root / ".gin.lock")], capture_output=True, timeout=3)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn(b"BlockingIOError", p.stderr)

    def flow(self, srv, anchors):
        expected = ["/api/config", "/me", UPGRADE]
        for anchor in anchors:
            expected += ["/api/config", "/me", srv.prefix + str(anchor)]
        self.assertEqual([r[1] for r in srv.records], expected)
        for method, path, headers in srv.records:
            self.assertEqual(method, "GET")
            self.assertNotIn(TOKEN, path)
            self.assertNotIn("Cookie", headers)
            if path == UPGRADE:
                self.assertNotIn("Authorization", headers)
                self.assertIn("phoenix", headers["Sec-WebSocket-Protocol"])
            elif path == "/api/config":
                self.assertNotIn("Authorization", headers)
            else:
                self.assertEqual(headers.get("Authorization"), "Bearer " + TOKEN)
        self.assertEqual([f[3] for f in srv.frames], ["phx_join"] * 3)

    def test_native_recovery_append(self):
        for role in ("channel", "query"):
            with self.subTest(role=role), self.fixture(role) as (srv, c, root):
                self.lock_held(root)
                outputs = list(root.rglob("out"))
                self.assertEqual(len(outputs), 1)
                out = outputs[0]
                inode = out.stat().st_ino
                srv.pages = {1: [row(2, role=role), row(3, role=role)],
                             3: [row(4, role=role), row(100, role=role)], 100: []}
                srv.send(dict(kind="message", message=row(100, role=role)))
                self.assertEqual(c.command("wait 1")["queued"], 1)  # actual client receipt
                self.assertEqual(c.command("stale")["code"], STALE)
                s = c.command("begin 1")
                self.assertEqual(s["unknown"], 0)
                srv.during = row(3, role=role)
                s = c.command("fetch 2")  # live arrives during HTTPS, receipt before REST feed
                self.assertEqual((s["after"], s["ids"], s["live"]), (3, [2, 3, 100], 2))
                self.assertEqual(out.read_bytes(), b"existing\n")  # no callback append
                self.assertEqual(c.command("fetch")["ids"], [2, 3, 4, 100])
                s = c.command("fetch")
                self.assertEqual((s["state"], s["after"], s["pages"]), (1, 100, 3))
                s = c.command("append")
                self.assertEqual(out.read_bytes(), golden([2, 3, 4, 100]))
                self.assertEqual(s["appended"], len(golden([2, 3, 4, 100])) - 9)
                self.assertEqual(s["checkpoint"], 100)
                self.assertEqual(out.stat().st_ino, inode)
                self.lock_held(root)
                # New attempt, trusted last-processed anchor (not /me/read cursor).
                srv.pages[100] = [row(101, role=role)]
                c.command("begin 100")
                s = c.command("fetch")
                self.assertEqual((s["ids"], s["unknown"]), ([101], 0))
                self.assertEqual(c.command("append")["checkpoint"], 101)
                self.assertEqual(out.read_bytes(), golden([2, 3, 4, 100, 101]))
                self.assertEqual((s["query"], s["operation"]), (0, 0))
                self.flow(srv, [1, 3, 100, 100])

    def test_actual_process_restart_preserves_log(self):
        with self.fixture() as (srv, c, root):
            out = next(root.rglob("out"))
            inode = out.stat().st_ino
            srv.pages = {0: [row(2)]}
            c.command("begin 0")
            self.assertEqual(c.command("fetch")["state"], 1)
            committed = c.command("append")["checkpoint"]
            self.assertEqual(out.read_bytes(), golden([2]))
            previous_pid = c.p.pid
            # Intentional owner shutdown, not a source of arbitrary reset skips.
            srv.stopped.set()
            c.close()
            self.assertTrue(srv.wss_closed.wait(5), "old socket handler closed")
            srv.stopped.clear()
            restarted = None
            try:
                restarted = Client(self, f"https://127.0.0.1:{srv.server_port}",
                                   self.certdir / "ca.pem", root, "channel")
                self.assertNotEqual(restarted.p.pid, previous_pid)
                self.assertEqual(out.stat().st_ino, inode)
                self.assertEqual(out.read_bytes(), golden([2]))
                self.assertEqual(restarted.command("start")["ready"], 3)
                self.assertEqual(len(srv.frames), 6)
                self.lock_held(root)
                srv.pages[committed] = [row(3)]
                restarted.command(f"begin {committed}")
                self.assertEqual(restarted.command("fetch")["ids"], [3])
                self.assertEqual(restarted.command("append")["checkpoint"], 3)
                self.assertEqual(out.read_bytes(), golden([2, 3]))
                self.assertEqual(out.stat().st_ino, inode)
            finally:
                srv.stopped.set()
                if restarted is not None:
                    restarted.close()

    def test_unknown_anchor_conflict_and_encryption(self):
        with self.fixture() as (srv, c, root):
            self.assertEqual(c.command("begin 0")["unknown"], 1)
            srv.send(dict(kind="message", message=row(100)))
            c.command("wait 1")
            s = c.command("drain")
            self.assertEqual((s["after"], s["ids"]), (0, [100]))
            srv.send(dict(kind="message", message=row(100)))
            c.command("wait 2")
            self.assertEqual(c.command("drain")["live"], 1)  # duplicates do not consume unique-live budget
            srv.send(dict(kind="message", message=row(100, "conflicting")))
            c.command("wait 3")
            s = c.command("drain")
            self.assertEqual((s["code"], s["after"], s["ids"]), (CONFLICT, 0, [100]))
            srv.send(dict(kind="message", message={**row(101), "kind": "e2e_future"}))
            s = c.command("wait 4")
            self.assertEqual((s["callback"], s["queued"], s["checkpoint"]), (UNSUPPORTED, 0, 0))
            self.assertEqual(list(root.rglob("out"))[0].read_bytes(), b"existing\n")
            self.flow(srv, [])
        with self.fixture() as (srv, c, root):
            srv.pages = {0: [row(1)]}
            c.command("begin 0")
            s = c.command("fetch")
            self.assertEqual((s["unknown"], s["state"], s["ids"]), (1, 1, [1]))
            c.command("append")
            self.assertEqual(list(root.rglob("out"))[0].read_bytes(), golden([1]))
            self.flow(srv, [0])

    def test_rest_conflict_and_encryption_fail_before_append(self):
        for mode, code in [("conflict", CONFLICT), ("encrypted", UNSUPPORTED)]:
            with self.subTest(mode=mode), self.fixture() as (srv, c, root):
                c.command("begin 0")
                srv.send(dict(kind="message", message=row(2)))
                c.command("wait 1")
                c.command("drain")
                bad = row(2, "conflict") if mode == "conflict" else {**row(2), "kind": "e2e_future"}
                srv.pages = {0: [row(1), bad]}
                s = c.command("fetch")
                self.assertEqual((s["code"], s["after"], s["ids"], s["checkpoint"]), (code, 0, [2], 0))
                self.assertEqual(list(root.rglob("out"))[0].read_bytes(), b"existing\n")
                self.flow(srv, [0])

    def test_callback_queue_dimensions(self):
        for limits, field in [("1 65536", "overflow_count"), ("8 1", "overflow_bytes")]:
            with self.subTest(limits=limits), self.fixture() as (srv, c, root):
                c.command("limits " + limits)
                srv.send(dict(kind="message", message=row(1)))
                srv.send(dict(kind="message", message=row(2)))
                s = c.command("wait 2")
                self.assertEqual(s[field], 1 if field == "overflow_count" else 2)
                self.assertEqual(s["overflow_bytes" if field == "overflow_count" else "overflow_count"], 0)
                self.assertEqual(s["queued"], 1 if field == "overflow_count" else 0)
                self.assertEqual(s["after"], 0)
                self.assertEqual(s["checkpoint"], 0)
                self.assertEqual(list(root.rglob("out"))[0].read_bytes(), b"existing\n")
                self.lock_held(root)


if __name__ == "__main__":
    unittest.main(verbosity=2)
