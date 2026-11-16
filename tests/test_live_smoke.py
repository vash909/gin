"""Offline guards for the explicitly opt-in development smoke; no live traffic."""
import errno
import fcntl
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/live_smoke.py'
spec = importlib.util.spec_from_file_location('opt_in_smoke', SCRIPT)
assert spec is not None and spec.loader is not None
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class SmokeGuards(unittest.TestCase):
    def argv(self):
        return ['--gin', '/nonexistent/gin', '--root', '/nonexistent/root',
                '--origin', 'https://example.invalid', '--network', 'testnet',
                '--target', '#test-smoke', '--work-dir', '/nonexistent/work']

    def test_authorization_required_before_io(self):
        with mock.patch.object(smoke, 'smoke') as run, mock.patch.object(smoke.os, 'open') as opened:
            with self.assertRaises(SystemExit) as error:
                smoke.main(self.argv())
            self.assertEqual(error.exception.code, 2)
            run.assert_not_called()
            opened.assert_not_called()

    def test_explicit_arguments_only_do_not_connect(self):
        with mock.patch.object(smoke.subprocess, 'Popen') as spawn:
            args = smoke.arguments(self.argv() + ['--authorize-plaintext-send'])
            self.assertEqual(args.target, '#test-smoke')
            spawn.assert_not_called()

    def test_origin_and_scope_tokens_are_rejected_not_repaired(self):
        for flag, value in [('--origin', 'http://example.invalid'),
                            ('--origin', 'https://name:credential@example.invalid'),
                            ('--origin', 'https://example.invalid/path'),
                            ('--network', 'TESTNET'), ('--target', 'Peer'),
                            ('--target', '#test/channel'), ('--root', '/safe/../other')]:
            with self.subTest(flag=flag, value=value):
                argv = self.argv() + ['--authorize-plaintext-send']
                argv[argv.index(flag) + 1] = value
                with self.assertRaises(SystemExit) as error:
                    smoke.arguments(argv)
                self.assertEqual(error.exception.code, 2)

    def setUp(self):
        import pwd
        # Guest scratch can live below shared /tmp, while a synced checkout can
        # retain host ownership. Keep the smoke tool's ancestor checks intact:
        # allocate test data only below a separately verified writable base.
        candidates = [os.environ.get('TMPDIR'), SCRIPT.parent.parent / '.build',
                      pwd.getpwuid(os.getuid()).pw_dir]
        for candidate in candidates:
            if not candidate:
                continue
            try:
                base = Path(candidate).resolve(strict=True)
                directory = smoke.private_directory(base, owner_only=False)
                os.close(directory)
                self.temp = tempfile.TemporaryDirectory(prefix='smoke-guard-', dir=base)
            except (OSError, ValueError):
                continue
            self.root = Path(self.temp.name)
            self.addCleanup(self.temp.cleanup)
            break
        else:
            self.fail('No trusted writable scratch base for smoke fixtures')

    def test_private_directory_rejects_symlink_ancestor(self):
        actual = self.root / 'actual'
        actual.mkdir(mode=0o700)
        link = self.root / 'link'
        link.symlink_to(actual, target_is_directory=True)
        with self.assertRaises(OSError):
            smoke.private_directory(link)

    def test_private_directory_rejects_public_mode(self):
        actual = self.root / 'public'
        actual.mkdir(mode=0o755)
        with self.assertRaises(ValueError):
            smoke.private_directory(actual)

    def test_credential_rejects_public_mode_symlink_and_hardlink(self):
        directory = smoke.private_directory(self.root)
        self.addCleanup(os.close, directory)
        private = self.root / '.gin.credentials'
        private.write_bytes(b'{}')
        private.chmod(0o644)
        with self.assertRaises(ValueError):
            smoke.private_digest('.gin.credentials', directory)
        private.chmod(0o600)
        self.assertEqual(len(smoke.private_digest('.gin.credentials', directory)), 32)
        (self.root / 'linked').symlink_to(private)
        with self.assertRaises(OSError):
            smoke.private_digest('linked', directory)
        os.link(private, self.root / 'hardlinked')
        with self.assertRaises(ValueError):
            smoke.private_digest('.gin.credentials', directory)

    def test_second_pipe_failure_closes_first_pipe_and_never_starts_owner(self):
        private = self.root / '.gin.credentials'
        private.write_bytes(b'{}')
        private.chmod(0o600)
        args = smoke.arguments(self.argv() + ['--authorize-plaintext-send'])
        args.root = args.work_dir = str(self.root)
        first = os.pipe()
        with mock.patch.object(smoke.os, 'pipe', side_effect=[first, OSError(errno.EMFILE, 'fixture')]), \
                mock.patch.object(smoke.subprocess, 'Popen') as spawn:
            result = smoke.smoke(args)
        self.assertFalse(result['passed'])
        self.assertEqual(result['failed_phase'], 'private-setup')
        spawn.assert_not_called()
        for fd in first:
            with self.assertRaises(OSError) as error:
                fcntl.fcntl(fd, fcntl.F_GETFD)
            self.assertEqual(error.exception.errno, errno.EBADF)


    def test_fixture_avoids_shared_temporary_ancestors(self):
        shared = self.root / 'shared'
        shared.mkdir(mode=0o1777)
        shared.chmod(0o1777)
        checkout = shared / 'checkout'
        (checkout / '.build').mkdir(parents=True, mode=0o700)
        home = self.root / 'private-home'
        home.mkdir(mode=0o700)
        from types import SimpleNamespace
        fixture = SmokeGuards('test_explicit_arguments_only_do_not_connect')
        accepted = False
        try:
            with mock.patch.dict(os.environ, {'TMPDIR': str(shared)}), \
                    mock.patch(__name__ + '.SCRIPT', checkout / 'scripts/live_smoke.py'), \
                    mock.patch('pwd.getpwuid', return_value=SimpleNamespace(pw_dir=str(home))):
                fixture.setUp()
            try:
                directory = smoke.private_directory(fixture.root)
            except (OSError, ValueError):
                pass
            else:
                os.close(directory)
                accepted = True
        finally:
            fixture.doCleanups()
        self.assertTrue(accepted, 'Fixture selected a root beneath a shared ancestor')


