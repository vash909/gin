"""Independent native liveness acceptance; synthetic loopback TLS, not live Grappa.

The Python stdlib is development-only. The real C owner acquires curl globals,
uses the real adapters, and defers every action until its callback has returned.
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
import threading
import time
import unittest

import test_http
from ws_fixture_frames import server_frame, take_client_frame

ROOT = Path(__file__).resolve().parents[1]
TOKEN = "aa~"
ROOT_TOPIC = "grappa:user:Alice"
CONFIG = {"server": "grappa", "protocol_version": 7, "min_protocol_version": 1}
UPGRADE = "/socket/websocket?vsn=2.0.0&client_proto=4"


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address):
        super().__init__(address, Handler)
        self.cv = threading.Condition()
        self.records = []
        self.frames = []
        self.errors = []
        self.actions = queue.Queue()
        self.stopped = threading.Event()
        self.upgrades = 0
        self.mode = "ok"

    def record(self, collection, value):
        with self.cv:
            collection.append(value)
            self.cv.notify_all()

    def wait(self, predicate, timeout=5):
        with self.cv:
            return self.cv.wait_for(lambda: predicate() or self.errors, timeout) and not self.errors

    def handle_error(self, request, client_address):
        import traceback
        self.record(self.errors, traceback.format_exc())


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, format, *args):
        pass

    def do_GET(self):
        srv = self.server
        srv.record(srv.records, (self.path, dict(self.headers)))
        status = 200
        if self.path == "/api/config":
            body = dict(CONFIG)
            if srv.mode == "protocol":
                body["min_protocol_version"] = 9
        elif self.path == "/me":
            body = {"kind": "user", "id": "12345678-1234-1234-1234-123456789abc", "name": "Alice"}
            if srv.mode == "uuid":
                body["id"] = "22345678-1234-1234-1234-123456789abc"
            if srv.mode == "fresh401" and srv.upgrades:
                status, body = 401, {"error": "unauthorized"}
        else:
            assert self.path == UPGRADE, "no mutation or invented endpoint allowed"
            self.upgrade()
            return
        raw = json.dumps(body).encode()
        self.send_response(status)
        self.send_header("Content-Length", str(len(raw)))
        self.end_headers()
        self.wfile.write(raw)

    def send(self, frame):
        self.connection.sendall(server_frame(json.dumps(frame).encode()))

    def marker(self):
        self.send([None, None, ROOT_TOPIC, "event", {"kind": "fixture_barrier"}])

    def upgrade(self):
        srv = self.server
        with srv.cv:
            srv.upgrades += 1
            epoch = srv.upgrades
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.send_header("Sec-WebSocket-Protocol", "wrong" if srv.mode == "handshake" else "phoenix")
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.02)
        buf = bytearray()
        joins = 0
        held_join = None
        previous_heartbeat = None
        try:
            while not srv.stopped.is_set():
                try:
                    action = srv.actions.get_nowait()
                except queue.Empty:
                    action = None
                if action == "drop":
                    self.connection.shutdown(socket.SHUT_RDWR)
                    return
                if action == "release":
                    assert held_join is not None
                    self.send(held_join)
                    held_join = None
                elif action is not None:
                    self.send(action)
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
                if opcode in (8, 10):
                    if opcode == 8:
                        return
                    continue
                assert opcode == 1 and final, "client must send complete TEXT envelopes"
                f = json.loads(payload)
                assert isinstance(f, list) and len(f) == 5
                srv.record(srv.frames, (epoch, f))
                jr, ref, topic, event, body = f
                if event == "phx_join":
                    assert jr == ref and body == {}
                    joins += 1
                    response = {"protocol_version": 7} if joins == 1 else {}
                    reply = [jr, ref, topic, "phx_reply", {"status": "ok", "response": response}]
                    if srv.mode == "joining" and joins == 2:
                        held_join = reply
                    else:
                        self.send(reply)
                    if joins == 3:
                        self.marker()
                elif event == "heartbeat":
                    assert jr is None and topic == "phoenix" and body == {} and isinstance(ref, str)
                    reply = [None, ref, "phoenix", "phx_reply", {"status": "ok", "response": {}}]
                    mode = srv.mode
                    if mode == "stale" and previous_heartbeat is not None:
                        self.send(previous_heartbeat)
                    elif mode in ("wrong-ref", "wrong-join", "wrong-topic", "literal-null"):
                        wrong = list(reply)
                        slot, value = {"wrong-ref": (1, "foreign"), "wrong-join": (0, ref),
                                       "wrong-topic": (2, ROOT_TOPIC), "literal-null": (0, "null")}[mode]
                        wrong[slot] = value
                        self.send(wrong)
                    elif mode in ("lost", "noise"):
                        pass
                    elif mode in ("hb-error", "hb-unknown"):
                        reply[4]["status"] = "error" if mode == "hb-error" else "future"
                        self.send(reply)
                    elif mode == "fragment":
                        raw = json.dumps(reply).encode()
                        # Complete JSON prefix still awaits actual WS FIN.
                        self.connection.sendall(server_frame(raw, final=False) +
                                                server_frame(b"ping", opcode=9) +
                                                server_frame(b"", opcode=0))
                        self.marker()
                    else:
                        self.send(reply)
                        self.marker()
                    if mode == "noise":
                        for _ in range(8):
                            self.connection.sendall(server_frame(b"ping", opcode=9))
                            self.marker()
                    previous_heartbeat = reply
                elif event == "open_query_window":
                    assert topic == ROOT_TOPIC and jr is not None
                    assert body == {"network_id": 7, "target_nick": "Bob"}
                    if srv.mode == "query-accepted":
                        self.send([jr, ref, topic, "phx_reply", {"status": "ok", "response": {}}])
                    else:
                        assert srv.mode == "query-pending"
                    self.marker()
                else:
                    raise AssertionError("unexpected client operation: " + event)
        except (BrokenPipeError, ConnectionResetError, ssl.SSLEOFError):
            pass
        finally:
            self.close_connection = True


class Driver:
    def __init__(self, test, origin, ca, interval=150, timeout=500, base=200, cap=400, retries=2):
        self.test = test
        binary = os.environ.get("GIN_LIVENESS_DRIVER", str(ROOT / ".build/liveness_driver"))
        if "GIN_LIVENESS_PREFIX" in os.environ:
            interval = max(interval, 1000)
            timeout = max(timeout, 3000)
            base, cap = max(base, 1000), max(cap, 2000)
        command = shlex.split(os.environ.get("GIN_LIVENESS_PREFIX", "")) + [binary, origin, str(ca),
            str(interval), str(timeout), str(base), str(cap), str(retries)]
        self.process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                        stderr=subprocess.PIPE, text=True)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.process.stdout, selectors.EVENT_READ)
        try:
            self.read()
        except BaseException:
            self.process.kill()
            self.process.wait()
            for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
                stream.close()
            self.selector.close()
            raise

    def read(self):
        self.test.assertTrue(self.selector.select(35), "native owner response deadline")
        line = self.process.stdout.readline()
        if not line:
            self.test.fail("native owner exited: " + self.process.stderr.read())
        self.test.assertNotIn(TOKEN, line)
        return json.loads(line)

    def command(self, command):
        self.process.stdin.write(command + "\n")
        self.process.stdin.flush()
        return self.read()

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(35)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait()
            raise
        stderr = self.process.stderr.read()
        self.process.stdout.close()
        self.process.stderr.close()
        self.selector.close()
        self.test.assertEqual(self.process.returncode, 0, stderr)
        self.test.assertEqual(stderr, "")


class LivenessTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        test_http.HTTPTests.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        test_http.HTTPTests.tearDownClass.__func__(cls)

    tls = test_http.HTTPTests.tls

    @contextlib.contextmanager
    def fixture(self, mode="ok", cert="good", ca="ca.pem", **settings):
        srv = Server(("127.0.0.1", 0))
        srv.mode = mode
        srv.socket = self.tls(cert).wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": .02})
        thread.start()
        driver = None
        try:
            driver = Driver(self, f"https://127.0.0.1:{srv.server_port}", self.certdir / ca, **settings)
            yield srv, driver
        finally:
            try:
                if driver is not None:
                    driver.close()
            finally:
                srv.stopped.set()
                srv.shutdown()
                srv.server_close()
                thread.join(3)
                self.assertFalse(thread.is_alive())
            self.assertEqual(srv.errors, [])

    def ready(self, driver):
        self.assertEqual(driver.command("start")["code"], "ok")
        state = driver.command("until 3000 1")
        self.assertEqual(state["connection"], 3, state)
        self.assertEqual(state["events"], 1, state)
        return state

    def test_heartbeat_success(self):
        for mode in ("ok", "fragment"):
            with self.subTest(mode=mode), self.fixture(mode) as (srv, d):
                self.ready(d)
                state = d.command("until 10000 3")
                self.assertEqual(state["events"], 3, "heartbeat reply barriers must reach the native client")
                self.assertEqual(state["code"], "ok")
                self.assertEqual(state["state"], 1)  # ACTIVE
                heartbeats = [f for _, f in srv.frames if f[3] == "heartbeat"]
                self.assertEqual(len(heartbeats), 2)
                self.assertEqual([int(f[1]) for _, f in srv.frames], list(range(1, 6)))
                self.assertEqual([path for path, _ in srv.records], ["/api/config", "/me", UPGRADE])
                self.assertNotIn("Authorization", srv.records[0][1])
                self.assertEqual(srv.records[1][1]["Authorization"], "Bearer " + TOKEN)
                self.assertNotIn("Authorization", srv.records[2][1])
                self.assertEqual(srv.records[2][1]["Sec-WebSocket-Protocol"], "phoenix,base64url.bearer.phx.YWF+")

    def wait_due(self, srv, driver, expected_state=4):
        """Owner delay, not evidence of frame reception; assert every status."""
        paths = [p for p, _ in srv.records]
        end = time.monotonic() + 10
        while time.monotonic() < end:
            state = driver.command("status")
            self.assertEqual([p for p, _ in srv.records], paths, "status must never do HTTP")
            if state["wait"] == 0:
                self.assertEqual(state["state"], expected_state, state)
                return state
            self.assertIn(state["state"], (2, 3), state)
            threading.Event().wait(min(.02, state["wait"] / 1000))
        self.fail("bounded owner retry delay expired")

    def assert_no_retry(self, srv, d, step):
        before = d.command("status")
        paths = [p for p, _ in srv.records]
        after = d.command("retry")
        self.assertEqual(after["step"], step, after)
        for key in ("code", "connection", "state", "reason", "used", "epoch", "query", "operation", "query_ref"):
            self.assertEqual(after[key], before[key], key)
        self.assertEqual([p for p, _ in srv.records], paths)
        return after

    def test_lost_or_uncorrelated_reply_times_out_despite_noise(self):
        for mode in ("lost", "wrong-ref", "wrong-join", "literal-null", "wrong-topic", "stale", "noise"):
            with self.subTest(mode=mode), self.fixture(mode) as (srv, d):
                self.ready(d)
                state = d.command("drive 10000")
                self.assertEqual(state["code"], "timeout", state)
                self.assertEqual(state["connection"], 4)
                self.assertEqual(state["reason"], 3)  # HEARTBEAT_TIMEOUT
                self.assertTrue(any(f[3] == "heartbeat" for _, f in srv.frames))
                self.assertEqual(len(srv.records), 3, "service must never execute HTTP")
                if mode == "noise":
                    self.assertGreater(state["events"], 1, "noise must actually reach the client")
                if mode == "stale":
                    self.assertEqual(len([f for _, f in srv.frames if f[3] == "heartbeat"]), 2)

    def test_heartbeat_while_child_join_pending(self):
        with self.fixture("joining") as (srv, d):
            self.assertEqual(d.command("start")["code"], "ok")
            state = d.command("until 10000 1")
            self.assertEqual(state["events"], 1, "heartbeat ACK barrier must reach client while JOINING")
            self.assertEqual(state["connection"], 2)
            self.assertEqual([f[3] for _, f in srv.frames], ["phx_join", "phx_join", "heartbeat"])
            srv.actions.put("release")
            state = d.command("until 10000 2")
            self.assertEqual(state["connection"], 3)
            self.assertEqual(state["code"], "ok")

    def test_matching_heartbeat_error_is_terminal(self):
        for mode in ("hb-error", "hb-unknown"):
            with self.subTest(mode=mode), self.fixture(mode) as (srv, d):
                self.ready(d)
                state = d.command("drive 10000")
                self.assertEqual(state["connection"], 4)
                self.assertEqual(state["state"], 5)  # TERMINAL
                self.assertEqual(state["reason"], 10)  # PROTOCOL
                self.assert_no_retry(srv, d, 4)

    def severance(self, topic=ROOT_TOPIC, generation=None, code="rate_limit_flood"):
        return [generation, None, topic, "event", {"kind": "web_session_severed", "code": code}]

    def test_actual_root_severance_stops_ready_and_joining(self):
        for mode in ("ok", "joining"):
            with self.subTest(mode=mode), self.fixture(mode) as (srv, d):
                if mode == "ok":
                    initial = self.ready(d)
                else:
                    d.command("start")
                    initial = d.command("until 10000 1")
                    self.assertEqual(initial["connection"], 2)
                srv.actions.put(self.severance())
                state = d.command("drive 10000")
                self.assertEqual(state["code"], "session-expired", state)
                self.assertEqual(state["state"], 5)
                self.assertEqual(state["reason"], 6)
                self.assertEqual(state["events"], initial["events"], "severance must not escape as app data")
                self.assert_no_retry(srv, d, 4)
                self.assertNotEqual(d.command("start")["step"], 0)
                self.assertEqual(len(srv.records), 3)

    def test_foreign_and_stale_root_cannot_impersonate_severance(self):
        with self.fixture(interval=10000) as (srv, d):
            self.ready(d)
            srv.actions.put(self.severance(topic="grappa:user:Mallory"))
            srv.actions.put(self.severance(generation="stale-root"))
            srv.actions.put([None, None, ROOT_TOPIC, "event", {"kind": "fixture_barrier"}])
            state = d.command("until 3000 2")
            self.assertEqual(state["events"], 2, "post-decoy barrier proves client consumed preceding frames")
            self.assertEqual(state["connection"], 3)
            srv.actions.put(self.severance())
            state = d.command("drive 3000")
            self.assertEqual(state["reason"], 6)
            self.assertEqual(state["events"], 2)

    def drop(self, srv, d):
        srv.actions.put("drop")
        state = d.command("drive 5000")
        self.assertEqual(state["connection"], 4, state)
        self.assertIn(state["reason"], (1, 2))  # actual I/O/peer close, not auth evidence
        return state

    def test_finite_owner_retries_fresh_http_root_first_epochs_refs(self):
        with self.fixture(interval=500) as (srv, d):
            state = self.ready(d)
            for used in range(3):
                self.assertEqual(state["epoch"], used + 1)
                if used:
                    old_root = srv.frames[0][1][0]
                    srv.actions.put(self.severance(generation=old_root))
                    srv.actions.put([None, None, ROOT_TOPIC, "event", {"kind": "fixture_barrier"}])
                    target = state["events"] + 1
                    state = d.command(f"until 10000 {target}")
                    self.assertEqual(state["events"], target, "old socket root generation must be ignored")
                    self.assertEqual(state["connection"], 3)
                state = d.command(f"until 10000 {state['events'] + 1}")
                self.assertEqual(state["connection"], 3)
                self.assertEqual(state["used"], used, "READY plus successful heartbeat must not reset lifetime budget")
                failed = self.drop(srv, d)
                self.assertEqual(failed["used"], used)
                if used == 2:
                    self.assertEqual(failed["state"], 6)
                    self.assert_no_retry(srv, d, 3)
                    break
                self.assertGreater(failed["wait"], 0)
                self.assert_no_retry(srv, d, 1)  # NOT_DUE; unchanged result/epoch/budget
                self.assertEqual(d.command("start")["step"], 1)  # INVALID, cannot bypass retry gate
                due = self.wait_due(srv, d)
                self.assertEqual(due["used"], used)
                paths = [p for p, _ in srv.records]
                serviced = d.command("service 0")
                self.assertEqual(serviced["epoch"], due["epoch"])
                self.assertEqual(serviced["used"], used)
                self.assertEqual([p for p, _ in srv.records], paths, "even due service must never retry/HTTP")
                retry = d.command("retry")
                self.assertEqual(retry["step"], 0)  # ATTEMPTED != READY
                self.assertEqual(retry["used"], used + 1)
                state = d.command(f"until 10000 {state['events'] + 1}")
                self.assertEqual(state["connection"], 3)
            self.assertEqual([p for p, _ in srv.records], ["/api/config", "/me", UPGRADE] * 3)
            frames = [f for _, f in srv.frames]
            self.assertEqual([int(f[1]) for f in frames], list(range(1, 13)))
            self.assertEqual([f[2] for f in frames], [ROOT_TOPIC, ROOT_TOPIC + "/network:Net",
                ROOT_TOPIC + "/network:Net/channel:#room", "phoenix"] * 3)
            self.assertEqual([epoch for epoch, _ in srv.frames], [1] * 4 + [2] * 4 + [3] * 4)

    def test_fresh_me401_after_drop_is_terminal(self):
        with self.fixture("fresh401", interval=10000) as (srv, d):
            self.ready(d)
            self.drop(srv, d)
            self.wait_due(srv, d)
            state = d.command("retry")
            self.assertEqual(state["step"], 0)
            self.assertEqual(state["code"], "unauthorized")
            self.assertEqual(state["state"], 5)
            self.assertEqual(state["reason"], 8)  # AUTH, actual completed /me401
            self.assert_no_retry(srv, d, 4)
            self.assertEqual([p for p, _ in srv.records], ["/api/config", "/me", UPGRADE, "/api/config", "/me"])
            self.assertEqual(srv.upgrades, 1)

    def test_security_tls_and_protocol_failure_are_terminal(self):
        cases = [("uuid", "good", "ca.pem", ["/api/config", "/me"]),
                 ("protocol", "good", "ca.pem", ["/api/config"]),
                 ("handshake", "good", "ca.pem", ["/api/config", "/me", UPGRADE]),
                 ("ok", "wrong", "ca.pem", []), ("ok", "good", "bad.pem", [])]
        for mode, cert, ca, paths in cases:
            with self.subTest(mode=mode, cert=cert, ca=ca), self.fixture(mode, cert, ca) as (srv, d):
                state = d.command("start")
                self.assertEqual(state["connection"], 4)
                self.assertEqual(state["state"], 5)
                self.assert_no_retry(srv, d, 4)
                self.assertEqual([p for p, _ in srv.records], paths)
                self.assertEqual(srv.frames, [])

    def test_zero_retries_and_permanent_explicit_close(self):
        with self.fixture(interval=10000, retries=0) as (srv, d):
            self.ready(d)
            self.drop(srv, d)
            self.assert_no_retry(srv, d, 3)
        with self.fixture(interval=10000) as (srv, d):
            self.ready(d)
            state = d.command("close")
            self.assertEqual(state["state"], 7)  # CANCELLED
            self.assert_no_retry(srv, d, 5)
            self.assertEqual(d.command("start")["step"], 1)
            self.assertEqual(len(srv.records), 3)

    def test_query_receipt_heartbeat_independence_and_no_replay(self):
        for mode, expected in (("query-accepted", 3), ("query-pending", 1)):
            with self.subTest(mode=mode), self.fixture(mode, interval=500) as (srv, d):
                self.ready(d)
                self.assertEqual(d.command("query 20000")["step"], 0)
                # Server marker follows actual full query reception (and ACK if any).
                state = d.command("until 10000 2")
                self.assertEqual(state["query"], expected)
                receipt = {k: state[k] for k in ("operation", "query_epoch", "query_ref", "query_join")}
                self.assertEqual(receipt["operation"], 42)
                state = d.command("until 10000 4")
                self.assertEqual(state["events"], 4, "heartbeats continue with query pending or receipt retained")
                self.assertEqual(state["query"], expected)
                failed = self.drop(srv, d)
                self.assertEqual(failed["state"], 3)  # RECEIPT_BLOCKED
                self.assertEqual(failed["query"], 3 if expected == 3 else 5)  # ACCEPTED/UNCERTAIN
                for key, value in receipt.items():
                    self.assertEqual(failed[key], value)
                self.wait_due(srv, d, expected_state=3)
                self.assert_no_retry(srv, d, 2)
                self.assertEqual(d.command("take")["step"], 1)
                self.assertEqual(d.command("take")["step"], 0)
                state = d.command("retry")
                self.assertEqual(state["step"], 0)
                state = d.command("until 10000 5")
                self.assertEqual(state["connection"], 3)
                self.assertEqual(state["query"], 0)
                self.assertEqual(len([f for _, f in srv.frames if f[3] == "open_query_window"]), 1)
                self.assertEqual([p for p, _ in srv.records], ["/api/config", "/me", UPGRADE] * 2)


if __name__ == "__main__":
    unittest.main(verbosity=2)
