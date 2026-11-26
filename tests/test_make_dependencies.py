"""Check fresh make graphs without executing compilers or test recipes."""
import os
from pathlib import Path
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
if os.environ.get("GIN_MAKE_GRAPH_INSPECTION") == "1":
    raise SystemExit("make dry-run unexpectedly executed the graph-test recipe")
MAKE = sys.argv.pop(1) if len(sys.argv) > 1 else "make"


def graph_scratch():
    """Keep graph workspaces absolute, including empty or relative TMPDIR."""
    directory = Path(os.environ.get("TMPDIR") or ROOT / ".build/test-scratch").resolve()
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def graph_header_override(work):
    """Isolate dependency-only SDK sentinels; never compile these headers."""
    directory = work / "graph-sdk"
    directory.mkdir(exist_ok=True)
    headers = [directory / name for name in ("memcheck.h", "valgrind.h")]
    for header in headers:
        if not header.exists():
            header.write_text("/* graph dependency sentinel, not a compiler input */\n")
            os.utime(header, (1_000_000_000, 1_000_000_000))
    return ["VALGRIND_HEADERS=" + " ".join(str(header) for header in headers)]


TARGETS = (
    ".build/test_account_receive_control",
    ".build/test_status_projection",
    ".build/test_status_projection_fidelity",
    ".build/test_status_render",
    ".build/test_connect_replan",
    ".build/test_authority_json_config",
    ".build/test_authority_json_identity",
    ".build/test_authority_json_store",
    ".build/test_authority_json_connect",
    ".build/test_account_naming",
    ".build/test_account_checkpoint",
    ".build/test_account_network_checkpoint",
    ".build/test_account_root",
    ".build/test_account_root_fault",
    ".build/test_account_plan",
    ".build/test_account_plan_fault",
    ".build/test_account_live_queue",
    ".build/test_account_sinks",
    ".build/test_account_sinks_fault",
)
PRIVATE_DRIVER_TARGETS = (
    ("GIN_REPLAN_DRIVER", ".build/connect_replan_wss_driver", "tests/test_connect_replan_wss.py"),
    ("TP_DRIVER", ".build/terminal_password_driver", "tests/test_terminal_password.py"),
    ("TP_FAULT_DRIVER", ".build/terminal_password_fault_driver", "tests/test_terminal_password.py"),
    ("DRIVER", ".build/account_fs_driver", "tests/test_account_fs.py"),
    ("GIN_LOGIN_CONTROL_BINARY", ".build/auth_login_control_driver", "tests/test_auth_login_control.py"),
    ("GIN_DISCOVERY_DRIVER", ".build/account_discovery_driver", "tests/test_account_discovery.py"),
    ("INTERACTIVE_DRIVER", ".build/interactive_login_driver", "tests/test_interactive_login.py"),
    ("GIN_ACCOUNT_OUTPUT_DRIVER", ".build/account_output_driver", "tests/test_account_output.py"),
    ("GIN_ACCOUNT_HISTORY_OUTPUT_DRIVER", ".build/account_history_output_driver", "tests/test_account_history_output.py"),
    ("GIN_ACCOUNT_HISTORY_BOOTSTRAP_DRIVER", ".build/account_history_bootstrap_driver", "tests/test_account_history_bootstrap.py"),
    ("GIN_ACCOUNT_OWNER_DRIVER", ".build/account_owner_driver", "tests/test_account_owner.py"),
)


RECEIVE_CASES = ("positive", "wrong_uuid", "overflow", "bytes")
DISPATCH_CASES = (
    "positive", "wrong_event_query", "wrong_event_nick", "life_joined", "life_kicked",
    "query_same", "query_changed", "nick_same", "nick_changed", "life_unknown",
    "life_offscope", "life_malformed", "life_encrypted", "diagnostic", "channel_dm",
    "wrong_source", "wrong_network", "unknown_dm", "encrypted",
)
TRANSPORT_SCENARIOS = (
    ("GIN_ACCOUNT_RECEIVE_DRIVER", ".build/account_receive_driver", "GIN_RECEIVE_CASE",
     "tests/test_account_receive.py", RECEIVE_CASES),
    ("GIN_ACCOUNT_DISPATCH_DRIVER", ".build/account_dispatch_driver", "GIN_DISPATCH_CASE",
     "tests/test_account_dispatch.py", DISPATCH_CASES),
    ("GIN_ACCOUNT_DISPATCH_DRIVER", ".build/test_account_dispatch_fidelity", "GIN_DISPATCH_CASE",
     "tests/test_account_dispatch.py", ("positive", "fidelity_kind", "fidelity_peer", "fidelity_id")),
)


INVENTORY_CASES = (
    "inventory_add", "inventory_same", "inventory_wrong_event", "inventory_wrong_source",
    "inventory_malformed", "inventory_unknown_network", "inventory_cap", "inventory_encrypted",
)


