"""Tracing JOIN #Other then literal SEND in same connected process."""
import contextlib
import errno
import fcntl
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
from typing import cast

REPO = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO / "tests"))
import test_session as startup
import test_session_outbound as outbound

TEXT = outbound.TEXT
PREFIX = outbound.PREFIX
BEFORE = outbound.BEFORE
AFTER = outbound.AFTER

def budget(native):
    return 90 if startup.PREFIX else native

class ControlServer(outbound.OutboundServer):
    methods: list[str]
    raw_bodies: list[bytes]

class ControlHandler(outbound.Handler):
    def do_POST(self):
        srv = cast(ControlServer, self.server)
        assert self.headers.get("Authorization") == "Bearer " + startup.TOKEN
        length = int(self.headers["Content-Length"])
        assert 0 < length <= 65536
        raw = self.rfile.read(length)
        srv.methods.append(self.command)
        srv.paths.append(self.path)
        srv.posts.append(raw)
        srv.raw_bodies.append(raw)
        if self.path == "/networks/NetA/channels":
            assert json.loads(raw) == {"name": "#Other"}, "wrong JOIN body"
            body = json.dumps({"ok": True}).encode()
            self.send_response(202)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()
            return
        if self.path == "/networks/NetA/channels/%23Room/messages":
            assert json.loads(raw) == {"body": TEXT}, "wrong SEND body"
            body = json.dumps({"ok": True}).encode()
            self.send_response(202)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            self.wfile.flush()
            srv.posted.set()
            return
        raise AssertionError(f"unexpected POST {self.path}")

    def do_DELETE(self):
        srv = cast(ControlServer, self.server)
        assert self.path in ("/networks/NetA/channels/%23rOOM", "/networks/NetA/channels/%23Other")
        assert self.headers.get("Authorization") == "Bearer " + startup.TOKEN
        assert int(self.headers.get("Content-Length", "0")) == 0
        assert self.headers.get("Content-Type") is None
        srv.methods.append(self.command)
        srv.paths.append(self.path)
        srv.posts.append(b"")
        srv.raw_bodies.append(b"")
        body = b'{"ok":true}'
        self.send_response(202)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        self.wfile.flush()

