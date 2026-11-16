"""Private owner clock jump after real pre-READY loss; no production hook."""
import json
import os
import select
import subprocess
import unittest
from typing import cast
import test_session as startup
import test_session_reconnect as recovery


class StartupDeadline(recovery.ReconnectFixture):
    def test_original_startup_deadline_is_not_restarted_after_join_loss(self):
        with self.fixture('joining_close') as (srv, origin):
            root = self.certdir / 'startup-deadline-root'
            init = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                  capture_output=True, timeout=90 if startup.PREFIX else 5)
            self.assertEqual(init.returncode, 0, repr(init.stderr))
            output, = root.rglob('out')
            output.write_bytes(b'existing prefix\n')
            inode, lock_inode = output.stat().st_ino, (root / '.gin.lock').stat().st_ino
            credential = root / '.gin.credentials'
            credential.write_text(json.dumps({'version': 1, 'origin': origin, 'token': startup.TOKEN,
                'subject_id': startup.UUID, 'subject_name': 'Alice'}))
            credential.chmod(0o600)
            old_credential = credential.read_bytes()
            old_tree = {str(p.relative_to(root)) for p in root.rglob('*')}
            config = self.certdir / 'startup-deadline-settings'
            config.write_text('startup_ms=180000\nconnect_ms=10000\ntotal_ms=20000\n'
                              'join_ms=12000\nheartbeat_interval_ms=60000\n'
                              'heartbeat_timeout_ms=10000\nretry_base_ms=20\n'
                              'retry_cap_ms=100\nmax_retries=2\n')
            config.chmod(0o600)
            rr, rw = os.pipe()
            for fd in (rr, rw):
                os.set_blocking(fd, False)
            proc = None
            try:
                command = self.command('connect', '-f', config, '-d', root, '-n', 'NetA',
                    '-c', '#Room', '-e', origin, '-m', 'plain', '-a', self.certdir / 'ca.pem', '-R', rw)
                private = os.environ.get('GIN_OWNER_BINARY', str(recovery.REPO / '.build/gin_owner_fault'))
                command[len(startup.PREFIX)] = private
                proc = subprocess.Popen(command, env=dict(os.environ, GIN_OWNER_FAULT='startup-expire'),
                    pass_fds=(rw,), stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                os.close(rw)
                rw = -1
                # The private TU advances only the owner CLOCK_MONOTONIC by 181s
                # just after real native service reports the first join loss.
                # Native transport/retry clocks remain real. A bad deadline reset
                # can therefore recover and publish READY, caught immediately.
                self.assertTrue(select.select([rr], [], [], 90 if startup.PREFIX else 10)[0],
                                'owner did not terminate at the expired original startup deadline')
                self.assertEqual(os.read(rr, 128), b'',
                    'READY published after the private owner clock passed the original startup deadline')
                stdout, stderr = proc.communicate(timeout=90 if startup.PREFIX else 10)
                self.assertEqual(proc.returncode, 1)
                self.assertEqual(stdout, b'')
                self.assertEqual(stderr, b'gin: startup timeout\n')
                self.assertEqual(srv.initial_failures, 1)
                self.assertEqual(srv.ws_attempts, 1, 'transport retried after the original deadline')
                self.assertEqual(srv.joins, ['grappa:user:Alice'])
                self.assertEqual(srv.paths, ['/api/config', '/me', '/boot', '/api/config', '/me',
                    '/socket/websocket?vsn=2.0.0&client_proto=36'], 'HTTP attempted after original expiry')
                self.assertEqual(output.read_bytes(), b'existing prefix\n')
                self.assertEqual(output.stat().st_ino, inode)
                self.assertEqual((root / '.gin.lock').stat().st_ino, lock_inode)
                self.assertEqual(credential.read_bytes(), old_credential)
                self.assertEqual({str(p.relative_to(root)) for p in root.rglob('*')}, old_tree)
            finally:
                cast(recovery.Server, srv).client_close_expected.set()
                if proc is not None:
                    if proc.poll() is None:
                        proc.kill()
                    proc.communicate(timeout=90 if startup.PREFIX else 10)
                os.close(rr)
                if rw >= 0:
                    os.close(rw)
            check = subprocess.run(self.command('init', '-d', root, '-n', 'NetA', '-c', '#Room'),
                                   capture_output=True, timeout=90 if startup.PREFIX else 5)
            self.assertEqual(check.returncode, 0, 'root lock not released')


if __name__ == '__main__':
    unittest.main()
