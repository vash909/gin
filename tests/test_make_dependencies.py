"""Check fresh make graphs without executing compilers or test recipes."""
import os
from pathlib import Path
import shlex
import signal
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
if os.environ.get("GIN_MAKE_GRAPH_INSPECTION") == "1":
    raise SystemExit("make dry-run unexpectedly executed the graph-test recipe")
MAKE = sys.argv.pop(1) if len(sys.argv) > 1 else "make"
TARGETS = (
    ".build/test_authority_json_config",
    ".build/test_authority_json_identity",
    ".build/test_authority_json_store",
    ".build/test_authority_json_connect",
)


class FreshCheckGraph(unittest.TestCase):
    def test_authority_units_are_built_before_check_executes_them(self):
        scratch = Path(os.environ.get("TMPDIR", ROOT / ".build/test-scratch"))
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="gin-make-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            # Only the graph is inspected; no application uses these symlinks.
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            env = dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1")
            child = subprocess.Popen(
                [MAKE, "-f", str(ROOT / "Makefile"), "-n", "check"],
                cwd=work, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                text=True, start_new_session=True,
            )
            try:
                stdout, stderr = child.communicate(timeout=10)
            finally:
                # Also bound/reap descendants if the graph unexpectedly executes.
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
                child.wait()
            self.assertEqual(child.returncode, 0, stderr)
            lines = stdout.splitlines()
            for target in TARGETS:
                with self.subTest(target=target):
                    builds = []
                    executions = []
                    for index, line in enumerate(lines):
                        fields = shlex.split(line)
                        # BSD make job mode wraps real commands in a shell group;
                        # its separate echo line is not an execution.
                        if fields[:1] == ["{"]:
                            fields = fields[1:]
                        if "-o" in fields and fields[fields.index("-o") + 1] == target:
                            builds.append(index)
                        if fields and fields[0] == target:
                            executions.append(index)
                    self.assertTrue(executions, "check no longer exercises " + target)
                    self.assertEqual(len(builds), 1,
                        "check would execute an unbuilt authority unit: " + target)
                    self.assertLess(builds[0], executions[0],
                        "authority unit compilation must precede execution")
            self.assertFalse((work / ".build").exists(), "dry graph unexpectedly executed recipes")


if __name__ == "__main__":
    unittest.main(verbosity=2)