class SessionControlsTracer(unittest.TestCase):
    certdir: Path
    operation_buffer: bytearray
    tls = startup.StartupTracer.tls

    @classmethod
    def setUpClass(cls):
        startup.StartupTracer.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls):
        startup.StartupTracer.tearDownClass.__func__(cls)

    def command(self, *args):
        binary = os.environ.get("GIN_SESSION_BINARY", str(REPO / ".build/gin"))
        return [*startup.PREFIX, binary, *map(str, args)]

    @contextlib.contextmanager
    def fixture(self):
        srv = ControlServer(("127.0.0.1", 0), ControlHandler)
        srv.paths, srv.joins, srv.errors, srv.posts = [], [], [], []
        srv.methods, srv.raw_bodies = [], []
        for name in ["stopped", "posted", "release_after"]:
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
            srv.shutdown()
            srv.server_close()
            thread.join(3)
            self.assertFalse(thread.is_alive(), "fixture listener leaked")
            self.assertFalse(srv.errors, "positive fixture handler failed")

    def wait_output(self, proc, path, marker):
        deadline = time.monotonic() + budget(8)
        while marker not in path.read_bytes():
            self.assertIsNone(proc.poll(), "connected process exited before incoming projection")
            self.assertLess(time.monotonic(), deadline, "incoming projection deadline")
            time.sleep(.02)

    def read_op_lines(self, fd, need, timeout):
        buf = self.operation_buffer
        lines: list[bytes] = []
        deadline = time.monotonic() + timeout
        while len(lines) < need:
            if b"\n" in buf:
                index = buf.index(b"\n")
                line = bytes(buf[:index + 1])
                del buf[:index + 1]
                self.assertLessEqual(len(line), 192, "operation line exceeds192")
                lines.append(line)
                continue
            self.assertLessEqual(len(buf), 192, "unterminated operation exceeds192")
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([fd], [], [], remaining)[0]:
                break
            chunk = os.read(fd, 4096)
            if not chunk:
                break
            buf.extend(chunk)
        return lines, bytes(buf)

    def run_control(self, operation="join"):
        self.assertIn(operation, ("join", "part-selected", "part-other"))
        selected_part = operation == "part-selected"
        other_part = operation == "part-other"
        with self.fixture() as (srv, origin):
            root = self.certdir / "controls-root"
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
            ready_read, ready_write = os.pipe()
            op_read, op_write = os.pipe()
            for fd in [ready_read, ready_write, op_read, op_write]:
                os.set_blocking(fd, False)
            flags = {fd: (fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)) for fd in [ready_write, op_write]}
            self.assertGreaterEqual(ready_write, 3)
            self.assertGreaterEqual(op_write, 3)
            settings = self.certdir / "controls-settings"
            settings.write_text("connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n" if startup.PREFIX else "")
            settings.chmod(0o600)
            proc = subprocess.Popen(self.command("connect", "-f", settings, "-d", root, "-n", "NetA", "-c", "#Room", "-e", origin, "-m", "plain", "-a", self.certdir / "ca.pem", "-R", ready_write, "-O", op_write), pass_fds=(ready_write, op_write), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.operation_buffer = bytearray()
            tree_before = {str(p.relative_to(root)) for p in root.rglob("*")}
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
                join_line = (b"/part #rOOM\n" if selected_part else
                    b"/part #Other\n" if other_part else b"/join #Other\n")
                if selected_part:
                    # Queue a complete following record atomically; it must not execute.
                    join_line += (TEXT + "\n").encode()
                self.assertLessEqual(len(join_line), os.fpathconf(writer, "PC_PIPE_BUF"))
                self.assertEqual(os.write(writer, join_line), len(join_line))
                lines, _ = self.read_op_lines(op_read, 1, budget(8))
                self.assertGreaterEqual(len(lines), 1, "no operation line after JOIN")
                kind = b"PART" if selected_part or other_part else b"JOIN"
                self.assertEqual(lines[0], b"OP1 1 " + kind + b" PENDING 0\n", "FIRSTLINE must match admitted control")
                terminal, _ = self.read_op_lines(op_read, 1, budget(8))
                self.assertEqual(terminal, [b"OP1 1 " + kind + b" ACCEPTED_PENDING 0\n"])
                if selected_part:
                    stdout, stderr = proc.communicate(timeout=budget(12))
                    self.assertEqual((proc.returncode, stdout, stderr), (0, b"", b""))
                    self.assertEqual(self.operation_buffer, bytearray(), "operation admitted after selected PART")
                    self.assertFalse(select.select([op_read], [], [], 0)[0], "extra operation after PART")
                    self.assertEqual(output.read_bytes(), PREFIX + BEFORE)
                    self.assertEqual(output.stat().st_ino, inode)
                    self.assertEqual({str(p.relative_to(root)) for p in root.rglob("*")}, tree_before)
                    self.assertEqual(srv.methods, ["DELETE"])
                    self.assertEqual(srv.posts, [b""])
                    self.assertEqual(srv.paths[-3:], ["/api/config", "/me", "/networks/NetA/channels/%23rOOM"])
                    self.assertEqual(credential.read_bytes(), original_credential)
                    for fd, original_flags in flags.items():
                        self.assertEqual((fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)), original_flags)
                    return
                text_line = (TEXT + "\n").encode()
                self.assertLessEqual(len(text_line), os.fpathconf(writer, "PC_PIPE_BUF"))
                self.assertEqual(os.write(writer, text_line), len(text_line))
                send_lines, _ = self.read_op_lines(op_read, 2, budget(8))
                self.assertEqual(len(send_lines), 2, "SEND did not produce 2 operation lines")
                self.assertEqual(send_lines[0], b"OP1 2 SEND PENDING 0\n")
                self.assertEqual(send_lines[1], b"OP1 2 SEND ACCEPTED_NO_PERSIST 0\n")
                self.assertEqual(self.operation_buffer, bytearray(), "extra buffered operation data")
                self.assertFalse(select.select([op_read], [], [], 0.2)[0], "unexpected extra operation data")
                srv.release_after.set()
                self.wait_output(proc, output, AFTER)
                data = output.read_bytes()
                self.assertEqual(data, PREFIX + BEFORE + AFTER, "output must be prefix+before+after exactly")
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual(list(root.rglob("out")), [output], "nonselected output fan-out")
                self.assertEqual({str(p.relative_to(root)) for p in root.rglob("*")}, tree_before, "extra local window or node")
                self.assertEqual(len(srv.posts), 2, "expected one JOIN one SEND")
                self.assertEqual(srv.methods, ["DELETE" if other_part else "POST", "POST"])
                control_path = "/networks/NetA/channels/%23Other" if other_part else "/networks/NetA/channels"
                self.assertEqual(srv.paths.count(control_path), 1)
                self.assertEqual(srv.paths.count("/networks/NetA/channels/%23Room/messages"), 1)
                if other_part:
                    self.assertEqual(srv.posts[0], b"")
                else:
                    self.assertEqual(json.loads(srv.posts[0]), {"name": "#Other"})
                self.assertEqual(json.loads(srv.posts[1]), {"body": TEXT})
                # Each mutation requires its own fresh config and bound identity.
                for post_path in [control_path, "/networks/NetA/channels/%23Room/messages"]:
                    idx = srv.paths.index(post_path)
                    self.assertGreaterEqual(idx, 2)
                    self.assertEqual(srv.paths[idx - 2], "/api/config")
                    self.assertEqual(srv.paths[idx - 1], "/me")
                proc.send_signal(signal.SIGTERM)
                stdout, stderr = proc.communicate(timeout=budget(12))
                self.assertEqual(proc.returncode, 0)
                self.assertEqual(stdout + stderr, b"")
                self.assertEqual(credential.read_bytes(), original_credential)
                for fd, original_flags in flags.items():
                    self.assertEqual((fcntl.fcntl(fd, fcntl.F_GETFL), fcntl.fcntl(fd, fcntl.F_GETFD)), original_flags)
            finally:
                srv.stopped.set()
                srv.posted.set()
                srv.release_after.set()
                if proc.poll() is None:
                    proc.kill()
                proc.communicate(timeout=budget(10))
                if writer >= 0:
                    os.close(writer)
                for fd in (ready_read, ready_write, op_read, op_write):
                    os.close(fd)

    def test_join_then_literal_send_same_session(self):
        self.run_control()

    def test_selected_part_stops_without_followup_send(self):
        self.run_control(operation="part-selected")

    def test_other_part_then_literal_send_same_session(self):
        self.run_control(operation="part-other")

if __name__ == "__main__":
    unittest.main()
