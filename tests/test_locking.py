"""Local advisory-lock and recovery tests; no runtime or network."""
import fcntl
import os
from pathlib import Path
import select
import signal
import stat
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parent.parent
BIN = REPO / ".build/gin"


class LockingTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(os.environ.get("TMPDIR", str(REPO / ".build/test-scratch")))
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="gin-lock-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base / "root"
        self.root.mkdir(mode=0o700)

    def gin(self, command="init", root=None, network="net", target="#one"):
        return subprocess.run([str(BIN), command, "-d", str(root or self.root),
                               "-n", network, "-c", target], cwd=self.base,
                              capture_output=True, timeout=5)

    def holder(self):
        child = subprocess.Popen([str(REPO / ".build/lock_holder"), str(self.root)],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=subprocess.PIPE)

        def cleanup():
            if child.poll() is None:
                child.kill()
            child.communicate(timeout=5)
        self.addCleanup(cleanup)
        assert(child.stdout is not None)
        ready, _, _ = select.select([child.stdout], [], [], 5)
        self.assertTrue(ready, "holder did not report acquisition")
        self.assertEqual(child.stdout.readline(), b"ready\n")
        return child

    def test_held_api_release_and_process_death_recover_without_data_loss(self):
        for killed in (False, True):
            with self.subTest(killed=killed):
                lock = self.root / ".gin.lock"
                # No PID meaning: contents are opaque and left alone by gin.
                lock.write_bytes(b"999999 stale pid\x00\n")
                lock.chmod(0o600)
                lock_inode = lock.stat().st_ino
                child = self.holder()
                self.assertEqual(lock.stat().st_ino, lock_inode)
                self.assertEqual(lock.read_bytes(), b"999999 stale pid\x00\n")
                log = self.root / "net/held/out"
                log.write_bytes(b"preserved history\x00\n")
                paths = [self.root, *self.root.rglob("*")]
                metadata = {p: p.lstat() for p in paths}
                self.assertEqual(self.gin(command="check").returncode, 0)
                self.assertEqual(self.gin().returncode, 1)
                for alias in (self.root, "root//."):
                    self.assertEqual(self.gin(root=alias, network="different",
                                              target="Alice").returncode, 1)
                self.assertEqual({p: p.lstat() for p in paths}, metadata)
                self.assertFalse((self.root / "different").exists())
                self.assertFalse((self.root / "unused").exists())
                # A separately rooted initializer is not serialized.
                self.assertEqual(self.gin(root=self.base / "independent").returncode, 0)
                inodes = {p: p.stat().st_ino for p in
                          (lock, log, self.root / "net/held/in")}
                if killed:
                    child.kill()
                    _, error = child.communicate(timeout=5)
                    self.assertEqual(child.returncode, -signal.SIGKILL)
                else:
                    assert child.stdin is not None and child.stdout is not None
                    child.stdin.write(b"r")
                    child.stdin.flush()
                    ready, _, _ = select.select([child.stdout], [], [], 5)
                    self.assertTrue(ready, "holder did not acknowledge release")
                    self.assertEqual(child.stdout.readline(), b"released\n")
                    self.assertIsNone(child.poll(), "release must be tested before process exit")
                    self.assertEqual(self.gin(target="#held").returncode, 0)
                    _, error = child.communicate(input=b"q", timeout=5)
                    self.assertEqual(child.returncode, 0, error)
                self.assertEqual(self.gin(target="#held").returncode, 0)
                self.assertEqual({p: p.stat().st_ino for p in inodes}, inodes)
                self.assertEqual(log.read_bytes(), b"preserved history\x00\n")
                self.assertEqual(lock.read_bytes(), b"999999 stale pid\x00\n")

    def test_partial_layout_recovery_preserves_log_and_fifo(self):
        # Layouts interrupted at different boundaries, including before a lock
        # existed. No blanket removal or transactional rollback is expected.
        for partial in ("network", "window", "fifo", "log"):
            with self.subTest(partial=partial):
                self.root = self.base / partial
                self.root.mkdir(mode=0o700)
                net = self.root / "net"
                net.mkdir(mode=0o700)
                window = net / "one"
                if partial != "network":
                    window.mkdir(mode=0o700)
                if partial == "fifo":
                    os.mkfifo(window / "in", 0o600)
                if partial == "log":
                    (window / "out").write_bytes(b"partial history\x00\n")
                    (window / "out").chmod(0o600)
                preserved = [p for p in self.root.rglob("*")]
                inodes = {p: p.stat().st_ino for p in preserved}
                child = self.holder()
                child.kill()
                child.communicate(timeout=5)
                self.assertEqual(child.returncode, -signal.SIGKILL)
                self.assertEqual(self.gin().returncode, 0)
                self.assertEqual({p: p.stat().st_ino for p in preserved}, inodes)
                self.assertTrue(stat.S_ISFIFO((window / "in").stat().st_mode))
                expected = b"partial history\x00\n" if partial == "log" else b""
                self.assertEqual((window / "out").read_bytes(), expected)

    def test_unsafe_lock_objects_rejected_without_mutation_or_blocking(self):
        for kind in ("symlink", "hardlink", "directory", "fifo", "mode000",
                     "mode400", "mode200", "mode640", "mode1600", "mode4600"):
            with self.subTest(kind=kind):
                self.root = self.base / kind
                self.root.mkdir(mode=0o700)
                lock = self.root / ".gin.lock"
                victim = self.base / (kind + "-victim")
                victim.write_bytes(b"do not change\x00\n")
                victim.chmod(0o600)
                if kind == "symlink":
                    lock.symlink_to(victim)
                elif kind == "hardlink":
                    os.link(victim, lock)
                elif kind == "directory":
                    lock.mkdir(mode=0o700)
                elif kind == "fifo":
                    os.mkfifo(lock, 0o600)
                else:
                    lock.write_bytes(b"unsafe lock data\x00\n")
                    lock.chmod(int(kind[4:], 8))
                before = {p: p.lstat() for p in (self.root, lock, victim)}
                self.assertEqual(self.gin().returncode, 1)
                self.assertEqual(self.gin(command="check").returncode, 0)
                self.assertEqual({p: p.lstat() for p in before}, before)
                self.assertEqual(list(self.root.iterdir()), [lock])
                self.assertEqual(victim.read_bytes(), b"do not change\x00\n")
                if kind.startswith("mode"):
                    lock.chmod(0o600)
                    self.assertEqual(lock.read_bytes(), b"unsafe lock data\x00\n")

    @unittest.skipUnless(os.geteuid() == 0, "Changing owners requires root")
    def test_foreign_lock_owner_rejected(self):
        lock = self.root / ".gin.lock"
        lock.write_bytes(b"foreign lock\n")
        lock.chmod(0o600)
        os.chown(lock, 65534, -1)
        try:
            before = {p: p.lstat() for p in (self.root, lock)}
            self.assertEqual(self.gin().returncode, 1)
            self.assertEqual({p: p.lstat() for p in before}, before)
            self.assertEqual(list(self.root.iterdir()), [lock])
            self.assertEqual(lock.read_bytes(), b"foreign lock\n")
        finally:
            os.chown(lock, os.geteuid(), -1)

    def test_new_lock_has_private_mode_even_with_extreme_umask(self):
        for mask in (0, 0o777):
            self.root = self.base / str(mask)
            old = os.umask(mask)
            try:
                self.assertEqual(self.gin().returncode, 0)
            finally:
                os.umask(old)
            lock = self.root / ".gin.lock"
            self.assertEqual(stat.S_IMODE(lock.stat().st_mode), 0o600)
            self.assertEqual(lock.stat().st_nlink, 1)
            self.assertEqual(lock.stat().st_uid, os.geteuid())
            self.assertEqual(lock.read_bytes(), b"")

    def test_invalid_settings_do_not_create_a_lock(self):
        for network, target in (("bad/name", "#one"), ("net", "#bad/name")):
            self.assertEqual(self.gin(network=network, target=target).returncode, 2)
            self.assertEqual(list(self.root.iterdir()), [])

    def test_held_root_rejects_other_targets_and_aliases_without_mutation(self):
        lock = self.root / ".gin.lock"
        lock.write_bytes(b"persistent opaque data\x00\n")
        lock.chmod(0o600)
        # Make our own directory-enumeration atime change deterministic.
        root_stat = self.root.stat()
        os.utime(self.root, ns=(1, root_stat.st_mtime_ns))
        with lock.open("r+b") as held:
            fcntl.lockf(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            before = {p: p.lstat() for p in (self.root, lock)}
            for alias in (self.root, "./root//."):
                result = self.gin(root=alias, network="other", target="Alice")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual({p: p.lstat() for p in before}, before)
                self.assertEqual(sorted(p.name for p in self.root.iterdir()), [".gin.lock"])
                # Our enumeration may update atime; refresh only after checking
                # that gin itself left the original metadata unchanged.
                before = {p: p.lstat() for p in before}
            self.assertEqual(self.gin(command="check").returncode, 0)
            self.assertEqual({p: p.lstat() for p in before}, before)
        self.assertEqual(lock.read_bytes(), b"persistent opaque data\x00\n")


if __name__ == "__main__":
    unittest.main(verbosity=2)
