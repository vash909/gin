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
        for network, target, options, relative in (
                ("azzurra", "#grappa", (), "azzurra/grappa"),
                ("Net", "&Foo", ("-T", "#&"), "%4Eet/+26foo")):
            args = ("init", "-d", self.root, "-n", network, "-c", target, *options)
            self.run_gin(*args)
            window = self.root / relative
            for path in (self.root, window.parent, window, window / "in", window / "out"):
                with self.subTest(path=str(path.relative_to(self.base))):
                    os.chown(path, 65534, -1)
                    try:
                        self.run_gin(*args, ok=False)
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
        self.assertTrue((self.root / "%41zzurra/grappa/out").is_file())

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

    def run_gin(self, *args, ok=True, env=None):
        self.assertTrue(BIN.is_file(), "gin executable has not been built")
        result = subprocess.run([str(BIN), *map(str, args)],
                                capture_output=True, text=True, timeout=5,
                                cwd=self.base, env=env)
        if ok:
            self.assertEqual(result.returncode, 0, result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertTrue(result.stderr)
        return result

    def test_check_is_read_only_and_not_a_filesystem_probe(self):
        self.run_gin("check", "-d", self.root / "missing/child",
                     "-n", "azzurra", "-c", "#grappa")
        self.assertFalse(self.root.exists())
        self.root.symlink_to(self.base / "absent", target_is_directory=True)
        self.run_gin("check", "-d", self.root, "-n", "azzurra", "-c", "#grappa")
        self.assertTrue(self.root.is_symlink())

    def test_effective_settings_errors_are_exit_two_without_mutation(self):
        for command in ("init", "check"):
            for root, network, target in (("", "net", "#ok"),
                                          ("new/../root", "net", "#ok"),
                                          ("root", "bad/name", "#ok"),
                                          ("root", "net", "#bad/name"),
                                          ("root\x01", "net", "#ok"),
                                          ("x" * 4096, "net", "#ok"),
                                          ("root", "net", "#" + "x" * 256)):
                with self.subTest(command=command, root=root, network=network, target=target):
                    result = self.run_gin(command, "-d", root, "-n", network,
                                          "-c", target, ok=False)
                    self.assertEqual(result.returncode, 2)
                    self.assertEqual(list(self.base.iterdir()), [])

    def config(self, data, path=None):
        path = self.base / "gin.conf" if path is None else path
        path.write_bytes(data)
        path.chmod(0o600)
        return path

    def test_explicit_config_precedence_and_cwd_relative_paths(self):
        directory = self.base / "settings"
        directory.mkdir()
        config = self.config(b" # comment\r\n\troot = ./root \r\nnetwork=FileNet\r\ntarget=#Grappa",
                             directory / "gin.conf")
        original = config.read_bytes()
        self.run_gin("check", "-f", config)
        self.assertFalse(self.root.exists())
        self.run_gin("init", "-n", "CliNet", "-f", config, "-c", "Alice")
        self.assertTrue((self.root / "%43li%4Eet/@alice/out").is_file())
        self.assertFalse((directory / "root").exists())
        self.assertFalse((self.root / "FileNet").exists())
        self.assertEqual(config.read_bytes(), original)
        other = self.base / "other"
        self.run_gin("init", "-f", config, "-d", other)
        self.assertTrue((other / "%46ile%4Eet/grappa/out").is_file())

    def test_partial_config_and_cli_missing_duplicate_unknown_options(self):
        config = self.config(b"network=net\n")
        for command in ("init", "check"):
            self.run_gin(command, "-f", config, "-d", self.root, "-c", "#ok")
            for options in (("-f", config), ("-f", config, "-f", config),
                            ("-d", self.root, "-d", self.root),
                            ("-n", "net", "-n", "net"),
                            ("-c", "#ok", "-c", "#ok"),
                            ("--token", "PRIVATE_PAYLOAD"), ("-f", ""),
                            ("-f",), ("--", "extra")):
                with self.subTest(command=command, options=options):
                    result = self.run_gin(command, *options, ok=False)
                    self.assertEqual(result.returncode, 2)
                    self.assertNotIn("PRIVATE_PAYLOAD", result.stderr)
        # No implicit environment or home-directory discovery.
        result = self.run_gin("check", ok=False)
        self.assertEqual(result.returncode, 2)

    def test_config_malformed_even_when_cli_overrides_every_setting(self):
        payloads = (b"root=root\nroot=other\n", b"network=net\nnetwork=net\n",
                    b"target=#ok\ntarget=#ok\n", b"password=PRIVATE_PAYLOAD\n",
                    b"PRIVATE_PAYLOAD\n", b"ROOT=root\n", b"root= \t\n",
                    b"network=\n", b"target=\n", b"=PRIVATE_PAYLOAD\n",
                    b"include=PRIVATE_PAYLOAD\n")
        for data in payloads:
            config = self.config(data)
            before = config.read_bytes()
            for command in ("init", "check"):
                with self.subTest(command=command, data=data):
                    result = self.run_gin(command, "-f", config, "-d", self.root,
                                          "-n", "net", "-c", "#ok", ok=False)
                    self.assertEqual(result.returncode, 2)
                    self.assertNotIn("PRIVATE_PAYLOAD", result.stderr)
                    self.assertFalse(result.stdout)
                    self.assertFalse(self.root.exists())
                    self.assertEqual(config.read_bytes(), before)

    def test_config_hash_is_not_an_inline_comment_or_shell_syntax(self):
        config = self.config(b"root=root\nnetwork=net\ntarget=#a#b\n")
        self.run_gin("init", "-f", config)
        self.assertTrue((self.root / "net/a%23b/out").is_file())
        for data in (b"root=root\nnetwork=net # comment\ntarget=#ok\n",
                     b"root=root\nnetwork='net'\ntarget=#ok\n"):
            self.config(data)
            result = self.run_gin("check", "-f", config, ok=False)
            self.assertEqual(result.returncode, 2)

    def test_config_unsafe_objects_and_io_errors_are_exit_one_without_mutation(self):
        valid = self.config(b"root=root\nnetwork=net\ntarget=#ok\n")
        link = self.base / "symlink"
        link.symlink_to(valid)
        hardlink = self.base / "hardlink"
        os.link(valid, hardlink)
        fifo = self.base / "fifo"
        os.mkfifo(fifo, 0o600)
        for path in (link, hardlink, valid, fifo, self.base, self.base / "missing"):
            for command in ("init", "check"):
                with self.subTest(command=command, path=path.name):
                    result = self.run_gin(command, "-f", path, "-d", self.root,
                                          "-n", "net", "-c", "#ok", ok=False)
                    self.assertEqual(result.returncode, 1)
                    self.assertFalse(self.root.exists())
        hardlink.unlink()
        for mode in (0o000, 0o400, 0o640, 0o644, 0o1600, 0o4600):
            valid.chmod(mode)
            for command in ("init", "check"):
                result = self.run_gin(command, "-f", valid, ok=False)
                self.assertEqual(result.returncode, 1)
                self.assertEqual(stat.S_IMODE(valid.stat().st_mode), mode)
        valid.chmod(0o600)

    @unittest.skipUnless(os.geteuid() == 0, "Changing owners requires root")
    def test_config_foreign_owner_is_rejected(self):
        config = self.config(b"root=root\nnetwork=net\ntarget=#ok\n")
        os.chown(config, 65534, -1)
        try:
            result = self.run_gin("check", "-f", config, ok=False)
            self.assertEqual(result.returncode, 1)
        finally:
            os.chown(config, os.geteuid(), -1)

    def test_config_controls_nul_and_line_overflow_rejected_even_in_comments(self):
        prefix = b"root=root\nnetwork=net\ntarget=#ok\n"
        payloads = [prefix + b"#hidden" + bytes([byte]) + b"PRIVATE_PAYLOAD\n"
                    for byte in (*range(9), 11, 12, *range(14, 32), 127)]
        payloads += [prefix + b"#bad\rcomment\n", prefix + b"#bad\r",
                     b"#" + b"a" * 8192 + b"\n", b"#" + b"a" * 16384,
                     prefix + b"target=#other\x00\n"]
        for data in payloads:
            config = self.config(data)
            for command in ("check", "init"):
                with self.subTest(command=command, data_length=len(data)):
                    result = self.run_gin(command, "-f", config, "-d", self.root,
                                          "-n", "net", "-c", "#ok", ok=False)
                    self.assertEqual(result.returncode, 2)
                    self.assertNotIn("PRIVATE_PAYLOAD", result.stderr)
                    self.assertFalse(self.root.exists())

    def test_config_bounds_are_exact_and_never_truncated_by_overrides(self):
        for data in (b"#" + b"a" * 8191 + b"\n",
                     (b"#" + b"a" * 8190 + b"\n") * 2,
                     b"#" + b"a" * 8191 + b"\r\n",
                     b"\n\t  \r\n# comment without final newline"):
            config = self.config(data)
            self.run_gin("check", "-f", config, "-d", self.root,
                         "-n", "net", "-c", "#ok")
        for data in ((b"#" + b"a" * 8190 + b"\n") * 2 + b"\n",
                     b"root=" + b"r" * 4096,
                     b"network=" + b"n" * 256,
                     b"target=#" + b"t" * 256):
            config = self.config(data)
            result = self.run_gin("check", "-f", config, "-d", self.root,
                                  "-n", "net", "-c", "#ok", ok=False)
            self.assertEqual(result.returncode, 2)
        self.assertFalse(self.root.exists())

    def test_effective_raw_and_encoded_boundaries(self):
        self.run_gin("check", "-d", "r" * 4095, "-n", "n" * 255,
                     "-c", "#" + "t" * 255)
        config = self.config(b"root=" + b"r" * 4095 + b"\nnetwork=" + b"n" * 255 +
                             b"\ntarget=#" + b"t" * 255)
        self.run_gin("check", "-f", config)
        self.run_gin("check", "-d", self.root, "-n", "net", "-c", "#" + "%" * 85)
        for target in ("#" + "%" * 86, "t" * 255, "#" + "é" * 43):
            result = self.run_gin("check", "-d", self.root, "-n", "net",
                                  "-c", target, ok=False)
            self.assertEqual(result.returncode, 2)
        self.assertFalse(self.root.exists())

    def test_no_implicit_discovery_or_config_environment(self):
        directory = self.base / ".config/gin"
        directory.mkdir(parents=True)
        config = self.config(b"root=root\nnetwork=net\ntarget=#ok\n", directory / "config")
        self.config(config.read_bytes(), directory / "gin.conf")
        env = dict(os.environ, HOME=str(self.base), XDG_CONFIG_HOME=str(self.base / ".config"),
                   GIN_CONFIG=str(config), GIN_ROOT=str(self.root),
                   GIN_NETWORK="net", GIN_TARGET="#ok")
        result = self.run_gin("check", env=env, ok=False)
        self.assertEqual(result.returncode, 2)
        self.run_gin("check", "-d", self.root, "-n", "net", "-c", "#ok", env=env)
        self.assertFalse(self.root.exists())

    def test_check_and_invalid_init_preserve_existing_tree(self):
        self.init()
        log = self.root / "azzurra/grappa/out"
        log.write_bytes(b"PRIVATE_HISTORY\x00\n")
        self.root.chmod(0o755)  # check must not probe or repair unsafe output modes.
        paths = [self.root, *sorted(self.root.rglob("*"))]
        before = {path: path.lstat() for path in paths}
        config = self.config(b"root=root\nnetwork=azzurra\ntarget=#grappa\n")
        self.run_gin("check", "-f", config)
        self.config(b"root=root\nnetwork=azzurra\ntarget=#grappa\ntoken=PRIVATE_PAYLOAD\n")
        result = self.run_gin("init", "-f", config, ok=False)
        self.assertEqual(result.returncode, 2)
        self.assertNotIn("PRIVATE_PAYLOAD", result.stderr)
        self.assertEqual({path: path.lstat() for path in paths}, before)
        self.assertEqual(log.read_bytes(), b"PRIVATE_HISTORY\x00\n")

    def test_configured_channel_types_and_network_case_are_disjoint(self):
        targets = {"#Foo": "foo", "&Foo": "+26foo", "+Foo": "+2Bfoo",
                   "!Foo": "+21foo", "#+26foo": "%2B26foo", "Foo": "@foo"}
        for network, encoded in (("net", "net"), ("Net", "%4Eet"), ("NET", "%4E%45%54")):
            for target, window in targets.items():
                self.run_gin("init", "-d", self.root, "-n", network, "-c", target,
                             "-T", "#&+!")
                log = self.root / encoded / window / "out"
                self.assertTrue(log.is_file())
                log.write_bytes(target.encode())
                inode = log.stat().st_ino
                self.run_gin("init", "-T", "!+#&", "-d", self.root,
                             "-n", network, "-c", target)
                self.assertEqual(log.stat().st_ino, inode)
                self.assertEqual(log.read_bytes(), target.encode())
        self.assertEqual(sorted(p.name for p in self.root.iterdir()),
                         sorted(["net", "%4Eet", "%4E%45%54", ".gin.lock"]))

    def test_chantypes_config_empty_precedence_and_duplicates(self):
        config = self.config(b"root=root\nnetwork=net\ntarget=&Foo\nchantypes=#&+!\n")
        self.run_gin("init", "-f", config)
        self.assertTrue((self.root / "net/+26foo/out").exists())
        self.run_gin("init", "-f", config, "-T", "")
        self.assertTrue((self.root / "net/@%26foo/out").exists())
        self.config(b"root=root\nnetwork=net\ntarget=#Foo\nchantypes=\n")
        self.run_gin("init", "-f", config)
        self.assertTrue((self.root / "net/@%23foo/out").exists())
        self.run_gin("init", "-f", config, "-T", "#")
        self.assertTrue((self.root / "net/foo/out").exists())
        for value in ("##", "#x", "#&+!!", "é", " ", "#\t", "#\x7f"):
            result = self.run_gin("check", "-f", config, "-T", value, ok=False)
            self.assertEqual(result.returncode, 2)
        for options in (("-T", "", "-T", "#"), ("-T", "#", "-T", "#")):
            self.assertEqual(self.run_gin("check", "-f", config, *options, ok=False).returncode, 2)
        for data in (b"chantypes=\nchantypes=#\n", b"chantypes=#\nchantypes=#\n",
                     b"chantypes=12345\n"):
            self.config(data)
            self.assertEqual(self.run_gin("check", "-f", config, "-d", self.root,
                                         "-n", "net", "-c", "ok", "-T", "#", ok=False).returncode, 2)
        # Semantic validation is on effective values, grammar/bounds on every file value.
        self.config(b"chantypes=x\n")
        self.run_gin("check", "-f", config, "-d", self.root, "-n", "net", "-c", "ok", "-T", "#")

    def test_utf8_scalars_and_expanded_network_bounds_before_mutation(self):
        invalid = [b"#\xc0\x80", b"#\xe0\x80\x80", b"#\xed\xa0\x80", b"#\xf4\x90\x80\x80",
                   b"#\xe2\x82", b"#\x80", b"#\xc2\x80", b"#\xc2\x9f"]
        for command in ("init", "check"):
            for target in invalid:
                config = self.config(b"root=root\nnetwork=net\ntarget=" + target + b"\n")
                self.assertEqual(self.run_gin(command, "-f", config, ok=False).returncode, 2)
                self.assertFalse(self.root.exists())
            self.run_gin(command, "-d", self.root, "-n", "A" * (255 // 3), "-c", "#ok")
            other = self.base / "absent"
            self.assertEqual(self.run_gin(command, "-d", other, "-n", "A" * (255 // 3 + 1),
                                         "-c", "#ok", ok=False).returncode, 2)
            self.assertFalse(other.exists())
            if self.root.exists():
                import shutil
                shutil.rmtree(self.root)
        for target, component in (("#é", "%C3%A9"), ("#é", "e%CC%81"),
                                  ("#É", "%C3%89"), ("#😀", "%F0%9F%98%80")):
            self.init(target=target)
            self.assertTrue((self.root / "azzurra" / component / "out").exists())
        names = [p.name.casefold() for p in (self.root / "azzurra").iterdir()]
        self.assertEqual(len(names), len(set(names)))

    def test_encoded_paths_keep_descriptor_security_and_log_identity(self):
        for kind in ("network-symlink", "window-symlink", "in-symlink", "out-symlink", "out-hardlink", "mode"):
            self.root = self.base / kind
            args = ("init", "-d", self.root, "-n", "Net", "-c", "&Foo", "-T", "#&")
            self.run_gin(*args)
            window = self.root / "%4Eet/+26foo"
            log = window / "out"
            log.write_bytes(b"encoded history\x00\n")
            before = {n: (window / n).stat().st_ino for n in ("in", "out")}
            self.run_gin(*args)
            self.assertEqual({n: (window / n).stat().st_ino for n in before}, before)
            victim = self.base / (kind + "-victim")
            victim.write_bytes(b"keep victim\n")
            victim.chmod(0o600)
            if kind == "network-symlink":
                network = window.parent
                network.rename(self.root / "saved")
                network.symlink_to(self.root / "saved", target_is_directory=True)
            elif kind == "window-symlink":
                window.rename(window.parent / "saved")
                window.symlink_to(window.parent / "saved", target_is_directory=True)
            elif kind == "mode":
                window.chmod(0o750)
            else:
                endpoint = window / ("in" if kind == "in-symlink" else "out")
                endpoint.unlink()
                if kind == "out-hardlink":
                    os.link(victim, endpoint)
                else:
                    endpoint.symlink_to(victim)
            paths = [self.root, *self.root.rglob("*")]
            metadata = {p: p.lstat() for p in paths}
            self.assertEqual(self.run_gin(*args, ok=False).returncode, 1)
            self.run_gin("check", *args[1:])
            self.assertEqual({p: p.lstat() for p in paths}, metadata)
            self.assertEqual(victim.read_bytes(), b"keep victim\n")
            if kind not in ("out-symlink", "out-hardlink"):
                if kind == "network-symlink":
                    log = self.root / "saved/+26foo/out"
                elif kind == "window-symlink":
                    log = window.parent / "saved/out"
                self.assertEqual(log.read_bytes(), b"encoded history\x00\n")
            if kind == "mode":
                window.chmod(0o700)

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
        self.assertIn("-T CHANTYPES", result.stdout)
        self.assertIn("UTF-8", result.stdout)
        self.assertIn("%41", result.stdout)
        self.assertIn("@grappa", result.stdout)
        self.assertIn("0700", result.stdout)
        self.assertIn("0600", result.stdout)
        self.assertFalse(result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
