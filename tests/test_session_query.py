import base64
import contextlib
import errno
import fcntl
import hashlib
import json
import os
import select
import signal
import subprocess
import sys
import threading
import time
import unittest
from pathlib import Path
from typing import Any, cast
from collections.abc import Iterator

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tests"))
import test_session as startup
import test_session_outbound as outbound
import test_session_controls as controls
from ws_fixture_frames import server_frame, take_client_frame

TEXT = outbound.TEXT
PREFIX = outbound.PREFIX
BEFORE = outbound.BEFORE
AFTER = outbound.AFTER

def budget(native: int) -> int:
    return 90 if startup.PREFIX else native

class QueryServer(controls.ControlServer):
    mode: str
    queries: list[list[Any]]
    query_seen: threading.Event
    release_query: threading.Event

class QueryHandler(controls.ControlHandler):
    def upgrade(self) -> None:
        srv = cast(QueryServer, self.server)
        assert self.headers.get("Authorization") is None
        assert self.headers.get("Sec-WebSocket-Protocol") == "phoenix,base64url.bearer.phx.YWF+"
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101)
        for name, value in [("Upgrade", "websocket"), ("Connection", "Upgrade"), ("Sec-WebSocket-Accept", accept), ("Sec-WebSocket-Protocol", "phoenix")]:
            self.send_header(name, value)
        self.end_headers()
        self.wfile.flush()
        self.connection.settimeout(.2)
        buffered = bytearray()
        def receive() -> Any:
            deadline = time.monotonic() + budget(8)
            while time.monotonic() < deadline and not srv.stopped.is_set():
                item = take_client_frame(buffered)
                if item is not None:
                    return item
                try:
                    data = self.connection.recv(16384)
                except TimeoutError:
                    continue
                if data is None or len(data) == 0:
                    return None
                buffered.extend(data)
            return None
        try:
            topic_root = ""
            topic_last = ""
            for index in range(3):
                item = receive()
                assert item is not None, "closed before required subscription"
                opcode, final, raw = item
                assert opcode == 1 and final
                jr, ref, topic, event, payload = json.loads(raw)
                assert event == "phx_join" and jr == ref and payload == {}
                srv.joins.append(topic)
                if index == 0:
                    topic_root = topic
                topic_last = topic
                response: dict[str, Any] = {"protocol_version": 36} if index == 0 else {}
                reply = [jr, ref, topic, "phx_reply", {"status": "ok", "response": response}]
                self.connection.sendall(server_frame(json.dumps(reply).encode()))
            assert topic_root == "grappa:user:Alice"
            assert topic_last == "grappa:user:Alice/network:NetA/channel:#room"
            # BEFORE incoming only after joins, receiver barrier not server send only
            frame_before = [None, None, topic_last, "event", {"kind": "message", "message": {"network": "NetA", "channel": "#room", "kind": "action", "sender": "Nick", "server_time": 0, "body": "\x01ACTION before-fifo"}}]
            self.connection.sendall(server_frame(json.dumps(frame_before).encode()))
            # now expect exactly one open_query_window on root
            item = receive()
            if item is None and srv.stopped.is_set():
                return
            assert item is not None, "no query frame"
            opcode, final, raw = item
            assert opcode == 1 and final, "unexpected query opcode"
            decoded = json.loads(raw)
            # capture independently
            srv.queries.append(decoded)
            assert decoded == ["1", "4", "grappa:user:Alice", "open_query_window", {"network_id": 3000000001, "target_nick": "Bob"}], f"wrong query frame {decoded}"
            srv.query_seen.set()
            assert srv.release_query.wait(budget(8)), "release_query barrier expired"
            if srv.stopped.is_set():
                return
            if srv.mode == "close":
                self.connection.sendall(server_frame(b"", opcode=8))
                while not srv.stopped.is_set():
                    item = receive()
                    if item is None:
                        return
                    assert item[0] in (8, 10), "unexpected operation after CLOSE"
                return
            if srv.mode == "error":
                ack = ["1", "4", "grappa:user:Alice", "phx_reply", {"status": "error", "response": {}}]
                self.connection.sendall(server_frame(json.dumps(ack).encode()))
                while not srv.stopped.is_set():
                    item = receive()
                    if item is None:
                        if srv.stopped.is_set():
                            return
                        return
                    assert item[0] in (8, 10), "unexpected application operation after query"
                return
            ack = ["1", "4", "grappa:user:Alice", "phx_reply", {"status": "ok", "response": {}}]
            self.connection.sendall(server_frame(json.dumps(ack).encode()))
            if srv.mode in ("ok", "queued"):
                assert srv.posted.wait(budget(8)), "message POST not observed before AFTER"
                if srv.stopped.is_set():
                    return
                assert srv.release_after.wait(budget(8)), "after-message barrier expired"
                if srv.stopped.is_set():
                    return
                frame_after = [None, None, topic_last, "event", {"kind": "message", "message": {"network": "NetA", "channel": "#room", "kind": "action", "sender": "Nick", "server_time": 0, "body": "\x01ACTION after-fifo"}}]
                self.connection.sendall(server_frame(json.dumps(frame_after).encode()))
            while not srv.stopped.is_set():
                item = receive()
                if item is None:
                    if srv.stopped.is_set():
                        return
                    # ordinary cleanup poll returned None -> normal return without masking
                    return
                assert item[0] in (8, 10), "unexpected application operation after query"
        finally:
            self.close_connection = True

