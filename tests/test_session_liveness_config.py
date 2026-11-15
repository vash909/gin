"""Read-only production CLI liveness-policy configuration, without network I/O."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get('GIN_SESSION_BINARY', str(REPO / '.build/gin'))).resolve()
PREFIX = shlex.split(os.environ.get('GIN_SESSION_PREFIX', ''))


class PolicyConfigTests(unittest.TestCase):
    def check_policy(self, policy, expected):
        scratch = Path(os.environ.get('TMPDIR', str(REPO / '.build/test-scratch')))
        scratch.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='gin-policy-', dir=scratch) as directory:
            base = Path(directory).resolve()
            root = base / 'must-not-exist'
            config = base / 'policy'
            config.write_text(policy)
            config.chmod(0o600)
            result = subprocess.run([*PREFIX, str(BINARY), 'connect-check', '-f', str(config),
                '-d', str(root), '-n', 'NetA', '-c', '#Room', '-e', 'https://127.0.0.1:9',
                '-m', 'plain', '-a', str(base / 'absent-ca.pem')],
                capture_output=True, timeout=30 if PREFIX else 5)
            self.assertEqual(result.returncode, expected,
                             'read-only CLI rejected the explicit bounded liveness policy: '
                             + repr(result.stderr))
            self.assertEqual(result.stdout, b'')
            if expected == 0:
                self.assertEqual(result.stderr, b'')
            self.assertFalse(root.exists(), 'read-only policy check mutated the root')
            self.assertEqual(sorted(p.name for p in base.iterdir()), ['policy'])


    def test_read_only_check_accepts_explicit_liveness_policy(self):
        self.check_policy('heartbeat_interval_ms=150\nheartbeat_timeout_ms=500\n'
                          'retry_base_ms=20\nretry_cap_ms=100\nmax_retries=0\n', 0)

    def test_read_only_check_accepts_exact_native_policy_maxima(self):
        self.check_policy('heartbeat_interval_ms=60000\nheartbeat_timeout_ms=60000\n'
                          'retry_base_ms=60000\nretry_cap_ms=60000\nmax_retries=16\n', 0)

    def test_bad_policy_is_rejected_without_filesystem_or_ca_probes(self):
        cases = [
            'heartbeat_interval_ms=0\n', 'heartbeat_interval_ms=60001\n',
            'heartbeat_timeout_ms=0\n', 'heartbeat_timeout_ms=60001\n',
            'retry_base_ms=0\n', 'retry_cap_ms=60001\n',
            'retry_base_ms=101\nretry_cap_ms=100\n',
            'max_retries=17\n', 'max_retries=-1\n', 'max_retries=00\n',
            'heartbeat_interval_ms=01\n',
            'heartbeat_timeout_ms=999999999999999999999999999999\n',
            'heartbeat_interval_ms=150\nheartbeat_interval_ms=150\n',
        ]
        for policy in cases:
            with self.subTest(policy=policy):
                self.check_policy(policy, 2)


if __name__ == '__main__':
    unittest.main()
