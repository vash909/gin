"""A complete persisted live append must leave guarded local progress metadata."""
import contextlib
import json
import os
import subprocess
import select
import stat
import struct
import unittest
import test_session_liveness as live


class AutomaticCheckpoint(live.LivenessFixture):
    @contextlib.contextmanager
    def fixture(self, case):
        with super().fixture(case) as (server, origin):
            server.persisted_id = 17
            yield server, origin

    def test_successful_persisted_output_publishes_automatic_checkpoint(self):
        self.run_case('positive')
        output = (self.certdir / ('heartbeat-' + self._testMethodName + '-positive') /
                  '%4Eet%41' / 'room' / 'out')
        self.assertEqual(output.read_bytes(), live.PREFIX_BYTES + live.LINE)
        checkpoint = output.with_name('.gin.checkpoint')
        self.assertTrue(checkpoint.exists(),
                        'successful persisted output did not publish local checkpoint')
        metadata = checkpoint.lstat()
        self.assertTrue(stat.S_ISREG(metadata.st_mode))
        self.assertEqual(stat.S_IMODE(metadata.st_mode), 0o600)
        self.assertEqual(metadata.st_nlink, 1)
        self.assertGreater(metadata.st_size, 0)
        self.assertLessEqual(metadata.st_size, 8192)
        data = checkpoint.read_bytes()
        self.assertEqual(data[:8], b'GINCP\x00\x00\x01')
        anchor, unknown, device, inode, size, key_size = struct.unpack_from('>6Q', data, 8)
        # The initial empty REST page predates live ID17 and does not prove a
        # processed prefix through it. Preserve the unknown-baseline anchor0.
        self.assertEqual((anchor, unknown), (0, 1))
        output_metadata = output.stat()
        self.assertEqual((device, inode, size),
                         (output_metadata.st_dev, output_metadata.st_ino, output_metadata.st_size))
        self.assertEqual(len(data), 56 + key_size)


    def check_rejected_metadata(self, kind):
        instrumented = bool(live.startup.PREFIX)
        with self.fixture('positive') as (server, origin):
            root = self.certdir / ('checkpoint-guard-' + kind)
            initialized = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=60 if instrumented else 5)
            self.assertEqual(initialized.returncode, 0, repr(initialized.stderr))
            output = root / '%4Eet%41' / 'room' / 'out'
            output.write_bytes(live.PREFIX_BYTES)
            original_inode = output.stat().st_ino
            checkpoint = output.with_name('.gin.checkpoint')
            if kind == 'symlink-lock':
                checkpoint.symlink_to(root / '.gin.lock')
            elif kind == 'hardlink':
                sentinel = root / 'checkpoint-sentinel'
                sentinel.write_bytes(b'preserve sentinel\n')
                sentinel.chmod(0o600)
                os.link(sentinel, checkpoint)
            else:
                checkpoint.write_bytes(b'not a checkpoint\n')
                checkpoint.chmod(0o644 if kind == 'unsafe-mode' else 0o600)
            before = checkpoint.lstat()
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps(dict(version=1, origin=origin, token=live.startup.TOKEN,
                subject_id=live.startup.UUID, subject_name='Alice')))
            credential.chmod(0o600)
            credential_bytes, credential_inode = credential.read_bytes(), credential.stat().st_ino
            settings = self.certdir / ('checkpoint-guard-policy-' + kind)
            interval, timeout = (1000, 3000) if instrumented else (150, 500)
            policy = f'heartbeat_interval_ms={interval}\nheartbeat_timeout_ms={timeout}\nmax_retries=0\n'
            if instrumented:
                policy += 'connect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\n'
            settings.write_text(policy)
            settings.chmod(0o600)
            ready_read, ready_write = os.pipe()
            os.set_blocking(ready_read, False)
            os.set_blocking(ready_write, False)
            # Stored metadata is now loaded before READY/application admission.
            # Permit only this deliberately expected socket close; handler
            # assertions and all other errors remain strict.
            server.client_close_expected.set()
            process = subprocess.Popen(self.command('connect', '-f', settings, '-d', root, '-n', 'NetA',
                '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', ready_write),
                pass_fds=(ready_write,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            os.close(ready_write)
            try:
                self.assertTrue(select.select([ready_read], [], [], 90 if instrumented else 5)[0])
                self.assertEqual(os.read(ready_read, 128), b'',
                                 'unsafe stored checkpoint must prevent READY')
                stdout, stderr = process.communicate(timeout=20 if instrumented else 4)
                self.assertEqual(process.returncode, 1, stderr.decode(errors='replace'))
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'gin: session I/O failure\n')
                self.assertEqual(server.joins, [])
                self.assertEqual(server.heartbeat_refs, [])
                self.assertFalse(server.app_emitted.is_set())
                self.assertFalse(any('/messages?' in path for path in server.paths))
                self.assertEqual(output.read_bytes(), live.PREFIX_BYTES)
                self.assertEqual(output.stat().st_ino, original_inode)
                self.assertEqual(credential.read_bytes(), credential_bytes)
                self.assertEqual(credential.stat().st_ino, credential_inode)
                after = checkpoint.lstat()
                self.assertEqual((after.st_ino, after.st_mode, after.st_nlink, after.st_size),
                                 (before.st_ino, before.st_mode, before.st_nlink, before.st_size))
                if kind == 'symlink-lock':
                    self.assertEqual(os.readlink(checkpoint), str(root / '.gin.lock'))
                else:
                    self.assertEqual(checkpoint.read_bytes(), b'preserve sentinel\n' if kind == 'hardlink'
                                     else b'not a checkpoint\n')
            finally:
                if process.poll() is None:
                    process.kill()
                process.communicate(timeout=5)
                os.close(ready_read)
            # The failed sink must release its original root normally.
            reacquired = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                capture_output=True, timeout=60 if instrumented else 5)
            self.assertEqual(reacquired.returncode, 0, repr(reacquired.stderr))

    def test_malformed_checkpoint_prevents_append(self):
        self.check_rejected_metadata('malformed')

    def test_unsafe_checkpoint_mode_is_not_repaired(self):
        self.check_rejected_metadata('unsafe-mode')

    def test_checkpoint_hardlink_is_not_opened(self):
        self.check_rejected_metadata('hardlink')

    def test_checkpoint_symlink_to_lock_is_not_followed(self):
        self.check_rejected_metadata('symlink-lock')


if __name__ == '__main__':
    unittest.main()
