"""Black-box tests; Python is a development dependency only."""
import os
from pathlib import Path
import subprocess
import stat
import tempfile
import unittest

BIN = Path(os.environ.get("GIN_BIN", ".build/gin")).resolve()


class GinTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("TMPDIR", ".build/test-scratch"))
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="gin-test-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name).resolve()
        self.root = self.base / "root"

    def init(self, root=None, network="azzurra", target="#grappa", ok=True):
        return self.run_gin("init", "-d", self.root if root is None else root,
                            "-n", network, "-c", target, ok=ok)

    def test_target_namespaces_and_ascii_casing(self):
        self.init(target="#GrApPa")
        self.init(target="GrApPa")
        self.init(target="#@grappa")
        self.init(target="#cafÉ")
        self.init(target="#café")
        self.init(target="#foo[1]")
        self.init(target="#foo{1}")
        self.assertEqual(sorted(p.name for p in (self.root / "azzurra").iterdir()),
                         sorted(["grappa", "@grappa", "%40grappa", "caf%C3%89",
                                 "caf%C3%A9", "foo%5B1%5D", "foo%7B1%7D"]))

    def test_unsafe_names_are_rejected_before_mutation(self):
        for field, values in (("network", ["", ".", "..", "../escape", "a/b", "a b", "a\nb", "a.b", "x" * 256]),
                              ("target", ["", "#", ".", "..", "#.", "#..", "../escape", "#a/b", "a b", "a\nb", "a\rb", "a\tb", "a\x7fb", "#" + "x" * 256])):
            for value in values:
                with self.subTest(field=field, value=value):
                    self.init(**{field: value}, ok=False)
                    self.assertFalse(self.root.exists())

    def test_root_symlink_ancestors_are_rejected(self):
        real = self.base / "real"
        real.mkdir(mode=0o700)
        link = self.base / "link"
        link.symlink_to(real, target_is_directory=True)
        self.init(root=link / "new-root", ok=False)
        self.assertEqual(list(real.iterdir()), [])

    def test_root_parent_traversal_is_rejected(self):
        anchor = self.base / "anchor"
        anchor.mkdir(mode=0o700)
        for root in (str(anchor) + "/../escape", str(anchor) + "/./../escape", ""):
            with self.subTest(root=root):
                self.init(root=root, ok=False)
        self.assertFalse((self.base / "escape").exists())

    def test_preexisting_directory_permissions_are_rejected_not_repaired(self):
        self.init()
        for path in (self.root, self.root / "azzurra", self.root / "azzurra/grappa"):
            for mode in (0o755, 0o770, 0o1700, 0o500, 0o000):
                with self.subTest(path=path.name, mode=oct(mode)):
                    path.chmod(mode)
                    try:
                        self.init(ok=False)
                        self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)
                    finally:
                        path.chmod(0o700)

    def test_preexisting_endpoints_must_have_safe_types_and_permissions(self):
        for name in ("in", "out"):
            for kind in ("regular", "fifo", "directory", "symlink", "hardlink"):
                with self.subTest(name=name, kind=kind):
                    self.root = self.base / (name + "-" + kind)
                    self.init()
                    endpoint = self.root / "azzurra/grappa" / name
                    endpoint.unlink()
                    victim = self.base / (name + "-victim-" + kind)
                    victim.write_bytes(b"do not touch\n")
                    victim.chmod(0o600)
                    if kind == "regular":
                        endpoint.write_bytes(b"unsafe endpoint\n")
                        endpoint.chmod(0o644 if name == "out" else 0o600)
                    elif kind == "fifo":
                        os.mkfifo(endpoint, 0o600)
                        endpoint.chmod(0o644 if name == "in" else 0o600)
                    elif kind == "directory":
                        endpoint.mkdir(mode=0o700)
                    elif kind == "symlink":
                        endpoint.symlink_to(victim)
                    else:
                        os.link(victim, endpoint)
                    self.init(ok=False)
                    self.assertEqual(victim.read_bytes(), b"do not touch\n")

    @unittest.skipUnless(os.geteuid() == 0, "Changing owners requires root; run this test with sudo")
    def test_foreign_owners_are_rejected(self):
        self.init()
        for path in (self.root, self.root / "azzurra", self.root / "azzurra/grappa",
                     self.root / "azzurra/grappa/in", self.root / "azzurra/grappa/out"):
            with self.subTest(path=str(path.relative_to(self.base))):
                os.chown(path, 65534, -1)
                try:
                    self.init(ok=False)
                finally:
                    os.chown(path, os.geteuid(), -1)

    def test_invalid_cli_is_usage_error_without_side_effects(self):
        commands = [(), ("check",), ("run",), ("--help", "extra"),
                    ("--version", "extra"), ("init",),
                    ("init", "-d", self.root, "-n", "azzurra"),
                    ("init", "-z"),
                    ("init", "-d", self.root, "-n", "azzurra", "-c", "#grappa", "extra")]
        valid = ("init", "-d", self.root, "-n", "azzurra", "-c", "#grappa")
        commands.extend(valid + (flag, value) for flag, value in
                        (("-d", self.root), ("-n", "azzurra"), ("-c", "#grappa")))
        for command in commands:
            with self.subTest(command=command):
                result = self.run_gin(*command, ok=False)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(result.stdout)
                self.assertFalse(self.root.exists())

    def test_reinitialization_preserves_existing_log_and_fifo_inode(self):
        self.init()
        window = self.root / "azzurra/grappa"
        log = window / "out"
        log.write_bytes(b"existing history\x00\n")
        before = {name: (window / name).stat().st_ino for name in ("in", "out")}
        self.init()
        self.assertEqual(log.read_bytes(), b"existing history\x00\n")
        self.assertEqual({name: (window / name).stat().st_ino for name in before}, before)

    def test_symlink_directories_and_wrong_directory_types_are_rejected(self):
        for depth in range(3):
            for kind in ("symlink", "file", "fifo"):
                with self.subTest(depth=depth, kind=kind):
                    self.root = self.base / (str(depth) + "-" + kind)
                    path = self.root
                    for component in ("azzurra", "grappa")[:depth]:
                        path.mkdir(mode=0o700)
                        path /= component
                    victim = self.base / ("victim-" + str(depth) + "-" + kind)
                    victim.mkdir(mode=0o700)
                    if kind == "symlink":
                        path.symlink_to(victim, target_is_directory=True)
                    elif kind == "file":
                        path.write_bytes(b"keep me\n")
                        path.chmod(0o600)
                    else:
                        os.mkfifo(path, 0o600)
                    self.init(ok=False)
                    self.assertEqual(list(victim.iterdir()), [])
                    if kind == "file":
                        self.assertEqual(path.read_bytes(), b"keep me\n")

    def test_endpoint_special_mode_bits_and_missing_owner_access_are_rejected(self):
        self.init()
        for name in ("in", "out"):
            path = self.root / "azzurra/grappa" / name
            for mode in (0o000, 0o400, 0o200, 0o620, 0o660, 0o1600, 0o4600):
                with self.subTest(name=name, mode=oct(mode)):
                    path.chmod(mode)
                    try:
                        self.init(ok=False)
                        self.assertEqual(stat.S_IMODE(path.stat().st_mode), mode)
                    finally:
                        path.chmod(0o600)

    def test_creation_modes_do_not_depend_on_umask(self):
        for mask in (0o000, 0o777):
            self.root = self.base / str(mask)
            old = os.umask(mask)
            try:
                self.init()
            finally:
                os.umask(old)
            for path in (self.root, self.root / "azzurra", self.root / "azzurra/grappa"):
                self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)
            for name in ("in", "out"):
                self.assertEqual(stat.S_IMODE((self.root / "azzurra/grappa" / name).stat().st_mode), 0o600)

    def test_relative_root_and_reordered_options(self):
        self.run_gin("init", "-c", "#grappa", "-n", "Azzurra", "-d", "./root//")
        self.assertTrue((self.root / "Azzurra/grappa/out").is_file())

    def test_missing_root_parent_is_not_recursively_created(self):
        self.init(root=self.base / "missing/child", ok=False)
        self.assertFalse((self.base / "missing").exists())

    def test_mapping_escapes_percent_without_namespace_collisions(self):
        for target in ("#grappa", "grappa", "#@grappa", "#%40grappa", "#..x", "#a.b", "a.b"):
            self.init(target=target)
        self.assertEqual(sorted(p.name for p in (self.root / "azzurra").iterdir()),
                         sorted(["grappa", "@grappa", "%40grappa", "%2540grappa",
                                 "..x", "a.b", "@a.b"]))

    def test_init_creates_private_layout(self):
        self.init()
        for path in (self.root, self.root / "azzurra", self.root / "azzurra/grappa"):
            self.assertTrue(path.is_dir())
            self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o700)
        window = self.root / "azzurra/grappa"
        self.assertTrue(stat.S_ISFIFO((window / "in").stat().st_mode))
        self.assertTrue(stat.S_ISREG((window / "out").stat().st_mode))
        for name in ("in", "out"):
            self.assertEqual(stat.S_IMODE((window / name).stat().st_mode), 0o600)
            self.assertEqual((window / name).stat().st_uid, os.geteuid())
        self.assertEqual((window / "out").read_bytes(), b"")

    def run_gin(self, *args, ok=True):
        self.assertTrue(BIN.is_file(), "gin executable has not been built")
        result = subprocess.run([str(BIN), *map(str, args)],
                                capture_output=True, text=True, timeout=5,
                                cwd=self.base)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr)
        return result

    def test_version(self):
        result = self.run_gin("--version")
        self.assertEqual(result.stdout, "gin 0.1.0-dev\n")
        self.assertFalse(result.stderr)

    def test_help(self):
        result = self.run_gin("--help")
        self.assertIn("gin init -d ROOT -n NETWORK -c TARGET", result.stdout)
        self.assertIn("REST + Phoenix", result.stdout)
        self.assertIn("not implemented", result.stdout)
        self.assertIn("ASCII A-Z", result.stdout)
        self.assertIn("@grappa", result.stdout)
        self.assertIn("0700", result.stdout)
        self.assertIn("0600", result.stdout)
        self.assertFalse(result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