class SessionQueryTracer(unittest.TestCase):
    certdir: Path
    operation_buffer: bytearray
    tls = startup.StartupTracer.tls

    @classmethod
    def setUpClass(cls) -> None:
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls) -> None:
        startup.StartupTracer.tearDownClass.__func__(cls)

    def command(self, *args: Any) -> list[str]:
        binary = os.environ.get("GIN_SESSION_BINARY", str(REPO / ".build/gin"))
        return [*startup.PREFIX, binary, *map(str, args)]

    @contextlib.contextmanager
    def fixture(self, mode: str = "ok") -> Iterator[tuple[QueryServer, str]]:
        srv = QueryServer(("127.0.0.1", 0), QueryHandler)
        srv.mode = mode
        srv.paths, srv.joins, srv.errors, srv.posts = [], [], [], []
        srv.methods, srv.raw_bodies = [], []
        srv.queries = []
        for name in ["stopped", "posted", "release_after", "query_seen", "release_query"]:
            setattr(srv, name, threading.Event())
        srv.socket = self.tls().wrap_socket(srv.socket, server_side=True)
        thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": .02})
        thread.start()
        try:
            yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally:
            srv.stopped.set()
            srv.posted.set()
            srv.release_after.set()
            srv.query_seen.set()
            srv.release_query.set()
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), "fixture listener leaked")
            self.assertFalse(srv.errors, "positive fixture handler failed")

    def wait_output(self, proc: subprocess.Popen[bytes], path: Path, marker: bytes) -> None:
        deadline = time.monotonic() + budget(8)
        while marker not in path.read_bytes():
            self.assertIsNone(proc.poll(), "connected process exited before incoming projection")
            self.assertLess(time.monotonic(), deadline, "incoming projection deadline")
            time.sleep(.02)

    def read_op_lines(self, fd: int, need: int, timeout: float) -> tuple[list[bytes], bytes]:
        buf = self.operation_buffer
        lines: list[bytes] = []
        deadline = time.monotonic() + timeout
        while len(lines) < need:
            if b"\n" in buf:
                index = buf.index(b"\n")
                line = bytes(buf[: index + 1])
                del buf[: index + 1]
                self.assertLessEqual(len(line), 192, "operation line exceeds 192")
                lines.append(line)
                continue
            self.assertLessEqual(len(buf), 192, "unterminated operation exceeds 192")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
                break
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            buf.extend(chunk)
        return lines, bytes(buf)

    def run_case(self, mode: str = "ok") -> None:
        with self.fixture(mode) as (srv, origin):
            root = self.certdir / f"query-root-{mode}"
            initialized = subprocess.run(self.command("init", "-d", root, "-n", "NetA", "-c", "#Room"), capture_output=True, timeout=budget(10))
            self.assertEqual(initialized.returncode, 0)
            outputs = list(root.rglob("out"))
            self.assertEqual(len(outputs), 1)
            output = outputs[0]
            output.write_bytes(PREFIX)
            inode = output.stat().st_ino
            credential = root / ".gin.credentials"
            credential.write_text(json.dumps({"version": 1, "origin": origin, "token": startup.TOKEN, "subject_id": startup.UUID, "subject_name": "Alice"}))
            credential.chmod(0o600)
            original_credential = credential.read_bytes()
            tree_before = {str(p.relative_to(root)) for p in root.rglob("*")}
            ready_read, ready_write = os.pipe()
            op_read, op_write = os.pipe()
            for fd in [ready_read, ready_write, op_read, op_write]:
                os.set_blocking(fd, False)
            flags = {fd: (fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)) for fd in [ready_write, op_write]}
            self.assertGreaterEqual(ready_write, 3)
            self.assertGreaterEqual(op_write, 3)
            settings = self.certdir / f"query-settings-{mode}"
            settings.write_text("connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n" if startup.PREFIX else "")
            settings.chmod(0o600)
            proc = subprocess.Popen(self.command("connect", "-f", settings, "-d", root, "-n", "NetA", "-c", "#Room", "-e", origin, "-m", "plain", "-a", self.certdir / "ca.pem", "-R", ready_write, "-O", op_write), pass_fds=(ready_write, op_write), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.operation_buffer = bytearray()
            writer = -1
            try:
                self.assertTrue(select.select([ready_read], [], [], budget(8))[0], "no correlated READY")
                self.assertEqual(os.read(ready_read, 128), b"READY\n", "invalid readiness record")
                self.assertFalse(select.select([ready_read], [], [], 0)[0], "extra READY data")
                self.wait_output(proc, output, BEFORE)
                try:
                    writer = os.open(output.with_name("in"), os.O_WRONLY | os.O_NONBLOCK)
                except OSError as error:
                    if error.errno == errno.ENXIO:
                        self.fail("SEMANTIC_RED: connected CLI receives but has no selected-FIFO reader")
                    raise
                qline = b"/query Bob\n"
                self.assertLessEqual(len(qline), os.fpathconf(writer, "PC_PIPE_BUF"))
                self.assertEqual(os.write(writer, qline), len(qline))
                lines, _ = self.read_op_lines(op_read, 1, budget(8))
                self.assertEqual(lines, [b"OP1 1 QUERY PENDING 0\n"], "FIRST receipt must be PENDING immediately")
                self.assertEqual(self.operation_buffer, bytearray(), "extra buffered operation after PENDING")
                self.assertFalse(select.select([op_read], [], [], 0.2)[0], "unexpected extra operation data after PENDING")
                self.assertTrue(cast(QueryServer, srv).query_seen.wait(budget(8)), "server did not see query")
                self.assertEqual(len(cast(QueryServer, srv).queries), 1)
                self.assertEqual(cast(QueryServer, srv).queries[0], ["1", "4", "grappa:user:Alice", "open_query_window", {"network_id": 3000000001, "target_nick": "Bob"}])
                self.assertEqual(srv.paths, ["/api/config", "/me", "/boot", "/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=36"])
                self.assertEqual(srv.methods, [])
                if mode != "ok":
                    tline = (TEXT + "\n").encode()
                    self.assertLessEqual(len(tline), os.fpathconf(writer, "PC_PIPE_BUF"))
                    self.assertEqual(os.write(writer, tline), len(tline))
                    quiet = 0.4 if not startup.PREFIX else 2.0
                    # STRICT LESS than server release_query budget 8/90
                    self.assertLess(quiet, budget(8))
                    self.assertEqual(self.operation_buffer, bytearray(), "queued literal leaked to OP before ACK")
                    self.assertFalse(select.select([op_read], [], [], quiet)[0], "OP data during quiet window: FIFO admitted second operation before ACK")
                    self.assertEqual(srv.methods, [], "HTTP mutation during quiet window")
                    self.assertEqual(cast(Any, srv).posts, [])
                    self.assertEqual(len(cast(QueryServer, srv).queries), 1, "extra query during quiet window")
                    self.assertEqual(srv.joins, ["grappa:user:Alice", "grappa:user:Alice/network:NetA", "grappa:user:Alice/network:NetA/channel:#room"])
                if mode in ("ok", "queued"):
                    cast(QueryServer, srv).release_query.set()
                    terminal, _ = self.read_op_lines(op_read, 1, budget(8))
                    self.assertEqual(terminal, [b"OP1 1 QUERY ACCEPTED_ACK 0\n"])
                    if mode == "ok":
                        self.assertEqual(self.operation_buffer, bytearray())
                        self.assertFalse(select.select([op_read], [], [], 0.2)[0], "unexpected extra after ACCEPTED_ACK")
                        # ACK does not create inventory/window/list subscription
                        self.assertEqual({str(p.relative_to(root)) for p in root.rglob("*")}, tree_before, "query created local window")
                        self.assertEqual(output.read_bytes(), PREFIX + BEFORE, "query mutated output")
                        self.assertEqual(output.stat().st_ino, inode)
                        # literal send after query terminal
                        tline = (TEXT + "\n").encode()
                        self.assertLessEqual(len(tline), os.fpathconf(writer, "PC_PIPE_BUF"))
                        self.assertEqual(os.write(writer, tline), len(tline))
                    send_lines, _ = self.read_op_lines(op_read, 2, budget(8))
                    self.assertEqual(send_lines, [b"OP1 2 SEND PENDING 0\n", b"OP1 2 SEND ACCEPTED_NO_PERSIST 0\n"])
                    self.assertEqual(self.operation_buffer, bytearray())
                    self.assertFalse(select.select([op_read], [], [], 0.2)[0], "unexpected extra operation after SEND")
                    self.assertTrue(cast(QueryServer, srv).posted.wait(budget(8)), "SEND POST not observed")
                    cast(QueryServer, srv).release_after.set()
                    self.wait_output(proc, output, AFTER)
                    data = output.read_bytes()
                    self.assertEqual(data, PREFIX + BEFORE + AFTER)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertNotIn(TEXT.encode(), data.split(BEFORE)[0] if BEFORE in data else data, "echo")
                    self.assertEqual(list(root.rglob("out")), [output], "nonselected output fan-out")
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob("*")}, tree_before, "Bob local node/tree changes")
                    self.assertNotIn(b"Bob", data, "Bob local node leak")
                    # HTTP/send assertions: one POST, fresh config/me before it, 202 ok
                    self.assertEqual(srv.methods, ["POST"])
                    self.assertEqual(len(cast(Any, srv).posts), 1)
                    self.assertEqual(json.loads(cast(Any, srv).posts[0]), {"body": TEXT})
                    self.assertEqual(srv.paths.count("/networks/NetA/channels/%23Room/messages"), 1)
                    idx = srv.paths.index("/networks/NetA/channels/%23Room/messages")
                    self.assertGreaterEqual(idx, 2)
                    self.assertEqual(srv.paths[idx - 2], "/api/config")
                    self.assertEqual(srv.paths[idx - 1], "/me")
                    self.assertEqual(srv.paths, ["/api/config", "/me", "/boot", "/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=36", "/api/config", "/me", "/networks/NetA/channels/%23Room/messages"])
                    for fd, orig in flags.items():
                        self.assertEqual((fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)), orig)
                    proc.send_signal(signal.SIGTERM)
                    stdout, stderr = proc.communicate(timeout=budget(12))
                    self.assertEqual(proc.returncode, 0)
                    self.assertEqual(stdout + stderr, b"")
                    self.assertEqual(credential.read_bytes(), original_credential)
                    self.assertFalse(cast(Any, srv).errors)
                else:
                    # STOP or ERROR: unified abort path after action
                    if mode == "stop":
                        proc.send_signal(signal.SIGTERM)
                    elif mode != "timeout":
                        cast(QueryServer, srv).release_query.set()
                    expected = {
                        "stop": b"OP1 1 QUERY UNCERTAIN 11\n",
                        "close": b"OP1 1 QUERY UNCERTAIN 11\n",
                        "timeout": b"OP1 1 QUERY UNCERTAIN 9\n",
                        "error": b"OP1 1 QUERY REMOTE_FAILURE 7\n",
                    }[mode]
                    terminal, _ = self.read_op_lines(op_read, 1, budget(8))
                    self.assertEqual(terminal, [expected])
                    stdout, stderr = proc.communicate(timeout=budget(12))
                    if mode == "stop":
                        self.assertEqual(proc.returncode, 0)
                        self.assertEqual(stdout + stderr, b"")
                    else:
                        self.assertEqual(proc.returncode, 1)
                        self.assertEqual(stdout, b"")
                        self.assertEqual(stderr, {
                            "error": b"gin: remote operation failure\n",
                            "close": b"gin: connection failure\n",
                            # Native query expiry fails the connection; the OP
                            # receipt still preserves TIMEOUT/UNCERTAIN.
                            "timeout": b"gin: connection failure\n",
                        }[mode])
                    self.assertEqual(self.operation_buffer, bytearray())
                    self.assertFalse(select.select([op_read], [], [], 0.2)[0], "unexpected extra OP after abort")
                    self.assertEqual(srv.methods, [])
                    self.assertEqual(cast(Any, srv).posts, [])
                    self.assertEqual(srv.paths, ["/api/config", "/me", "/boot", "/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=36"])
                    self.assertEqual(output.read_bytes(), PREFIX + BEFORE)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob("*")}, tree_before)
                    self.assertEqual(credential.read_bytes(), original_credential)
                    for fd, orig in flags.items():
                        self.assertEqual((fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)), orig)
                    # invariants before close already checked; return inside finally will handle barriers
                    return
            finally:
                cast(QueryServer, srv).stopped.set()
                cast(QueryServer, srv).posted.set()
                cast(QueryServer, srv).release_after.set()
                cast(QueryServer, srv).query_seen.set()
                cast(QueryServer, srv).release_query.set()
                if proc.poll() is None:
                    proc.kill()
                try:
                    proc.communicate(timeout=budget(10))
                finally:
                    if writer >= 0:
                        os.close(writer)
                    for fd in (ready_read, ready_write, op_read, op_write):
                        os.close(fd)

    def test_query_then_literal_send_same_session(self) -> None:
        self.run_case(mode="ok")

    def test_query_queued_literal_before_ack(self) -> None:
        self.run_case(mode="queued")

    def test_query_sigterm_before_ack_with_queued_literal(self) -> None:
        self.run_case(mode="stop")

    def test_query_remote_error_before_ack_with_queued_literal(self) -> None:
        self.run_case(mode="error")

    def test_query_close_before_ack_with_queued_literal(self) -> None:
        self.run_case(mode="close")

    def test_query_timeout_before_ack_with_queued_literal(self) -> None:
        self.run_case(mode="timeout")

if __name__ == "__main__":
    unittest.main()
