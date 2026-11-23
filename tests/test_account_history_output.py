#!/usr/bin/env python3
"""Opt-in API contract. Real loopback Native; locally decoded REST, no GET claim.

Run against the real opt-in implementation with GIN_ACCOUNT_HISTORY_OUTPUT_DRIVER.
The original interface-absence RED is retained separately, not a semantic bug.
Import only the fixture module, never an inherited TestCase's test methods.
"""
import os
import signal
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path
import test_account_dispatch as wire

class HistoryOutput(wire.Replan):
    def test_receive(self):
        self.run_storage(None)
    def test_partial(self):
        self.run_storage("partial")
    def test_replace(self):
        self.run_storage("replace")
    def run_storage(self, fault):
        with tempfile.TemporaryDirectory(prefix="gin-history-output-", dir=os.environ.get("TMPDIR") or str(wire.ROOT / ".build")) as temp:
            root = Path(temp) / "irc"
            with self.fixture() as (state, origin, _gate):
                env = dict(os.environ, GIN_DISPATCH_CASE="positive")
                env.pop("GIN_RECEIVE_CASE", None)
                env.pop("GIN_OUTPUT_CASE", None)
                env.pop("GIN_HISTORY_OUTPUT_CASE", None)
                if fault:
                    env["GIN_HISTORY_OUTPUT_CASE"] = fault
                proc = subprocess.Popen([os.environ["GIN_ACCOUNT_HISTORY_OUTPUT_DRIVER"], origin, str(self.certdir / "ca.pem"), str(root)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True, env=env)
                try:
                    out, err = proc.communicate(timeout=18)
                except BaseException:
                    if proc.poll() is None:
                        os.killpg(proc.pid, signal.SIGKILL)
                    proc.communicate()
                    raise
                self.assertEqual(proc.returncode, 0, err)
                self.assertEqual(err, b"")
                expected_out = b"PASS history REST MERGED scope no-mutation six-admissions five-destinations\n"
                if fault:
                    expected_out += b"PASS storage boundary sticky unchanged-checkpoint ambiguous-reopen\n"
                self.assertEqual(out, expected_out)
                self.assertNotIn(wire.TOKEN.encode(), out + err)
            def line(kind, sender="sender", body="hello"):
                return (f'[1970-01-01T00:00:00.123Z] {kind} sender="{sender}" body="{body}"\n').encode()
            trusted_line = line("notice", "trusted", "prefix")
            baseline_out = line("server_event")
            # six admissions: one/out server_event, two/out notice, one/#old1 privmsg, two/#old2 privmsg, one/@peer privmsg*2
            # before fault trusted prefix 7 adds one notice line to one/out
            logs = {str(p.relative_to(root)): p.read_bytes() for p in root.rglob("out")}
            saved = root / "one" / "out.saved"
            if fault is None:
                expected = {
                    "one/out": baseline_out + trusted_line,
                    "two/out": line("notice"),
                    "one/#old1/out": line("privmsg"),
                    "two/#old2/out": line("privmsg"),
                    "one/@peer/out": line("privmsg") * 2,
                }
                # healthy closing receiver before final cleanup still one EOF, lease retained probe is native side
                self.assertEqual(logs, expected)
            elif fault == "partial":
                expected = {
                    "one/out": baseline_out + trusted_line + b"[1970",
                    "two/out": line("notice"),
                    "one/#old1/out": line("privmsg"),
                    "two/#old2/out": line("privmsg"),
                    "one/@peer/out": line("privmsg") * 2,
                }
                self.assertEqual(logs, expected)
                self.assertFalse(saved.exists())
            else: # replace
                expected = {
                    "one/out": b"replacement\n",
                    "two/out": line("notice"),
                    "one/#old1/out": line("privmsg"),
                    "two/#old2/out": line("privmsg"),
                    "one/@peer/out": line("privmsg") * 2,
                }
                self.assertEqual(logs, expected)
                self.assertEqual(saved.read_bytes(), baseline_out + trusted_line)
                self.assertNotEqual(saved.stat().st_ino, (root / "one" / "out").stat().st_ino)
            checkpoints = list(root.rglob(".gin.checkpoint"))
            self.assertEqual(len(checkpoints), 5)
            for cp in checkpoints:
                data = cp.read_bytes()
                st = cp.stat()
                self.assertEqual(st.st_mode & 0o7777, 0o600)
                self.assertEqual(st.st_nlink, 1)
                is_one = cp.parent == root / "one"
                log_path = cp.parent / ("out.saved" if fault == "replace" and is_one else "out")
                log = log_path.stat()
                self.assertEqual(data[:8], b"GINCP\0\0\1")
                self.assertEqual(int.from_bytes(data[8:16], "big"), 7 if is_one else 0)
                self.assertEqual(int.from_bytes(data[16:24], "big"), 1)
                self.assertEqual(int.from_bytes(data[24:32], "big"), log.st_dev)
                self.assertEqual(int.from_bytes(data[32:40], "big"), log.st_ino)
                expected_size = log.st_size - (5 if fault == "partial" and is_one else 0)
                self.assertEqual(int.from_bytes(data[40:48], "big"), expected_size)
                self.assertIn(origin.encode(), data)
                self.assertIn(b"22345678-1234-1234-1234-123456789abc", data)
                self.assertNotIn(wire.TOKEN.encode(), data)
            for entry in root.rglob("*"):
                st = entry.lstat()
                self.assertEqual(stat.S_IMODE(st.st_mode), 0o700 if stat.S_ISDIR(st.st_mode) else 0o600)
                if stat.S_ISREG(st.st_mode):
                    self.assertEqual(st.st_nlink, 1)
                    self.assertNotIn(wire.TOKEN.encode(), entry.read_bytes())
            self.assertEqual(state["paths"], ["/api/config", "/me", "/socket/websocket?vsn=2.0.0&client_proto=4"])
            self.assertEqual(len(state["joins"]), 10)
            self.assertEqual(state["queries"], [])
            self.assertEqual(state["eof"], [1])
            self.assertEqual(state["live"], 0)

if __name__ == "__main__":
    unittest.main(verbosity=2)