class SmokeIPC(SmokeGuards):
    """Harness IPC/failure regressions only; not native/live qualification."""

    def run_owner(self, mode):
        import json
        import subprocess
        import sys
        import textwrap

        root = self.root / 'enrolled'
        root.mkdir(mode=0o700)
        credential = root / '.gin.credentials'
        credential.write_bytes(b'{}')
        credential.chmod(0o600)
        window = root / 'testnet' / 'test-smoke'
        window.mkdir(parents=True, mode=0o700)
        window.parent.chmod(0o700)
        out = window / 'out'
        out.write_bytes(b'original prefix\n')
        out.chmod(0o600)
        owner = self.root / 'owner'
        owner.write_text('#!' + sys.executable + '\n' + textwrap.dedent('''\
            import os, pathlib, signal, sys
            signal.alarm(8)
            mode = MODE
            argv = sys.argv
            conf = pathlib.Path(argv[argv.index('-f') + 1]).read_text()
            settings = dict(line.split('=', 1) for line in conf.splitlines())
            window = pathlib.Path(settings['root']) / settings['network'] / settings['target'][1:]
            out = window / 'out'
            fifo = window / 'in'
            os.mkfifo(fifo, 0o600)
            reader = os.open(fifo, os.O_RDWR)
            ready = int(argv[argv.index('-R') + 1])
            operations = int(argv[argv.index('-O') + 1])
            marker = b''
            def stop(signum, frame):
                if mode == 'prefix':
                    out.write_bytes(b'changed prefix\\n' + marker)
                elif mode == 'truncate':
                    out.write_bytes(b'original prefix\\n')
                elif mode == 'inode':
                    replacement = window / 'replacement'
                    replacement.write_bytes(out.read_bytes())
                    replacement.chmod(0o600)
                    replacement.replace(out)
                elif mode == 'duplicate':
                    with out.open('ab') as stream:
                        stream.write(marker)
                sys.exit(0)
            signal.signal(signal.SIGTERM, stop)
            os.write(ready, b'READY\\n')
            while not marker.endswith(b'\\n'):
                marker += os.read(reader, 1024)
            with out.open('ab') as stream:
                stream.write(marker)
            receipts = b'OP1 1 SEND PENDING 0\\nOP1 1 SEND ACCEPTED_PERSISTED 0\\n'
            if mode == 'secret':
                receipts = b'OP1 1 SEND PENDING 0\\nSYNTHETIC_SECRET\\n'
            os.write(operations, receipts)
            while True:
                signal.pause()
            ''').replace('MODE', repr(mode)))
        owner.chmod(0o700)
        # Independent deadline wraps the real smoke(), not Popen mocking.
        runner = textwrap.dedent('''\
            import importlib.util, json, sys
            spec = importlib.util.spec_from_file_location('smoke', sys.argv[1])
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            result = module.smoke(module.arguments(sys.argv[2:]))
            print(json.dumps(result, sort_keys=True))
            ''')
        command = [sys.executable, '-c', runner, str(SCRIPT), '--gin', str(owner),
                   '--root', str(root), '--work-dir', str(self.root),
                   '--origin', 'https://example.invalid', '--network', 'testnet',
                   '--target', '#test-smoke', '--authorize-plaintext-send']
        completed = subprocess.run(command, capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(completed.stderr, '')
        return json.loads(completed.stdout), completed.stdout

    def test_unexpected_receipt_is_not_public(self):
        result, emitted = self.run_owner('secret')
        self.assertFalse(result['passed'])
        self.assertEqual(result['failed_phase'], 'one-send')
        self.assertEqual(result['owner_exit'], 0)
        self.assertNotIn('SYNTHETIC_SECRET', emitted)
        self.assertNotIn('receipts', result)

    def assert_stopped_output_rejected(self, mode):
        result, emitted = self.run_owner(mode)
        self.assertEqual(result['owner_exit'], 0)
        self.assertEqual(result['stdout_bytes'], 0)
        self.assertEqual(result['stderr_bytes'], 0)
        self.assertEqual(result['receipts'],
                         ['OP1 1 SEND PENDING 0', 'OP1 1 SEND ACCEPTED_PERSISTED 0'])
        self.assertFalse(result['passed'], emitted)
        self.assertEqual(result['failed_phase'], 'stop')
        self.assertEqual(result['exception_type'], 'ValueError')

    def test_post_stop_prefix_corruption(self):
        self.assert_stopped_output_rejected('prefix')

    def test_post_stop_truncation(self):
        self.assert_stopped_output_rejected('truncate')

    def test_post_stop_inode_replacement(self):
        self.assert_stopped_output_rejected('inode')

    def test_post_stop_duplicate_marker(self):
        self.assert_stopped_output_rejected('duplicate')

    def test_real_ipc_clean_stop_control(self):
        result, emitted = self.run_owner('clean')
        self.assertTrue(result['passed'], emitted)
        self.assertTrue(result['observed_marker_once'])
        self.assertTrue(result['prefix_inode_credentials_preserved'])
        self.assertEqual(result['owner_exit'], 0)


if __name__ == '__main__':
    unittest.main()
