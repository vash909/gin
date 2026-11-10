#!/usr/bin/env python3
"""Private synthetic credential fixtures; never print credential contents."""
import json
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import unittest

DRIVER = str(Path(sys.argv.pop(1)).resolve()) if len(sys.argv) > 1 else str(Path('.build/credential_driver').resolve())
OK, INVALID, UNSAFE, IO, NOT_FOUND = range(5)

class Credentials(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(dir=Path(os.environ.get('TMPDIR', '/tmp')).resolve())
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'root'
        self.proc = subprocess.Popen([DRIVER, str(self.root)], stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        self.addCleanup(self.stop)
        self.assertEqual(self.line(), 'ready')
        self.path = self.root / '.gin.credentials'

    def line(self):
        self.assertTrue(select.select([self.proc.stdout], [], [], 5)[0], 'driver response deadline')
        return self.proc.stdout.readline().strip()

    def command(self, cmd):
        self.proc.stdin.write(cmd + '\n')
        self.proc.stdin.flush()
        return tuple(map(int, self.line().split()))

    def stop(self):
        try:
            self.proc.communicate('quit\n' if self.proc.poll() is None else None, timeout=5)
        except (subprocess.TimeoutExpired, BrokenPipeError):
            self.proc.kill()
            self.proc.communicate(timeout=5)
        self.assertEqual(self.proc.returncode, 0, 'driver failed (private diagnostics withheld)')

    def fixture(self, data):
        self.path.write_bytes(data)
        self.path.chmod(0o600)

    def test_clear(self):
        self.assertEqual(self.command('clear'), (OK, 1, 0))

    def test_roundtrip_replace_forget(self):
        self.assertEqual(self.command('load'), (NOT_FOUND, 1, 0))
        self.assertEqual(self.command('save')[0], OK)
        self.assertEqual(self.path.stat().st_mode & 0o7777, 0o600)
        self.assertEqual(self.command('load'), (OK, 0, 1))
        self.assertEqual(self.command('other'), (INVALID, 1, 0))
        old = self.path.read_bytes()
        self.assertEqual(self.command('invalid-save')[0], INVALID)
        self.assertTrue(old == self.path.read_bytes(), 'old record was changed')
        self.assertEqual(self.command('replace')[0], OK)
        self.assertEqual(self.command('load'), (OK, 0, 0))
        self.assertEqual(self.command('forget')[0], OK)
        self.assertFalse(self.path.exists())
        self.assertEqual(self.command('forget')[0], NOT_FOUND)

    def test_write_failure_preserves_old_record(self):
        self.assertEqual(self.command('save')[0], OK)
        before = self.path.read_bytes()
        inode = self.path.stat().st_ino
        self.assertEqual(self.command('write-failure')[0], IO)
        self.assertTrue(before == self.path.read_bytes(), 'write failure changed old record')
        self.assertEqual(self.path.stat().st_ino, inode)
        self.assertFalse(list(self.root.glob('.gin.credentials.tmp.*')))
        self.assertEqual(self.command('load'), (OK, 0, 1))

    def test_exhausted_temporary_names_preserve_old_record(self):
        self.assertEqual(self.command('save')[0], OK)
        before = self.path.read_bytes()
        names = [self.root / ('.gin.credentials.tmp.%d.%d' % (self.proc.pid, n))
                 for n in range(2, 66)]
        for name in names:
            name.write_bytes(b'synthetic stale fixture')
            name.chmod(0o600)
        self.assertEqual(self.command('replace')[0], IO)
        self.assertTrue(before == self.path.read_bytes(), 'collision failure changed old record')
        for name in names:
            self.assertTrue(name.read_bytes() == b'synthetic stale fixture')

    def test_released_root(self):
        self.assertEqual(self.command('released'), (INVALID, 1, 0))
        self.assertEqual(self.command('save')[0], INVALID)
        self.assertEqual(self.command('forget')[0], INVALID)

    def test_boundary_record_and_escaped_unicode(self):
        self.assertEqual(self.command('save')[0], OK)
        record = json.loads(self.path.read_bytes())
        record['token'] = 'x' * 4096
        record['subject_name'] = 'x' * 255
        self.fixture(json.dumps(record).encode())
        self.assertEqual(self.command('load')[0], OK)
        record['subject_name'] = 'Unicode \u00e9 \\" { [ ] }'
        self.fixture(json.dumps(record).encode())
        self.assertEqual(self.command('load')[0], OK)

    def test_malformed_records(self):
        self.assertEqual(self.command('save')[0], OK)
        good = self.path.read_bytes()
        record = json.loads(good)
        cases = [b'', b'{}', good + b'{}', good + b'\x00',
                 good[:-1] + b',"version":1}',
                 good.replace(b'"version":1', b'"version":true'),
                 good.replace(b'"version":1', b'"version":2'),
                 good.replace(b'Synthetic user', b'bad\\u0000name'),
                 good.replace(b'Synthetic user', b'bad\xffname'),
                 b'[' * 3000 + b']' * 3000,
                 b'{"x":' * 2000 + b'1' + b'}' * 2000,
                 b' ' * 16385]
        for key in record:
            missing = dict(record)
            del missing[key]
            cases.append(json.dumps(missing).encode())
        for key, value in [('extra', 1), ('token', 123), ('token', 'bad token'),
                           ('subject_name', 'bad\u0080'), ('subject_id', 'bad'),
                           ('origin', 'https://elsewhere.test'),
                           ('subject_name', 'x' * 256), ('token', 'x' * 4097),
                           ('token', []), ('subject_name', {})]:
            changed = dict(record)
            changed[key] = value
            cases.append(json.dumps(changed).encode())
        for n, data in enumerate(cases):
            with self.subTest(case=n):
                self.fixture(data)
                self.assertEqual(self.command('load'), (INVALID, 1, 0))

    def unsafe_operations(self):
        identity = self.path.lstat()
        self.assertEqual(self.command('load'), (UNSAFE, 1, 0))
        self.assertEqual(self.command('save')[0], UNSAFE)
        self.assertEqual(self.command('forget')[0], UNSAFE)
        after = self.path.lstat()
        self.assertEqual((identity.st_dev, identity.st_ino, identity.st_mode),
                         (after.st_dev, after.st_ino, after.st_mode))

    def test_unsafe_modes(self):
        self.fixture(b'private synthetic data')
        for mode in (0o644, 0o400, 0o000, 0o1600, 0o4600):
            with self.subTest(mode=mode):
                self.path.chmod(mode)
                self.unsafe_operations()
        self.path.chmod(0o600)

    def test_symlink(self):
        outside = Path(self.tmp.name) / 'outside'
        outside.write_bytes(b'private synthetic data')
        outside.chmod(0o600)
        self.path.symlink_to(outside)
        self.unsafe_operations()
        self.assertTrue(outside.read_bytes() == b'private synthetic data')

    def test_hardlink(self):
        outside = Path(self.tmp.name) / 'outside'
        outside.write_bytes(b'private synthetic data')
        outside.chmod(0o600)
        os.link(outside, self.path)
        self.unsafe_operations()

    def test_fifo(self):
        os.mkfifo(self.path, 0o600)
        self.unsafe_operations()

    def test_directory(self):
        self.path.mkdir(mode=0o700)
        self.unsafe_operations()

    def test_lock_alias_retains_lock(self):
        os.link(self.root / '.gin.lock', self.path)
        self.unsafe_operations()
        check = subprocess.run([sys.executable, '-c',
            'import fcntl,sys; f=open(sys.argv[1],"r+"); '
            '\ntry: fcntl.lockf(f,fcntl.LOCK_EX|fcntl.LOCK_NB)'
            '\nexcept BlockingIOError: sys.exit(0)'
            '\nsys.exit(1)', str(self.root / '.gin.lock')], timeout=5,
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.assertEqual(check.returncode, 0, 'credential operation released root lock')
        self.path.unlink()
        self.assertEqual(self.command('save')[0], OK)

    def test_root_mode_change(self):
        self.root.chmod(0o750)
        self.assertEqual(self.command('load'), (UNSAFE, 1, 0))
        self.assertEqual(self.command('save')[0], UNSAFE)
        self.assertEqual(self.command('forget')[0], UNSAFE)
        self.root.chmod(0o700)

    @unittest.skipUnless(os.geteuid() == 0, 'foreign owner requires isolated privilege')
    def test_foreign_owner(self):
        self.fixture(b'private synthetic data')
        os.chown(self.path, 65534, -1)
        self.unsafe_operations()

    def test_stale_temporary_files_untouched(self):
        stale = self.root / '.gin.credentials.tmp.stale'
        stale.write_bytes(b'private synthetic stale data')
        stale.chmod(0o600)
        self.assertEqual(self.command('save')[0], OK)
        self.assertTrue(stale.read_bytes() == b'private synthetic stale data')
        self.assertEqual(len(list(self.root.glob('.gin.credentials.tmp.*'))), 1)

    def test_validate(self):
        self.assertEqual(self.command('validate')[0], OK)
        for n in range(1, 10):
            with self.subTest(case=n):
                self.assertEqual(self.command('validate' + str(n))[0], INVALID)

if __name__ == '__main__':
    unittest.main()