class FreshCheckGraph(unittest.TestCase):
    HISTORY_ONE_ROLE = ".build/account_history_one_wss_driver"

    def history_one_expected(self, instrumented=False):
        return (["GIN_HISTORY_ONE_SETUP_PROFILE=instrumented-positive", "GIN_HISTORY_ONE_PREFIX=valgrind --trace-children=yes --error-exitcode=99 --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all --log-file=.build/history-one-valgrind-$$.%p.log"] if instrumented else []) + ["GIN_HISTORY_ONE_DRIVER=${PWD}/" + self.HISTORY_ONE_ROLE,
                "GIN_HISTORY_ONE_ROOT=${PWD}/.build/history-one-root-$$",
                "GIN_HISTORY_ONE_RECEIPT=${PWD}/.build/history-one-root-$$/receipt.json",
                sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests",
                "-p", "test_account_history_one_wss.py", "-v"]

    def test_history_one_exact_argv(self):
        expected = self.history_one_expected()
        for fields, trailer in ((expected, None), (["{", *expected, ";", "}"], None),
                                (["{", *expected], ["}", "||", "exit", "$?"])):
            self.assert_hot_command(fields, expected, trailer)
        for bad in ([*expected, "-k", "quiet"], ["env", *expected],
                    ["GIN_HISTORY_ONE_DRIVER=.build/wrong", *expected[1:]],
                    [*expected[:3], "python3", *expected[4:]]):
            with self.assertRaises(AssertionError):
                self.assert_hot_command(bad, expected)

    def test_history_one_suite_exact_five(self):
        import test_account_history_one_wss as history
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(history).countTestCases(), 5)
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(history.QuietHistory), [
            "test_held_history_services_old_live_then_nonempty_rest",
            "test_held_unsupported_message_cannot_drain_success",
            "test_nonrow_metadata_remains_acceptable",
            "test_quiet_new_query_requires_focused_history",
            "test_unsupported_message_cannot_drain_success"])

    def test_history_one_prefix_policy(self):
        from unittest.mock import patch
        import test_account_history_one_wss as history
        tail = ['/private/binary', 'https://127.0.0.1:1', '/private/ca', '/private/root', 'held-unsupported']
        for raw, prefix in ((None, []), ('', []), ('   ', []),
                            ("'/private/recording wrapper' --tag", ['/private/recording wrapper', '--tag'])):
            with self.subTest(raw=raw), patch.dict(os.environ, {'GIN_HISTORY_ONE_DRIVER': tail[0]}, clear=True):
                if raw is not None:
                    os.environ['GIN_HISTORY_ONE_PREFIX'] = raw
                self.assertEqual(history.driver_command(*tail[1:]), prefix + tail)
        for profile in (None, '', 'ordinary', 'instrumented-positive'):
            for prefix in ('', '/private/recording-wrapper', 'valgrind'):
                with patch.dict(os.environ, {'GIN_HISTORY_ONE_DRIVER': tail[0],
                                            'GIN_HISTORY_ONE_PREFIX': prefix}, clear=True):
                    if profile is not None:
                        os.environ['GIN_HISTORY_ONE_SETUP_PROFILE'] = profile
                    flags = ['--instrumented-positive-setup'] if profile == 'instrumented-positive' else []
                    self.assertEqual(history.driver_command(*tail[1:]), shlex.split(prefix)+tail+flags)
        for profile in ('unknown', ' instrumented-positive', 'INSTRUMENTED', '   '):
            with patch.dict(os.environ, {'GIN_HISTORY_ONE_DRIVER': tail[0],
                                        'GIN_HISTORY_ONE_SETUP_PROFILE': profile}, clear=True):
                with self.assertRaises(ValueError):
                    history.driver_command(*tail[1:])
        with patch.dict(os.environ, {'GIN_HISTORY_ONE_PREFIX': "'malformed"}):
            with self.assertRaises(ValueError):
                history.driver_command(*tail[1:])

    def test_history_one_canonical_strict_registration(self):
        text = (ROOT / 'scripts/check.sh').read_text()
        self.assertEqual(text.count('make CURL_CONFIG="$host_curl_config" check-account-history-one-valgrind'), 1)

    def test_history_one_recursive_inputs(self):
        self.assert_project_inputs_rebuild(self.HISTORY_ONE_ROLE)

    def test_history_one_make_prefix_checkout_paths(self):
        """Real Make/shell plus fixture second shlex: argv regression, not transport."""
        import json
        with tempfile.TemporaryDirectory(prefix="gin-prefix-", dir=graph_scratch()) as temporary:
            base = Path(temporary)
            recorder = base / "record.py"
            recorder.write_text("import json, os, sys\n"
                "sys.path.insert(0, os.environ['GIN_ARGV_TESTS'])\n"
                "import test_account_history_one_wss as history\n"
                "command = history.driver_command('https://127.0.0.1:1', 'ca.pem', 'root', 'quiet')\n"
                "print(json.dumps({'argv': command, 'cwd': os.getcwd(), 'profile': os.environ.get('GIN_HISTORY_ONE_SETUP_PROFILE')}))\n")
            for name in ("ordinary-control", "checkout with spaces", "checkout's apostrophe"):
                work = base / name
                work.mkdir()
                for directory in ("src", "include", "tests"):
                    (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
                shutil.copyfile(ROOT / "Makefile", work / "Makefile")
                artifact = work / self.HISTORY_ONE_ROLE
                artifact.parent.mkdir()
                # Synthetic up-to-date prerequisite only; never executed as Native.
                artifact.write_bytes(b"")
                future = max(path.stat().st_mtime for directory in ("src", "include", "tests")
                             for path in (ROOT / directory).iterdir()) + 60
                os.utime(artifact, (future, future))
                for strict in (False, True):
                    with self.subTest(checkout=name, strict=strict):
                        result = subprocess.run([MAKE, "-f", "Makefile",
                            "PYTHON=" + shlex.join([sys.executable, "-B", str(recorder)]),
                            "check-account-history-one" + ("-valgrind" if strict else "")],
                            cwd=work, env=dict(os.environ, GIN_ARGV_TESTS=str(ROOT / "tests")),
                            capture_output=True, text=True, timeout=10, check=False)
                        self.assertEqual(result.returncode, 0, "real Make argv recorder: " + result.stderr)
                        records = [json.loads(line) for line in result.stdout.splitlines()
                                   if line.startswith('{"argv":')]
                        self.assertEqual(len(records), 1, result.stdout)
                        record = records[0]
                        self.assertEqual(record['cwd'], str(work))
                        tail = [str(work / self.HISTORY_ONE_ROLE),
                                'https://127.0.0.1:1', 'ca.pem', 'root', 'quiet']
                        argv = record['argv']
                        if strict:
                            self.assertEqual(argv[:6], self.OUTPUT_STATUS_VG)
                            logs = [word for word in argv if word.startswith('--log-file=')]
                            self.assertEqual(len(logs), 1, "exactly one --log-file argument")
                            self.assertRegex(logs[0], r'^--log-file=\.build/history-one-valgrind-[0-9]+\.%p\.log$')
                            self.assertEqual(argv[7:], tail + ['--instrumented-positive-setup'],
                                             "second-shlex must preserve exact argv without wrong path tokens")
                            self.assertEqual(record['profile'], 'instrumented-positive')
                        else:
                            self.assertEqual(argv, tail)
                            self.assertIsNone(record['profile'])

    def test_history_one_entry_graphs(self):
        with tempfile.TemporaryDirectory(prefix="gin-history-one-graph-", dir=graph_scratch()) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-history-one",
                          "check-account-receive-hot", "check-account-history-one-valgrind"):
                with self.subTest(entry=entry):
                    child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"),
                        "PYTHON=" + shlex.join([sys.executable, "-B"]), "-n", entry], cwd=work,
                        env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                    try:
                        stdout, stderr = child.communicate(timeout=10)
                    finally:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                    self.assertEqual(child.returncode, 0, "history-one entry graph: " + stderr)
                    raw = stdout.splitlines()
                    lines = [(i, shlex.split(line)) for i, line in enumerate(raw)
                             if not re.fullmatch(r"--- [^\n]+ ---", line.strip())
                             and not line.startswith("echo ")]
                    builds = [(i, fields) for i, fields in lines if "-o" in fields
                              and fields[fields.index("-o") + 1] == self.HISTORY_ONE_ROLE]
                    calls = [(i, fields) for i, fields in lines
                             if "test_account_history_one_wss.py" in fields]
                    if entry == "check-account-receive-hot":
                        self.assertEqual(builds, [], "receiver-only scope must not build focused history")
                        self.assertEqual(calls, [], "receiver-only scope must not run focused history")
                        continue
                    self.assertEqual(len(builds), 1, "focused history compiler exactly once")
                    linked = [field for field in builds[0][1] if field.endswith(".c")]
                    self.assertEqual(len(linked), len(set(linked)), "no duplicate translation units")
                    for source in ("tests/account_history_one_wss_driver.c", "src/account_history_one.c",
                                   "src/account_output.c", "src/account_dispatch.c", "src/account_receive.c",
                                   "src/account_registry.c", "src/history_fetch.c", "src/history_recovery.c",
                                   "src/connect.c", "src/checkpoint.c"):
                        self.assertIn(source, linked)
                    self.assertEqual(len(calls), 1, "focused history suite exactly once and unfiltered")
                    i, fields = calls[0]
                    self.assert_hot_command(fields, self.history_one_expected(entry.endswith("-valgrind")),
                        shlex.split(raw[i + 1]) if i + 1 < len(raw) else None)
                    self.assertLess(builds[0][0], i, "compile before full five-method suite")
                    self.assertFalse((work / ".build").exists(), "dry graph must not execute recipes")

    OUTPUT_STATUS_SELECTORS = ("PLAN", "REGISTRY", "SINKS", "CHECKPOINT")
    OUTPUT_STATUS_ROLES = tuple(".build/test_account_output_hot_status" + suffix
                                for suffix in ("", "_plain", "_vg"))
    OUTPUT_STATUS_VG = ["valgrind", "--trace-children=yes", "--error-exitcode=99",
                        "--leak-check=full", "--show-leak-kinds=all",
                        "--errors-for-leak-kinds=all"]

    def test_output_status_exact_argv(self):
        for role in self.OUTPUT_STATUS_ROLES:
            expected = (self.OUTPUT_STATUS_VG if role.endswith("_vg") else []) + [role]
            self.assert_hot_command(expected, expected)
            self.assert_hot_command(["{", *expected], expected, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "--lease-probe"], ["env", *expected],
                        [*expected, "-k", "status"]):
                with self.assertRaises(AssertionError):
                    self.assert_hot_command(bad, expected)

    def test_output_status_entry_graphs(self):
        entries = (("check", (0, 1)), ("check-account-components", (0, 1)),
                   ("check-account-resources", (0, 1)),
                   ("check-account-resources-valgrind", (2,)),
                   ("check-account-output-status-native", (0, 1)),
                   ("check-account-output-status-valgrind", (2,)))
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-output-status-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry, indices in entries:
                with self.subTest(entry=entry):
                    result = subprocess.run([MAKE, *graph_header_override(work), "-f",
                        str(ROOT / "Makefile"), "-n", entry], cwd=work,
                        env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        capture_output=True, text=True, timeout=15, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    raw = result.stdout.splitlines()
                    lines = [(i, shlex.split(line)) for i, line in enumerate(raw)
                             if not re.fullmatch(r"--- [^\n]+ ---", line.strip())
                             and not line.startswith("echo ")]
                    for index in indices:
                        role = self.OUTPUT_STATUS_ROLES[index]
                        builds = [(i, w) for i, w in lines if "-o" in w and
                                  w[w.index("-o") + 1] == role]
                        self.assertEqual(len(builds), 1, role)
                        i, fields = builds[0]
                        sources = [w for w in fields if w.endswith(".c")]
                        self.assertEqual(len(sources), len(set(sources)))
                        self.assertIn("tests/test_account_output_hot_status.c", sources)
                        for name in ("account_output", "account_plan", "account_registry",
                                     "account_sinks", "checkpoint"):
                            self.assertNotIn("src/" + name + ".c", sources)
                        guarded = index != 1
                        self.assertEqual(fields.count("-DGIN_STATUS_GUARDS"), int(guarded))
                        self.assertEqual(sources.count("tests/output_status_guard.c"), int(guarded))
                        self.assertEqual(fields.count("-DGIN_STATUS_VALGRIND"), int(index == 2))
                        wraps = [w for w in fields if w.startswith("-Wl,--wrap=")]
                        expected_wraps = ["-Wl,--wrap=" + name for name in
                            ("malloc", "calloc", "realloc", "free", "read", "write", "close",
                             "fstat", "fstatat", "lseek", "fsync", "clock_gettime", "time",
                             "gettimeofday", "gin_account_sinks_hot_bind",
                             "gin_account_sinks_hot_append", "gin_checkpoint_commit",
                             "gin_checkpoint_close", "open", "openat", "fcntl")]
                        self.assertEqual(wraps, expected_wraps if guarded else [])
                        objects = [w for w in fields if w.endswith(".o")]
                        suffix = ("native", "plain", "vg")[index]
                        self.assertEqual(objects, [".build/output_status_" + suffix + "_" +
                            selector.lower() + ".o" for selector in self.OUTPUT_STATUS_SELECTORS])
                        for obj, selector in zip(objects, self.OUTPUT_STATUS_SELECTORS):
                            compiles = [(j, w) for j, w in lines if "-o" in w and
                                        w[w.index("-o") + 1] == obj]
                            self.assertEqual(len(compiles), 1)
                            j, command = compiles[0]
                            self.assertLess(j, i)
                            self.assertEqual(command[0], fields[0])
                            self.assertEqual([w for w in command if w.endswith(".c")],
                                             ["tests/output_status_snapshot_probe.c"])
                            self.assertEqual([w for w in command if w.startswith("-DGIN_STATUS_PROBE_")],
                                             ["-DGIN_STATUS_PROBE_" + selector])
                            self.assertNotIn("-DGIN_STATUS_VALGRIND", command)
                        calls = [(j, w) for j, w in lines if role in w and "-o" not in w]
                        self.assertEqual(len(calls), 1)
                        j, command = calls[0]
                        self.assertLess(i, j)
                        expected = (self.OUTPUT_STATUS_VG if index == 2 else []) + [role]
                        self.assert_hot_command(command, expected,
                            shlex.split(raw[j+1]) if j+1 < len(raw) else None)
            self.assertFalse((work / ".build").exists())

    def test_output_status_recursive_inputs(self):
        for role in self.OUTPUT_STATUS_ROLES:
            with self.subTest(role=role):
                self.assert_project_inputs_rebuild(role)
        for profile in ("native", "plain", "vg"):
            for selector in self.OUTPUT_STATUS_SELECTORS:
                self.assert_project_inputs_rebuild(".build/output_status_" + profile + "_" +
                                                   selector.lower() + ".o")

    def test_output_status_no_optional_headers(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-output-no-vg-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for role in self.OUTPUT_STATUS_ROLES[:2]:
                result = subprocess.run([MAKE, "-f", str(ROOT / "Makefile"), "-n",
                    "VALGRIND_HEADERS=" + str(work / "missing-optional-header"), role],
                    cwd=work, capture_output=True, text=True, timeout=15, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn("-DGIN_STATUS_VALGRIND", result.stdout)
            control = subprocess.run([MAKE, "-f", str(ROOT / "Makefile"), "-n",
                "VALGRIND_HEADERS=" + str(work / "missing-optional-header"),
                self.OUTPUT_STATUS_ROLES[2]], cwd=work, capture_output=True,
                text=True, timeout=15, check=False)
            self.assertNotEqual(control.returncode, 0)
            self.assertFalse((work / ".build").exists())

    RESOURCE_ROLES = ('.build/test_credential_identity', '.build/test_account_root_hot_fault', '.build/test_account_fs_hot_fault', '.build/test_account_sinks_hot', '.build/test_account_sinks_hot_bind', '.build/test_account_sinks_hot_status', '.build/test_account_output_hot')

    def test_resources_graph_exact_once(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-resources-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-resources"):
                child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n",
                    "PYTHON=" + shlex.join([sys.executable, "-B"]), entry], cwd=work,
                    env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                try:
                    out, err = child.communicate(timeout=15)
                finally:
                    try: os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError: pass
                    child.wait()
                self.assertEqual(child.returncode, 0, "resource entry missing: " + err)
                raw = out.splitlines()
                lines = [(i, shlex.split(s)) for i, s in enumerate(raw)
                         if not re.fullmatch(r"--- [^\n]+ ---", s.strip())]
                lines = [(i, w) for i, w in lines if w[:1] != ["echo"]]
                for role in self.RESOURCE_ROLES:
                    builds = [(i,w) for i,w in lines if "-o" in w and w[w.index("-o")+1] == role]
                    self.assertEqual(len(builds), 1, "resource compiler exactly once: " + role)
                    linked = [w for w in builds[0][1] if w.endswith(".c")]
                    self.assertEqual(len(linked), len(set(linked)), "resource duplicate TU: " + role)
                    expected = [[role]] if role != ".build/test_account_sinks_hot" else [
                        ["HOT_FAULT=" + mode, role] for mode in ("", "missing", "mismatch", "symlink", "postpair", "taint")]
                    calls = [(i,w) for i,w in lines if role in w and "-o" not in w]
                    self.assertEqual(len(calls), len(expected), "resource scenarios exactly once: " + role)
                    for (i,w), want in zip(calls, expected):
                        self.assert_hot_command(w, want, shlex.split(raw[i+1]) if i+1<len(raw) else None)
                        self.assertLess(builds[0][0], i)
                self.assertFalse((work / ".build").exists())

    def test_resources_all_legacy_sinks_closure(self):
        expected = ('.build/test_account_sinks', '.build/test_account_sinks_fault', '.build/account_owner_driver', '.build/account_history_bootstrap_driver', '.build/account_history_output_driver', '.build/account_output_driver', '.build/gin', '.build/gin_cleanup_fault', '.build/gin_receive_observer', '.build/gin_owner_fault', '.build/runtime_stderr', '.build/account_cli_control_driver', '.build/gin_account_fifo_observer')
        consumers = self.native_consumers(ROOT, ("src/account_sinks.c",))
        self.assertTrue(set(expected).issubset(consumers))
        for role in expected:
            with self.subTest(role=role):
                self.assert_project_inputs_rebuild(role)

    def test_resources_recursive_inputs(self):
        for role in self.RESOURCE_ROLES:
            with self.subTest(role=role):
                self.assert_project_inputs_rebuild(role)

    DISPATCH_HOT_ROLES = (".build/test_account_dispatch_hot",
                          ".build/account_dispatch_hot_wss_driver")

    def dispatch_hot_expected(self):
        return [[self.DISPATCH_HOT_ROLES[0]],
                ["GIN_ACCOUNT_DISPATCH_HOT_DRIVER=${PWD}/" + self.DISPATCH_HOT_ROLES[1],
                 "GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_receipt.json",
                 sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests",
                 "-p", "test_account_dispatch_hot_wss.py", "-v"]]

    def test_receive_hot_focused_closed_graph(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-receive-closed-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            result = subprocess.run([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n",
                "PYTHON=" + shlex.join([sys.executable, "-B"]), "check-account-receive-hot"],
                cwd=work, capture_output=True, text=True, timeout=15, check=False)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn("dispatch_hot", result.stdout,
                             "focused receiver must not execute undeclared dispatch roles")
            raw = result.stdout.splitlines()
            calls = [(i, shlex.split(line)) for i, line in enumerate(raw)
                     if not re.fullmatch(r"--- [^\n]+ ---", line.strip())
                     and not line.startswith("echo ")]
            for role in (".build/test_account_receive_hot", ".build/account_receive_hot_wss_driver"):
                builds = [(i,w) for i,w in calls if "-o" in w and w[w.index("-o")+1] == role]
                self.assertEqual(len(builds), 1)
            expected = [[".build/test_account_receive_hot"],
                ["GIN_ACCOUNT_HOT_DRIVER=${PWD}/.build/account_receive_hot_wss_driver",
                 sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests",
                 "-p", "test_account_receive_hot_wss.py", "-v"]]
            executions = [(i,w) for i,w in calls if "-o" not in w and
                (".build/test_account_receive_hot" in w or "test_account_receive_hot_wss.py" in w)]
            self.assertEqual(len(executions), 2)
            for (i, fields), want in zip(executions, expected):
                self.assert_hot_command(fields, want,
                    shlex.split(raw[i+1]) if i+1 < len(raw) else None)
            self.assertFalse((work / ".build").exists())

    def test_graph_discovery_without_real_valgrind_headers(self):
        self.assert_graph_without_real_valgrind_headers(False)

    def test_graph_profile_without_real_valgrind_headers(self):
        self.assert_graph_without_real_valgrind_headers(True)

    def assert_graph_without_real_valgrind_headers(self, profile):
        from unittest.mock import patch
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-no-vg-", dir=scratch) as temporary:
            work = Path(temporary)
            missing = work / "absent-real-sdk"
            self.assertFalse(missing.exists())
            text = (ROOT / "Makefile").read_text()
            text, count = re.subn(r"^VALGRIND_HEADERS = .*", "VALGRIND_HEADERS = " +
                str(missing / "memcheck.h") + " " + str(missing / "valgrind.h"), text, flags=re.M)
            self.assertEqual(count, 1)
            (work / "Makefile").write_text(text)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            # The real dedicated target must still require its external headers.
            control = subprocess.run([MAKE, "-n", ".build/account_dispatch_hot_wss_valgrind_driver"],
                cwd=work, capture_output=True, text=True, timeout=15, check=False)
            self.assertNotEqual(control.returncode, 0)
            self.assertIn("absent-real-sdk", control.stderr)
            # Pin nested scratch outside the substituted ROOT even without TMPDIR.
            with patch.dict(globals(), ROOT=work), patch.dict(os.environ, TMPDIR=str(scratch)):
                if profile:
                    self.test_dispatch_hot_valgrind_profile()
                else:
                    self.assertIn(".build/account_dispatch_hot_wss_valgrind_driver",
                                  self.native_consumers(work, ("src/account_dispatch.c",)))
            self.assertFalse(missing.exists())
            self.assertFalse((work / ".build").exists())

    def test_graph_scratch_environment_isolation(self):
        from unittest.mock import patch
        scratch = graph_scratch()
        original_root = ROOT
        original_present = "TMPDIR" in os.environ
        original_value = os.environ.get("TMPDIR")
        with tempfile.TemporaryDirectory(prefix="gin-scratch-control-", dir=scratch) as temporary:
            for mode in ("unset", "empty", "configured", "relative"):
                with self.subTest(mode=mode):
                    environment = dict(os.environ)
                    if mode == "unset":
                        environment.pop("TMPDIR", None)
                    elif mode == "relative":
                        environment["TMPDIR"] = os.path.relpath(temporary)
                    else:
                        environment["TMPDIR"] = "" if mode == "empty" else temporary
                    with patch.dict(os.environ, environment, clear=True):
                        expected = (Path(temporary) if mode in ("configured", "relative")
                                    else ROOT / ".build/test-scratch").resolve()
                        self.assertEqual(graph_scratch(), expected)
                        self.assertTrue(graph_scratch().is_absolute())
                        self.assert_graph_without_real_valgrind_headers(False)
                        self.assert_graph_without_real_valgrind_headers(True)
                        self.assertIs(ROOT, original_root)
                        self.assertEqual("TMPDIR" in os.environ, "TMPDIR" in environment)
                        self.assertEqual(os.environ.get("TMPDIR"), environment.get("TMPDIR"))
                    self.assertEqual("TMPDIR" in os.environ, original_present)
                    self.assertEqual(os.environ.get("TMPDIR"), original_value)

    def test_dispatch_hot_exact_argv(self):
        for expected in self.dispatch_hot_expected():
            self.assert_hot_command(expected, expected)
            self.assert_hot_command(["{", *expected, ";", "}"], expected)
            self.assert_hot_command(["{", *expected], expected, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "-k", "incremental"], ["env", *expected],
                        [*expected, "extra"], ["GIN_DISPATCH_CASE=positive", *expected]):
                with self.assertRaises(AssertionError):
                    self.assert_hot_command(bad, expected)

    def test_dispatch_hot_discovers_one(self):
        import test_account_dispatch_hot_wss as hot
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(hot).countTestCases(), 1)
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(hot.DispatchHot),
                         ["test_incremental_dispatch"])

    def test_dispatch_hot_entry_graphs(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-dispatch-hot-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-dispatch-hot"):
                child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n",
                    "PYTHON=" + shlex.join([sys.executable, "-B"]), entry], cwd=work,
                    env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                try:
                    out, err = child.communicate(timeout=15)
                finally:
                    try: os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError: pass
                    child.wait()
                self.assertEqual(child.returncode, 0, "dispatch hot entry missing: " + err)
                raw = out.splitlines()
                lines = [(i, shlex.split(s)) for i, s in enumerate(raw)
                         if not re.fullmatch(r"--- [^\n]+ ---", s.strip())]
                lines = [(i,w) for i,w in lines if w[:1] != ["echo"]]
                for role, want in zip(self.DISPATCH_HOT_ROLES, self.dispatch_hot_expected()):
                    builds = [(i,w) for i,w in lines if "-o" in w and w[w.index("-o")+1] == role]
                    self.assertEqual(len(builds), 1, "dispatch hot compiler exactly once: " + role)
                    linked = [w for w in builds[0][1] if w.endswith(".c")]
                    self.assertEqual(len(linked), len(set(linked)), "dispatch duplicate TU")
                    self.assertEqual(linked.count("src/account_registry.c"), 1)
                    if role == self.DISPATCH_HOT_ROLES[0]:
                        self.assertNotIn("src/account_dispatch.c", linked)
                        self.assertNotIn("src/account_receive.c", linked)
                        calls = [(i,w) for i,w in lines if role in w and "-o" not in w]
                    else:
                        self.assertIn("src/account_dispatch.c", linked)
                        self.assertIn("src/account_receive.c", linked)
                        self.assertIn("src/connect.c", linked)
                        calls = [(i,w) for i,w in lines if "test_account_dispatch_hot_wss.py" in w]
                    self.assertEqual(len(calls), 1, "dispatch hot exactly once unfiltered")
                    i,w = calls[0]
                    self.assert_hot_command(w, want, shlex.split(raw[i+1]) if i+1<len(raw) else None)
                    self.assertLess(builds[0][0], i)
                self.assertFalse((work / ".build").exists())

    def test_dispatch_hot_recursive_inputs(self):
        for role in self.DISPATCH_HOT_ROLES:
            with self.subTest(role=role):
                self.assert_project_inputs_rebuild(role)

    def test_dispatch_hot_valgrind_profile(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-dispatch-vg-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            output = subprocess.check_output([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n",
                "PYTHON=" + shlex.join([sys.executable, "-B"]),
                "check-account-dispatch-hot-valgrind"], cwd=work, text=True)
            raw = output.splitlines()
            lines = [(i, shlex.split(line)) for i,line in enumerate(raw)
                     if not re.fullmatch(r"--- [^\n]+ ---", line.strip())]
            role = ".build/account_dispatch_hot_wss_valgrind_driver"
            builds = [(i,w) for i,w in lines if "-o" in w and w[w.index("-o")+1] == role]
            self.assertEqual(len(builds), 1)
            self.assertEqual(builds[0][1].count("-DGIN_DISPATCH_VALGRIND"), 1)
            linked = [w for w in builds[0][1] if w.endswith(".c")]
            self.assertEqual(len(linked), len(set(linked)))
            for source in ("src/account_dispatch.c", "src/account_receive.c", "src/account_registry.c"):
                self.assertEqual(linked.count(source), 1)
            wrappers = [(i,w) for i,w in lines if "printf" in w]
            self.assertEqual(len(wrappers), 1)
            wi, wrapper = wrappers[0]
            offset = 1 if wrapper[:1] == ["{"] else 0
            command = shlex.split(wrapper[offset+3])
            expected_wrapper = ["printf", "%s\\n", "#!/bin/sh", wrapper[offset+3],
                                ">", ".build/account_dispatch_hot_wss_vg"]
            self.assert_hot_command(wrapper, expected_wrapper,
                shlex.split(raw[wi+1]) if wi+1<len(raw) else None)
            strict = ["valgrind", "--trace-children=yes", "--error-exitcode=99",
                      "--leak-check=full", "--show-leak-kinds=all", "--errors-for-leak-kinds=all"]
            self.assertEqual(command, ["exec", *strict,
                "${0%/*}/account_dispatch_hot_wss_valgrind_driver", "$@", "--instrumented-setup"])
            calls = [(i,w) for i,w in lines if "test_account_dispatch_hot_wss.py" in w]
            self.assertEqual(len(calls), 1)
            expected = self.dispatch_hot_expected()[1]
            expected[0] = "GIN_ACCOUNT_DISPATCH_HOT_DRIVER=${PWD}/.build/account_dispatch_hot_wss_vg"
            expected[1] = "GIN_ACCOUNT_DISPATCH_HOT_RECEIPT=.build/account_dispatch_hot_valgrind_receipt.json"
            i,w = calls[0]
            self.assert_hot_command(w, expected, shlex.split(raw[i+1]) if i+1<len(raw) else None)
            self.assertLess(builds[0][0], i)
            units = [(i,w) for i,w in lines if ".build/test_account_dispatch_hot" in w and "-o" not in w]
            self.assertEqual(len(units), 1)
            i,w = units[0]
            self.assert_hot_command(w, [*strict, ".build/test_account_dispatch_hot"],
                                    shlex.split(raw[i+1]) if i+1<len(raw) else None)
            self.assertFalse((work / ".build").exists())
        self.assert_project_inputs_rebuild(role)

    def test_dispatch_hot_legacy_recursive_inputs(self):
        consumers = self.native_consumers(ROOT, ("src/account_dispatch.c",))
        self.assertTrue(set((*self.DISPATCH_HOT_ROLES, ".build/account_dispatch_hot_wss_valgrind_driver")).issubset(consumers))
        for role in consumers:
            with self.subTest(role=role):
                self.assert_project_inputs_rebuild(role)

    RECEIVE_HOT_ROLES = (".build/test_account_receive_hot",
                         ".build/account_receive_hot_wss_driver")

    def receive_hot_expected(self):
        return [[self.RECEIVE_HOT_ROLES[0]],
                ["GIN_ACCOUNT_HOT_DRIVER=${PWD}/" + self.RECEIVE_HOT_ROLES[1],
                 sys.executable, "-B", "-m", "unittest", "discover", "-s", "tests",
                 "-p", "test_account_receive_hot_wss.py", "-v"]]

    def test_receive_hot_exact_argv(self):
        for expected in self.receive_hot_expected():
            self.assert_hot_command(expected, expected)
            self.assert_hot_command(["{", *expected, ";", "}"], expected)
            self.assert_hot_command(["{", *expected], expected, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "-k", "same_socket"], ["env", *expected],
                        [*expected, "extra"], ["GIN_RECEIVE_CASE=positive", *expected],
                        [w.replace("account_receive_hot_wss_driver", "account_receive_driver")
                         for w in expected]):
                if bad == expected:
                    continue
                with self.subTest(bad=bad), self.assertRaises(AssertionError):
                    self.assert_hot_command(bad, expected)

    def test_receive_hot_suite_discovers_six(self):
        import test_account_receive_hot_wss as hot
        suite = unittest.defaultTestLoader.loadTestsFromModule(hot)
        self.assertEqual(suite.countTestCases(), 6)
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(hot.OwnedHot), [
            "test_join_rejected", "test_join_timeout", "test_same_socket",
            "test_start_failure_before_me_retry",
            "test_start_rename_upgrade_failure_close_retry",
            "test_start_rename_upgrade_failure_retry"])

    def test_receive_hot_all_three_entry_graphs(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-receive-hot-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-receive-hot"):
                with self.subTest(entry=entry):
                    child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"),
                        "PYTHON=" + shlex.join([sys.executable, "-B"]), "-n", entry],
                        cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                    try:
                        stdout, stderr = child.communicate(timeout=10)
                    finally:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                    self.assertEqual(child.returncode, 0, "receive hot entry graph: " + stderr)
                    raw = stdout.splitlines()
                    lines = [(i, shlex.split(line)) for i, line in enumerate(raw)
                             if not re.fullmatch(r"--- [^\n]+ ---", line.strip())]
                    lines = [(i, f) for i, f in lines if f[:1] != ["echo"]]
                    for role, expected in zip(self.RECEIVE_HOT_ROLES, self.receive_hot_expected()):
                        builds = [(i, f) for i, f in lines if "-o" in f
                                  and f[f.index("-o") + 1] == role]
                        self.assertEqual(len(builds), 1, "receive hot compiler exactly once: " + role)
                        linked = [w for w in builds[0][1] if w.endswith(".c")]
                        self.assertEqual(len(linked), len(set(linked)), "receive hot duplicate link")
                        for source in ("src/account_plan.c", "src/account_naming.c",
                                       "src/session_live_queue.c", "src/command.c", "src/topology.c"):
                            self.assertIn(source, linked)
                        self.assertIn("tests/" + role.split("/")[-1] + ".c", linked)
                        if role == self.RECEIVE_HOT_ROLES[0]:
                            for included in ("src/connect.c", "src/account_receive.c", "tests/test_connect_hot.c"):
                                self.assertNotIn(included, linked, "included TU must not duplicate-link")
                            calls = [(i, f) for i, f in lines if role in f and "-o" not in f]
                        else:
                            self.assertIn("src/connect.c", linked)
                            self.assertIn("src/account_receive.c", linked)
                            calls = [(i, f) for i, f in lines
                                     if any("test_account_receive_hot_wss.py" in w for w in f)]
                        self.assertEqual(len(calls), 1, "receive hot execution exactly once unfiltered: " + role)
                        i, fields = calls[0]
                        self.assert_hot_command(fields, expected,
                            shlex.split(raw[i + 1]) if i + 1 < len(raw) else None)
                        self.assertLess(builds[0][0], i, "receive hot compile before suite")
                    if entry != "check-account-receive-hot":
                        legacy = [(i, f) for i, f in lines
                                  if "tests/test_account_receive.py" in f]
                        self.assertEqual(len(legacy), len(RECEIVE_CASES),
                                         "legacy receive selectors exactly once")
                        for mode, (i, fields) in zip(RECEIVE_CASES, legacy):
                            self.assert_hot_command(fields,
                                ["GIN_ACCOUNT_RECEIVE_DRIVER=.build/account_receive_driver",
                                 "GIN_RECEIVE_CASE=" + mode, sys.executable, "-B",
                                 "tests/test_account_receive.py"],
                                shlex.split(raw[i + 1]) if i + 1 < len(raw) else None)
                    self.assertFalse((work / ".build").exists())

    def test_receive_hot_all_actual_consumers_recursive_inputs(self):
        consumers = self.native_consumers(ROOT, ("src/account_receive.c", "src/account_receive_hot.h"))
        self.assertTrue(set(self.RECEIVE_HOT_ROLES).issubset(consumers),
                        "receive hot roles missing from consumer audit")
        for target in consumers:
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    HOT_ROLES = (".build/test_connect_hot", ".build/test_connect_hot_fault",
                 ".build/connect_hot_wss_driver")

    def assert_hot_command(self, fields, expected, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"],
                                 "unknown hot multiline shell trailer")
                fields = fields[1:]
        self.assertEqual(fields, expected, "hot command requires exact whole argv")

    def hot_expected(self):
        return [[self.HOT_ROLES[0]], [self.HOT_ROLES[1]],
                ["GIN_HOT_DRIVER=${PWD}/" + self.HOT_ROLES[2], sys.executable,
                 "-B", "-m", "unittest", "discover", "-s", "tests", "-p",
                 "test_connect_hot_wss.py", "-v"]]

    def test_hot_exact_argv_shell_grammar(self):
        for expected in self.hot_expected():
            self.assert_hot_command(expected, expected)
            self.assert_hot_command(["{", *expected, ";", "}"], expected)
            self.assert_hot_command(["{", *expected], expected, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "extra"], ["env", *expected], ["echo", *expected],
                        [*expected, "-k", "query"], ["{", *expected, "}"],
                        ["{", *expected, ";", "}", ";"]):
                with self.subTest(bad=bad), self.assertRaises(AssertionError):
                    self.assert_hot_command(bad, expected)
            with self.assertRaises(AssertionError):
                self.assert_hot_command(["{", *expected], expected, ["}", "||", "exit", "0"])

    def assert_hot_graph(self, stdout):
        raw = stdout.splitlines()
        lines = []
        for index, line in enumerate(raw):
            # BSD target banners are display metadata, not shell commands.
            if re.fullmatch(r"--- [^\n]+ ---", line.strip()):
                continue
            fields = shlex.split(line)
            if fields[:1] == ["echo"]:
                continue
            lines.append((index, fields))
        for role, expected in zip(self.HOT_ROLES, self.hot_expected()):
            builds = [(i, f) for i, f in lines if "-o" in f and f[f.index("-o") + 1] == role]
            self.assertEqual(len(builds), 1, "hot role requires exactly one compiler: " + role)
            linked = [w for w in builds[0][1] if w.endswith(".c")]
            self.assertEqual(len(linked), len(set(linked)), "duplicate hot link inputs")
            self.assertIn("src/command.c", linked)
            self.assertIn("src/topology.c", linked)
            self.assertIn("src/topology_lookup.c", linked)
            self.assertIn("tests/" + role.split("/")[-1] + ".c", linked)
            if role == self.HOT_ROLES[2]:
                self.assertIn("src/connect.c", linked)
                calls = [(i, f) for i, f in lines if any("test_connect_hot_wss.py" in w for w in f)]
            else:
                self.assertNotIn("src/connect.c", linked, "unit includes actual connect.c")
                calls = [(i, f) for i, f in lines if role in f and "-o" not in f]
            self.assertEqual(len(calls), 1, "hot role requires exactly one unfiltered execution: " + role)
            i, fields = calls[0]
            next_fields = shlex.split(raw[i + 1]) if i + 1 < len(raw) else None
            self.assert_hot_command(fields, expected, next_fields)
            self.assertLess(builds[0][0], i, "hot driver must build before execution")

    def test_hot_all_three_entry_graphs(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-hot-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-connect-hot"):
                with self.subTest(entry=entry):
                    child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"),
                        "PYTHON=" + shlex.join([sys.executable, "-B"]), "-n", entry],
                        cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                    try:
                        stdout, stderr = child.communicate(timeout=10)
                    finally:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                    self.assertEqual(child.returncode, 0, "SETUP: " + stderr)
                    self.assert_hot_graph(stdout)
                    self.assertFalse((work / ".build").exists())

    def test_hot_roles_recursive_project_headers(self):
        for target in self.HOT_ROLES:
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def native_consumers(self, source_root, needles=("src/connect.c",)):
        # Discover expanded inputs in a fresh graph, never in the caller's
        # possibly warm build tree. GNU and BSD make need no force-build flags.
        source_root = source_root.resolve()
        targets = re.findall(r"^(\.build/[^ :]+):", (source_root / "Makefile").read_text(), re.M)
        consumers = []
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-native-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(source_root / directory, target_is_directory=True)
            for target in sorted(set(targets)):
                if target.endswith(".o"):
                    continue
                result = subprocess.run([MAKE, *graph_header_override(work), "-f", str(source_root / "Makefile"), "-n", target],
                                        cwd=work, capture_output=True, text=True, timeout=10, check=False)
                self.assertEqual(result.returncode, 0, result.stderr)
                commands = [shlex.split(line) for line in result.stdout.splitlines()
                            if " -o " + target + " " in line and not line.startswith("echo ")]
                commands = [fields for fields in commands if "-o" in fields
                            and fields[fields.index("-o") + 1] == target]
                self.assertEqual(len(commands), 1)
                pending = [work / w for w in commands[0] if w.endswith(".c")]
                seen = set()
                while pending:
                    path = pending.pop().resolve()
                    if path in seen:
                        continue
                    seen.add(path)
                    for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', path.read_text(), re.M):
                        found = next((p for p in (path.parent/name, work/'include'/name, work/'src'/name)
                                      if p.is_file()), None)
                        self.assertIsNotNone(found, name)
                        if found is not None:
                            pending.append(found)
                if any((source_root / name).resolve() in seen for name in needles):
                    consumers.append(target)
            self.assertFalse((work / ".build").exists(), "dry graph unexpectedly executed recipes")
        self.assertGreater(len(consumers), 3, "audit must include legacy Native consumers")
        return consumers

    def test_native_consumer_discovery_ignores_warm_source_tree(self):
        expected = self.native_consumers(ROOT)
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-native-warm-", dir=scratch) as temporary:
            work = Path(temporary)
            shutil.copyfile(ROOT / "Makefile", work / "Makefile")
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            targets = re.findall(r"^(\.build/[^ :]+):", (work / "Makefile").read_text(), re.M)
            # Synthetic mtime fixtures only, never compiled or executed binaries.
            for target in targets:
                artifact = work / target
                artifact.parent.mkdir(parents=True, exist_ok=True)
                artifact.write_bytes(b"")
                os.utime(artifact, (2_000_000_000, 2_000_000_000))
            control = subprocess.run([MAKE, *graph_header_override(work), "-n", ".build/account_cli_control_driver"], cwd=work,
                                     capture_output=True, text=True, timeout=10, check=False)
            self.assertEqual(control.returncode, 0, control.stderr)
            self.assertNotIn(" -o .build/account_cli_control_driver ", control.stdout)
            before = {target: (work / target).stat().st_mtime_ns for target in targets}
            self.assertEqual(self.native_consumers(work), expected)
            self.assertEqual({target: (work / target).stat().st_mtime_ns for target in targets}, before)

    def test_hot_header_all_actual_native_consumers(self):
        for target in self.native_consumers(ROOT):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def assert_registry_command(self, fields, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"],
                                 "registry requires the exact BSD multiline trailer")
                fields = fields[1:]
        self.assertEqual(fields, [".build/test_account_registry"],
                         "registry requires exact standalone argv without prefixes or arguments")

    def test_registry_exact_argv_and_bsd_wrappers(self):
        expected = [".build/test_account_registry"]
        self.assert_registry_command(expected)
        self.assert_registry_command(["{", *expected, ";", "}"])
        self.assert_registry_command(["{", *expected], ["}", "||", "exit", "$?"])
        for bad in ([*expected, "extra"], ["env", *expected], ["echo", *expected],
                    [".build/test_account_plan"], ["{", *expected, "}"],
                    ["{", *expected, ";", "}", ";"]):
            with self.subTest(bad=bad), self.assertRaises(AssertionError):
                self.assert_registry_command(bad)
        with self.assertRaises(AssertionError):
            self.assert_registry_command(["{", *expected], ["}", "||", "exit", "0"])

    def test_registry_rebuilds_for_recursive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/test_account_registry")

    def test_registry_all_three_entry_graphs(self):
        target = ".build/test_account_registry"
        expected_sources = ["tests/test_account_registry.c", "src/account_plan.c",
                            "src/account_naming.c", "src/naming.c", "src/topology.c",
                            "src/connect_topic.c", "src/command.c", "src/account_registry.c"]
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-registry-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-registry"):
                with self.subTest(entry=entry):
                    child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n", entry],
                        cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                    try:
                        stdout, stderr = child.communicate(timeout=10)
                    finally:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                    self.assertEqual(child.returncode, 0, stderr)
                    lines = [shlex.split(line) for line in stdout.splitlines()]
                    builds = [(i, fields) for i, fields in enumerate(lines)
                              if fields[:1] != ["echo"] and "-o" in fields
                              and fields[fields.index("-o") + 1] == target]
                    self.assertEqual(len(builds), 1, "registry must compile exactly once before execution")
                    linked = [field for field in builds[0][1] if field.endswith(".c")]
                    self.assertEqual(linked, expected_sources, "registry requires its distinct pure source/link closure")
                    self.assertNotIn("-lcurl", builds[0][1], "registry must not link curl runtime")
                    calls = [(i, fields) for i, fields in enumerate(lines)
                             if fields[:1] not in (["echo"], ["---"]) and "-o" not in fields and target in fields]
                    self.assertEqual(len(calls), 1, "registry must execute exactly once")
                    index, fields = calls[0]
                    self.assert_registry_command(fields,
                        lines[index + 1] if index + 1 < len(lines) else None)
                    self.assertLess(builds[0][0], index, "registry compilation must precede execution")
                    self.assertFalse((work / ".build").exists(), "dry graph executed a recipe")

    def assert_inventory_command(self, fields, mode, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"])
                fields = fields[1:]
        self.assertEqual(fields, [
            "GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver",
            "GIN_DISPATCH_CASE=" + mode, sys.executable, "-B",
            "tests/test_account_dispatch.py"], "inventory requires exact mode, role and unfiltered argv")

    def test_inventory_exact_argv_and_bsd_wrappers(self):
        for mode in INVENTORY_CASES:
            expected = ["GIN_ACCOUNT_DISPATCH_DRIVER=.build/account_dispatch_driver",
                        "GIN_DISPATCH_CASE=" + mode, sys.executable, "-B",
                        "tests/test_account_dispatch.py"]
            self.assert_inventory_command(expected, mode)
            self.assert_inventory_command(["{", *expected, ";", "}"], mode)
            self.assert_inventory_command(["{", *expected], mode, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "-k", "receive"], [*expected, "Dispatch.test_receive"],
                        ["GIN_ACCOUNT_DISPATCH_DRIVER=.build/wrong", *expected[1:]],
                        [*expected[:2], "python3", *expected[3:]],
                        [expected[0], "GIN_DISPATCH_CASE=positive", *expected[2:]]):
                with self.subTest(bad=bad), self.assertRaises(AssertionError):
                    self.assert_inventory_command(bad, mode)
            with self.assertRaises(AssertionError):
                self.assert_inventory_command(["{", *expected], mode, ["}", "||", "exit", "0"])

    def test_inventory_loader_one_method(self):
        import test_account_dispatch as dispatch
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(dispatch).countTestCases(), 1)
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(dispatch.Replan), ["test_receive"])

    def test_inventory_all_three_entry_graphs(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-inventory-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", "check-account-dispatch-inventory"):
                with self.subTest(entry=entry):
                    result = subprocess.run([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"),
                        "PYTHON=" + shlex.join([sys.executable, "-B"]), "-n", entry],
                        cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        capture_output=True, text=True, timeout=10, check=False)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    lines = [shlex.split(line) for line in result.stdout.splitlines()]
                    calls = [(i, fields) for i, fields in enumerate(lines)
                             if fields[:1] != ["echo"] and any(
                                 field.startswith("GIN_DISPATCH_CASE=inventory_") for field in fields)]
                    modes = [field.split("=", 1)[1] for _, fields in calls
                             for field in fields if field.startswith("GIN_DISPATCH_CASE=")]
                    self.assertCountEqual(modes, INVENTORY_CASES, "missing or duplicated inventory modes")
                    builds = [(i, fields) for i, fields in enumerate(lines)
                              if fields[:1] != ["echo"] and "-o" in fields and
                              fields[fields.index("-o") + 1] == ".build/account_dispatch_driver"]
                    self.assertEqual(len(builds), 1)
                    for index, fields in calls:
                        mode = next(word.split("=", 1)[1] for word in fields
                                    if word.startswith("GIN_DISPATCH_CASE="))
                        self.assert_inventory_command(fields, mode,
                            lines[index + 1] if index + 1 < len(lines) else None)
                        self.assertLess(builds[0][0], index)
                    if entry == "check-account-dispatch-inventory":
                        suite_calls = [fields for fields in lines if fields[:1] != ["echo"]
                                       and "tests/test_account_dispatch.py" in fields]
                        self.assertEqual(len(suite_calls), 8)
                    self.assertFalse((work / ".build").exists())

    def assert_fifo_command(self, fields, observer, next_fields=None,
                            suite="test_account_fifo_send.py"):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"])
                fields = fields[1:]
        role = ".build/gin_account_fifo_observer" if observer else ".build/gin"
        expected = (["GIN_FIFO_OBSERVER=1"] if observer else [])
        expected += ["GIN_ACCOUNT_CLI_BINARY=${PWD}/" + role, sys.executable, "-B",
                     "-m", "unittest", "discover", "-s", "tests", "-p",
                     suite, "-v"]
        self.assertEqual(fields, expected, "FIFO requires exact full-module invocation and binary role")

    def test_fifo_exact_argv_and_bsd_wrappers(self):
        for observer in (False, True):
            role = ".build/gin_account_fifo_observer" if observer else ".build/gin"
            expected = (["GIN_FIFO_OBSERVER=1"] if observer else []) + [
                "GIN_ACCOUNT_CLI_BINARY=${PWD}/" + role, sys.executable, "-B", "-m",
                "unittest", "discover", "-s", "tests", "-p", "test_account_fifo_send.py", "-v"]
            self.assert_fifo_command(expected, observer)
            self.assert_fifo_command(["{", *expected, ";", "}"], observer)
            self.assert_fifo_command(["{", *expected], observer, ["}", "||", "exit", "$?"])
            for bad in ([*expected, "-k", "channel"], [*expected, "FIFO.test_channel"],
                        ["GIN_FIFO_CASE=channel", *expected], ["env", *expected],
                        ["echo", *expected], ["GIN_FIFO_OBSERVER=0", *expected],
                        [word.replace(role, ".build/wrong") for word in expected],
                        [word.replace(sys.executable, "python3") for word in expected],
                        ["{", *expected, "}"], ["{", *expected, ";", "}", ";"]):
                with self.subTest(bad=bad), self.assertRaises(AssertionError):
                    self.assert_fifo_command(bad, observer)
            with self.assertRaises(AssertionError):
                self.assert_fifo_command(["{", *expected], observer, ["}", "||", "exit", "0"])

    def test_fifo_loader_exact_eight_methods(self):
        import test_account_fifo_send as fifo
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(fifo.FIFO), [
            "test_cancelled_post_close_guard", "test_channel", "test_held_stop",
            "test_query", "test_query_held_stop",
            "test_setup_failure_ownership", "test_transcript_flood_is_bounded_and_drained",
            "test_transcript_late_output"])
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(fifo).countTestCases(), 8)

    def test_fifo_observer_rebuilds_for_recursive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/gin_account_fifo_observer")

    def test_fifo_both_roles_once_in_all_entry_graphs(self):
        self.assert_fifo_entry_graphs("test_account_fifo_send.py", "check-account-fifo-send")

    def test_network_fifo_both_roles_once_in_all_entry_graphs(self):
        self.assert_fifo_entry_graphs("test_account_network_fifo.py", "check-account-network-fifo")

    def test_network_fifo_loader_exact_six_methods(self):
        import test_account_network_fifo as network
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(network.NetworkFIFO),
                         ["test_network_fifo_join", "test_network_fifo_part", "test_network_fifo_query_open"])
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(
            network.NetworkFIFOCharacterization), ["test_network_fifo_held_stop", "test_network_fifo_part_held_stop", "test_network_fifo_query_held_stop"])
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(network).countTestCases(), 6)

    def test_network_fifo_exact_argv_and_bsd_wrappers(self):
        suite = "test_account_network_fifo.py"
        for observer in (False, True):
            role = ".build/gin_account_fifo_observer" if observer else ".build/gin"
            expected = (["GIN_FIFO_OBSERVER=1"] if observer else []) + [
                "GIN_ACCOUNT_CLI_BINARY=${PWD}/" + role, sys.executable, "-B", "-m",
                "unittest", "discover", "-s", "tests", "-p", suite, "-v"]
            self.assert_fifo_command(expected, observer, suite=suite)
            self.assert_fifo_command(["{", *expected, ";", "}"], observer, suite=suite)
            self.assert_fifo_command(["{", *expected], observer,
                                     ["}", "||", "exit", "$?"], suite=suite)
            for bad in ([*expected, "-k", "join"], [*expected, "NetworkFIFO.test_network_fifo_join"],
                        ["GIN_FIFO_CASE=join", *expected], ["env", *expected],
                        ["echo", *expected], ["GIN_FIFO_OBSERVER=0", *expected],
                        [word.replace(role, ".build/wrong") for word in expected],
                        [word.replace(sys.executable, "python3") for word in expected],
                        [word.replace(suite, "test_account_fifo_send.py") for word in expected],
                        ["{", *expected, "}"], ["{", *expected, ";", "}", ";"]):
                with self.subTest(bad=bad), self.assertRaises(AssertionError):
                    self.assert_fifo_command(bad, observer, suite=suite)
            with self.assertRaises(AssertionError):
                self.assert_fifo_command(["{", *expected], observer,
                                         ["}", "||", "exit", "0"], suite=suite)

    def assert_fifo_entry_graphs(self, suite, dedicated_target):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-fifo-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for entry in ("check", "check-account-components", dedicated_target):
                with self.subTest(entry=entry):
                    child = subprocess.Popen([MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"),
                        "PYTHON=" + shlex.join([sys.executable, "-B"]), "-n", entry],
                        cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, start_new_session=True)
                    try:
                        stdout, stderr = child.communicate(timeout=10)
                    finally:
                        try:
                            os.killpg(child.pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                        child.wait()
                    self.assertEqual(child.returncode, 0, stderr)
                    lines = [shlex.split(line) for line in stdout.splitlines()]
                    calls = [(i, words) for i, words in enumerate(lines)
                             if suite in words and words[:1] != ["echo"]]
                    self.assertEqual(len(calls), 2, "missing or duplicated full FIFO roles")
                    for observer, (index, words) in zip((False, True), calls):
                        self.assert_fifo_command(words, observer,
                            lines[index + 1] if index + 1 < len(lines) else None, suite=suite)
                        role = ".build/gin_account_fifo_observer" if observer else ".build/gin"
                        builds = [(i, fields) for i, fields in enumerate(lines)
                                  if fields[:1] != ["echo"] and "-o" in fields
                                  and fields[fields.index("-o") + 1] == role]
                        self.assertEqual(len(builds), 1)
                        self.assertLess(builds[0][0], index)
                        linked = [word for word in builds[0][1] if word.endswith(".c")]
                        self.assertEqual(len(linked), len(set(linked)))
                        for source in ("src/gin.c", "src/account_cli.c"):
                            self.assertIn(source, linked)
                        if observer:
                            self.assertIn("tests/account_fifo_send_observer.c", linked)
                            self.assertNotIn("src/account_owner.c", linked)
                        else:
                            self.assertIn("src/account_owner.c", linked)
                    self.assertFalse((work / ".build").exists())

    def assert_cli_command(self, fields, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"],
                                 "unknown multiline BSD job-shell trailer")
                fields = fields[1:]
        self.assertEqual(fields, [sys.executable, "-B", "tests/test_account_cli.py"],
                         "CLI suite requires exact unfiltered default production/private roles")

    def test_cli_command_exact_argv(self):
        expected = [sys.executable, "-B", "tests/test_account_cli.py"]
        self.assert_cli_command(expected)
        self.assert_cli_command(["{", *expected, ";", "}"])
        self.assert_cli_command(["{", *expected], ["}", "||", "exit", "$?"])
        for fields in ([*expected, "CLI.test_receive"], [*expected, "-k", "receive"],
                       ["GIN_ACCOUNT_CLI_BINARY=.build/other", *expected],
                       ["GIN_ACCOUNT_CLI_CONTROL_DRIVER=.build/other", *expected],
                       ["echo", *expected], ["env", *expected],
                       ["python3", *expected[1:]], [expected[0], expected[-1]],
                       ["{", *expected, "}"], ["{", *expected, ";", "}", ";"]):
            with self.subTest(fields=fields), self.assertRaises(AssertionError):
                self.assert_cli_command(fields)
        with self.assertRaises(AssertionError):
            self.assert_cli_command(["{", *expected], ["}", "||", "exit", "0"])

    def test_cli_suite_loader_exact_six_methods(self):
        import test_account_cli as cli
        names = ["test_control_errors", "test_default_root", "test_full_stderr",
                 "test_invalid_grammar", "test_prompt_cancellation", "test_receive"]
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(cli.CLI), names)
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(cli).countTestCases(), 6)

    def test_cli_roles_rebuild_for_transitive_project_inputs(self):
        for target in (".build/gin", ".build/account_cli_control_driver"):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def assert_bootstrap_null_command(self, fields, target, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"],
                                 "unknown multiline BSD job-shell trailer")
                fields = fields[1:]
        self.assertEqual(fields, [target, "--null-input"])

    def test_bootstrap_null_command_exact_shell_grammar(self):
        target = ".build/account_history_bootstrap_driver"
        self.assert_bootstrap_null_command([target, "--null-input"], target)
        self.assert_bootstrap_null_command(["{", target, "--null-input", ";", "}"], target)
        self.assert_bootstrap_null_command(["{", target, "--null-input"], target,
                                           ["}", "||", "exit", "$?"])
        with self.assertRaises(AssertionError):
            self.assert_bootstrap_null_command(["{", target, "--null-input"], target,
                                               ["}", "||", "exit", "0"])
        for fields in (
            [target, "--null-input", "extra"],
            ["echo", target, "--null-input"],
            ["{", target, "--null-input", "}"],
            ["{", target, "--null-input", ";", "}", ";"],
            ["{", "{", target, "--null-input", ";", "}", ";", "}"],
        ):
            with self.subTest(fields=fields), self.assertRaises(AssertionError):
                self.assert_bootstrap_null_command(fields, target)

    def test_bootstrap_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/account_history_bootstrap_driver")

    def test_owner_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/account_owner_driver")

    def assert_owner_command(self, fields, next_fields=None):
        if fields[:1] == ["{"]:
            if fields[-2:] == [";", "}"]:
                fields = fields[1:-2]
            else:
                self.assertEqual(next_fields, ["}", "||", "exit", "$?"],
                                 "unknown multiline BSD job-shell trailer")
                fields = fields[1:]
        self.assertEqual(
            fields,
            ["GIN_ACCOUNT_OWNER_DRIVER=.build/account_owner_driver",
             sys.executable, "-B", "tests/test_account_owner.py"],
            "owner suite requires the exact unfiltered command",
        )

    def test_owner_command_exact_interpreter_and_shell_grammar(self):
        expected = ["GIN_ACCOUNT_OWNER_DRIVER=.build/account_owner_driver",
                    sys.executable, "-B", "tests/test_account_owner.py"]
        self.assert_owner_command(expected)
        self.assert_owner_command(["{", *expected, ";", "}"])
        self.assert_owner_command(["{", *expected], ["}", "||", "exit", "$?"])
        for fields in (
            [*expected, "Owner.test_receive"],
            [*expected, "-k", "receive"],
            ["GIN_OWNER_CASE=positive", *expected],
            ["env", *expected],
            ["echo", *expected],
            [expected[0], "python3", expected[-1]],
            [expected[0], sys.executable, expected[-1]],
            ["GIN_ACCOUNT_OWNER_DRIVER=.build/other_driver", *expected[1:]],
            ["{", *expected, "}"],
            ["{", *expected, ";", "}", ";"],
            ["{", "{", *expected, ";", "}", ";", "}"],
        ):
            with self.subTest(fields=fields), self.assertRaises(AssertionError):
                self.assert_owner_command(fields)
        with self.assertRaises(AssertionError):
            self.assert_owner_command(["{", *expected], ["}", "||", "exit", "0"])

    def test_owner_suite_discovers_one_unfiltered_boundary(self):
        import test_account_owner as owner
        self.assertEqual(unittest.defaultTestLoader.getTestCaseNames(owner.Owner), ["test_receive"])
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(owner).countTestCases(), 1)

    def test_bootstrap_suite_discovers_all_three_boundaries(self):
        import test_account_history_bootstrap as bootstrap
        self.assertEqual(
            [(case.__name__, unittest.defaultTestLoader.getTestCaseNames(case))
             for case in (bootstrap.Bootstrap, bootstrap.NullOrigin, bootstrap.StopAppend)],
            [("Bootstrap", ["test_receive"]), ("NullOrigin", ["test_receive"]),
             ("StopAppend", ["test_receive"])],
        )
        self.assertEqual(unittest.defaultTestLoader.loadTestsFromModule(bootstrap).countTestCases(), 3)

    def test_receive_control_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/test_account_receive_control")

    def test_history_output_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/account_history_output_driver")

    def test_history_output_suite_discovers_all_storage_boundaries(self):
        import test_account_history_output
        self.assertEqual(
            unittest.defaultTestLoader.getTestCaseNames(test_account_history_output.HistoryOutput),
            ["test_partial", "test_receive", "test_replace"],
        )

    def test_output_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/account_output_driver")

    def test_output_suite_discovers_all_storage_boundaries(self):
        import test_account_output
        self.assertEqual(
            unittest.defaultTestLoader.getTestCaseNames(test_account_output.Output),
            ["test_partial", "test_receive", "test_replace"],
        )

    def test_transport_roles_rebuild_for_transitive_project_inputs(self):
        for _, target, _, _, _ in TRANSPORT_SCENARIOS:
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def test_transport_scenarios_in_both_check_graphs(self):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-transport-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            for check_target in ("check", "check-account-components"):
                result = subprocess.run(
                    [MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n", check_target],
                    cwd=work, env=dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1"),
                    capture_output=True, text=True, timeout=10, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                lines = [shlex.split(line) for line in result.stdout.splitlines()]
                for variable, target, selector, suite, cases in TRANSPORT_SCENARIOS:
                    with self.subTest(check_target=check_target, target=target):
                        builds = [i for i, fields in enumerate(lines) if "-o" in fields
                                  and fields[fields.index("-o") + 1] == target]
                        self.assertEqual(len(builds), 1, "missing transport compiler role: " + target)
                        linked = [field for field in lines[builds[0]] if field.endswith(".c")]
                        self.assertEqual(len(linked), len(set(linked)), "duplicate C link inputs")
                        if target.endswith("fidelity"):
                            self.assertNotIn("src/account_dispatch.c", linked)
                            self.assertNotIn("tests/account_dispatch_driver.c", linked)
                        executions = [(i, fields) for i, fields in enumerate(lines)
                                      if variable + "=" + target in fields and suite in fields]
                        actual = [field.split("=", 1)[1] for _, fields in executions
                                  for field in fields if field.startswith(selector + "=")]
                        expected_cases = (*cases, *INVENTORY_CASES) if target == ".build/account_dispatch_driver" else cases
                        self.assertCountEqual(actual, expected_cases, "missing or duplicated explicit selectors")
                        self.assertTrue(all(i > builds[0] for i, _ in executions))
                self.assertFalse((work / ".build").exists(), "dry graph executed a recipe")

    def test_status_projection_inputs(self):
        for target in (".build/test_status_projection", ".build/test_status_projection_fidelity"):
            self.assert_project_inputs_rebuild(target)

    def test_status_render_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/test_status_render")

    def test_replan_roles_rebuild_for_transitive_project_inputs(self):
        for target in (".build/test_connect_replan", ".build/connect_replan_wss_driver"):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def test_discovery_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/account_discovery_driver")

    def test_sinks_rebuild_for_transitive_project_inputs(self):
        for target in (".build/test_account_sinks", ".build/test_account_sinks_fault"):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def test_plan_and_queue_rebuild_for_transitive_project_inputs(self):
        for target in (".build/test_account_plan", ".build/test_account_plan_fault",
                       ".build/test_account_live_queue"):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def test_account_checkpoint_rebuilds_for_transitive_project_headers(self):
        self.assert_project_inputs_rebuild(".build/test_account_checkpoint")

    def test_account_network_checkpoint_rebuilds_for_transitive_project_inputs(self):
        self.assert_project_inputs_rebuild(".build/test_account_network_checkpoint")

    def test_login_roles_rebuild_for_transitive_project_inputs(self):
        for target in (".build/test_account_root", ".build/test_account_root_fault",
                       ".build/auth_login_control_driver", ".build/interactive_login_driver"):
            with self.subTest(target=target):
                self.assert_project_inputs_rebuild(target)

    def assert_project_inputs_rebuild(self, target):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-header-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            # Copy inputs: synthetic timestamp changes must never touch the checkout.
            for directory in ("src", "include", "tests"):
                for source in (ROOT / directory).iterdir():
                    if source.suffix in (".c", ".h"):
                        destination = work / directory / source.name
                        destination.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copyfile(source, destination)
                        os.utime(destination, (1_000_000_000, 1_000_000_000))

            # Isolate external header mtimes too; these are dependency sentinels,
            # never compiler inputs or executable instrumentation substitutes.
            extra_headers = []
            if target in (".build/account_dispatch_hot_wss_valgrind_driver",
                          ".build/test_account_output_hot_status_vg"):
                graph_header_override(work)
                extra_headers = [work / "graph-sdk" / name
                                 for name in ("memcheck.h", "valgrind.h")]

            def graph():
                child = subprocess.Popen(
                    [MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "-n", target],
                    cwd=work, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    text=True, start_new_session=True,
                )
                try:
                    stdout, stderr = child.communicate(timeout=10)
                finally:
                    try:
                        os.killpg(child.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    child.wait()
                self.assertEqual(child.returncode, 0, stderr)
                # BSD make's up-to-date prose is not shell syntax (unpaired quote).
                commands = [shlex.split(line) for line in stdout.splitlines()
                            if " -o " + target in line]
                return [fields for fields in commands if "-o" in fields and
                        fields[fields.index("-o") + 1] == target]

            fresh = graph()
            self.assertEqual(len(fresh), 1)
            pending = [work / field for field in fresh[0] if field.endswith(".c")]
            self.assertTrue(pending)
            visited = set()
            while pending:
                source = pending.pop().resolve()
                if source in visited:
                    continue
                self.assertTrue(source.is_relative_to(work), str(source))
                visited.add(source)
                for name in re.findall(r'^\s*#\s*include\s*"([^"]+)"', source.read_text(), re.M):
                    choices = (source.parent / name, work / "include" / name, work / "src" / name)
                    found = next((path for path in choices if path.is_file()), None)
                    self.assertIsNotNone(found, "unresolved quoted include: " + name)
                    if found is not None:
                        pending.append(found)
            dependencies = sorted(visited) + extra_headers
            self.assertTrue(any(path.suffix == ".h" for path in dependencies))
            # This empty artifact is only an mtime fixture, never an executable.
            # Multi-stage legacy links also need synthetic object prerequisites.
            # These are timestamp-only artifacts and are never executed.
            for field in fresh[0]:
                if field.endswith(".o") and field.startswith(".build/"):
                    object_artifact = work / field
                    object_artifact.parent.mkdir(parents=True, exist_ok=True)
                    object_artifact.write_bytes(b"")
                    os.utime(object_artifact, (1_000_000_005, 1_000_000_005))
            artifact = work / target
            artifact.parent.mkdir(parents=True, exist_ok=True)
            artifact.write_bytes(b"")
            os.utime(artifact, (1_000_000_010, 1_000_000_010))
            self.assertFalse(graph(), "timestamp control must be up to date")
            for dependency in dependencies:
                with self.subTest(dependency=str(dependency.relative_to(work))):
                    os.utime(dependency, (1_000_000_020, 1_000_000_020))
                    try:
                        self.assertEqual(len(graph()), 1,
                            "project-input change must rebuild " + str(dependency.relative_to(work)))
                    finally:
                        os.utime(dependency, (1_000_000_000, 1_000_000_000))

    def test_authority_units_are_built_before_check_executes_them(self):
        self.assert_check_graph("check")

    def test_account_components_are_built_before_focused_check_executes_them(self):
        self.assert_check_graph("check-account-components")

    def assert_check_graph(self, check_target):
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(prefix="gin-make-graph-", dir=scratch) as temporary:
            work = Path(temporary)
            # Only the graph is inspected; no application uses these symlinks.
            for directory in ("src", "include", "tests"):
                (work / directory).symlink_to(ROOT / directory, target_is_directory=True)
            env = dict(os.environ, GIN_MAKE_GRAPH_INSPECTION="1")
            # Control the dry-run interpreter independently of inherited Make
            # overrides; the exact-argv oracle must reject hardcoded recipes.
            graph_python = shlex.join([sys.executable, "-B"])
            child = subprocess.Popen(
                [MAKE, *graph_header_override(work), "-f", str(ROOT / "Makefile"), "PYTHON=" + graph_python,
                 "-n", check_target],
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
                if check_target == "check-account-components" and target.startswith(".build/test_authority_json_"):
                    continue
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
            for variable, target, suite in PRIVATE_DRIVER_TARGETS:
                with self.subTest(target=target):
                    builds = []
                    executions = []
                    for index, line in enumerate(lines):
                        fields = shlex.split(line)
                        if fields[:1] == ["{"]:
                            fields = fields[1:]
                        if "-o" in fields and fields[fields.index("-o") + 1] == target:
                            builds.append(index)
                        if variable + "=" + target in fields and suite in fields:
                            executions.append(index)
                    self.assertTrue(executions, "check no longer exercises " + target)
                    self.assertEqual(len(builds), 1, "check must build exactly one " + target)
                    if target in (".build/account_output_driver", ".build/account_history_output_driver",
                                  ".build/account_history_bootstrap_driver", ".build/account_owner_driver"):
                        self.assertEqual(len(executions), 1, "suite must execute exactly once")
                        invocation = shlex.split(lines[executions[0]])
                        tail = invocation[invocation.index(suite) + 1:]
                        self.assertTrue(all(word in (";", "}") for word in tail),
                                        "storage suite must not filter test methods")
                    if target == ".build/account_owner_driver":
                        linked = [word for word in shlex.split(lines[builds[0]])
                                  if word.endswith(".c")]
                        self.assertEqual(len(linked), len(set(linked)), "duplicate owner link inputs")
                        for source in ("tests/account_owner_driver.c", "src/account_owner.c",
                                       "src/account_history_bootstrap.c"):
                            self.assertIn(source, linked)
                        invocation = shlex.split(lines[executions[0]])
                        next_fields = (shlex.split(lines[executions[0] + 1])
                                       if executions[0] + 1 < len(lines) else None)
                        self.assert_owner_command(invocation, next_fields)
                        suite_calls = [line for line in lines
                                       if suite in shlex.split(line)
                                       and shlex.split(line)[:1] != ["echo"]]
                        self.assertEqual(len(suite_calls), 1,
                                         "owner suite must not execute with another driver")
                    if target == ".build/account_history_bootstrap_driver":
                        linked = [word for word in shlex.split(lines[builds[0]])
                                  if word.endswith(".c")]
                        self.assertEqual(len(linked), len(set(linked)), "duplicate bootstrap link inputs")
                        self.assertNotIn("src/account_history_bootstrap.c", linked,
                                         "driver already includes the production coordinator")
                        modes = [(index, shlex.split(line)) for index, line in enumerate(lines)
                                 if target in shlex.split(line) and "--null-input" in shlex.split(line)]
                        self.assertEqual(len(modes), 1, "NULL-input regression must execute exactly once")
                        self.assertGreater(modes[0][0], builds[0])
                        fields = modes[0][1]
                        next_fields = (shlex.split(lines[modes[0][0] + 1])
                                       if modes[0][0] + 1 < len(lines) else None)
                        self.assert_bootstrap_null_command(fields, target, next_fields)
                    self.assertLess(builds[0], executions[0],
                        "private driver compilation must precede its suite")
            cli_calls = [i for i, line in enumerate(lines)
                         if "tests/test_account_cli.py" in shlex.split(line)
                         and shlex.split(line)[:1] != ["echo"]]
            self.assertEqual(len(cli_calls), 1, "CLI suite must execute exactly once unfiltered")
            call = cli_calls[0]
            self.assert_cli_command(shlex.split(lines[call]),
                                    shlex.split(lines[call + 1]) if call + 1 < len(lines) else None)
            for role in (".build/gin", ".build/account_cli_control_driver"):
                builds = [(i, shlex.split(line)) for i, line in enumerate(lines)
                          if " -o " + role + " " in line
                          and shlex.split(line)[:1] != ["echo"]]
                self.assertEqual(len(builds), 1, "missing CLI compiler role: " + role)
                self.assertLess(builds[0][0], call)
                linked = [word for word in builds[0][1] if word.endswith(".c")]
                self.assertEqual(len(linked), len(set(linked)), "duplicate CLI link inputs")
                if role.endswith("control_driver"):
                    self.assertIn("tests/account_cli_control_driver.c", linked)
                    self.assertNotIn("src/account_cli.c", linked,
                                     "private driver includes the production implementation")
                    self.assertNotIn("src/interactive_login.c", linked,
                                     "private driver substitutes interactive login")
            self.assertFalse((work / ".build").exists(), "dry graph unexpectedly executed recipes")


class ReceiveHotInstrumentation(unittest.TestCase):
    def test_receive_hot_prefix_command(self):
        import importlib.util
        from unittest.mock import patch
        testdir = str(ROOT / "tests")
        with patch.object(sys, "path", [testdir] + sys.path):
            spec = importlib.util.spec_from_file_location(
                "receive_hot_prefix_guard", ROOT / "tests/test_account_receive_hot_wss.py")
            self.assertIsNotNone(spec)
            assert spec is not None and spec.loader is not None
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        command = getattr(module, "driver_command", None)
        self.assertTrue(callable(command), "hot WSS fixture must expose prefix-aware driver_command")
        assert callable(command)
        for prefix, expected in (("", []), ("   ", []),
                ("valgrind --error-exitcode=99", ["valgrind", "--error-exitcode=99"]),
                ("'/private/wrapper path' --tag", ["/private/wrapper path", "--tag"])):
            with self.subTest(prefix=prefix), patch.dict(os.environ,
                    {"GIN_ACCOUNT_HOT_DRIVER": "/private/hot-driver", "GIN_ACCOUNT_HOT_PREFIX": prefix}):
                self.assertEqual(command("https://127.0.0.1:1", "/private/ca.pem", "positive"),
                    expected + ["/private/hot-driver", "https://127.0.0.1:1", "/private/ca.pem", "positive"]
                    + (["--instrumented-setup"] if expected else []))
        with patch.dict(os.environ, {"GIN_ACCOUNT_HOT_DRIVER": "/private/hot-driver"}, clear=True):
            self.assertEqual(command("https://127.0.0.1:1", "/private/ca.pem", "recovery"),
                ["/private/hot-driver", "https://127.0.0.1:1", "/private/ca.pem", "recovery"])

    def test_receive_hot_real_wrapper_setup_policy(self):
        import json
        import importlib.util
        from unittest.mock import patch
        testdir = str(ROOT / "tests")
        with patch.object(sys, "path", [testdir] + sys.path):
            spec = importlib.util.spec_from_file_location(
                "receive_hot_policy_guard", ROOT / "tests/test_account_receive_hot_wss.py")
            assert spec is not None and spec.loader is not None
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        scratch = graph_scratch()
        with tempfile.TemporaryDirectory(dir=scratch) as tmp:
            root = Path(tmp)
            wrapper = root / "recording wrapper"
            receipt = root / "calls.jsonl"
            wrapper.write_text("#!/usr/bin/env python3\nimport json, os, sys\n"
                "with open(sys.argv[1], 'a') as stream: stream.write(json.dumps(sys.argv[2:]) + '\\n')\n"
                "os.execv(sys.argv[2], sys.argv[2:])\n")
            wrapper.chmod(0o700)
            driver = root / "driver"
            driver.write_text("#!/usr/bin/env python3\nimport sys\n"
                "assert sys.argv[-1] == '--instrumented-setup', sys.argv\n")
            driver.chmod(0o700)
            for mode in ("positive", "recovery"):
                with self.subTest(mode=mode), patch.dict(os.environ, {"GIN_ACCOUNT_HOT_DRIVER": str(driver),
                        "GIN_ACCOUNT_HOT_PREFIX": shlex.join([str(wrapper), str(receipt)])}):
                    result = subprocess.run(module.driver_command("https://127.0.0.1:1", "ca.pem", mode),
                        capture_output=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)
            calls = [json.loads(line) for line in receipt.read_text().splitlines()]
            self.assertEqual(calls, [[str(driver), "https://127.0.0.1:1", "ca.pem", mode,
                "--instrumented-setup"] for mode in ("positive", "recovery")])

    def test_receive_hot_explicit_valgrind_registration(self):
        source = (ROOT / "scripts/check.sh").read_text()
        self.assertIn("GIN_ACCOUNT_HOT_PREFIX=", source,
            "canonical checks must instrument the actual owned hot WSS driver")
        self.assertIn("test_account_receive_hot", source,
            "canonical checks must register the no-argument hot receiver unit")


if __name__ == "__main__":
    unittest.main(verbosity=2)
