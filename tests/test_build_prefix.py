"""Exercise only the native curl builder's pre-download prefix validation."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD_SCRIPT = ROOT / "scripts/build-host-curl.sh"


class BuildPrefixTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name).resolve()
        source = BUILD_SCRIPT.read_text()
        marker = "python3 - \"$GIN_CURL_PREFIX\" \"$TMPDIR\" <<'PY'\n"
        self.assertEqual(source.count(marker), 1)
        validator = source.split(marker, 1)[1].split("\nPY\n", 1)[0]
        self.validator = self.root / "validate.py"
        self.validator.write_text(validator)

    def validate(self, prefix):
        return subprocess.run([sys.executable, str(self.validator), str(prefix),
                               str(self.root)], capture_output=True, text=True,
                              timeout=5)

    def test_private_prefix_is_accepted_without_creation(self):
        prefix = self.root / "native-curl"
        result = self.validate(prefix)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse(prefix.exists())

    def test_deployment_subtree_is_rejected_as_system_prefix(self):
        result = self.validate("/opt/shared-tools/native-curl")
        self.assertEqual(result.returncode, 1)
        self.assertIn("must not be a system prefix", result.stderr)

    def test_system_roots_are_rejected(self):
        for prefix in ("/", "/usr", "/usr/local", "/opt", "/tmp"):
            with self.subTest(prefix=prefix):
                result = self.validate(prefix)
                self.assertEqual(result.returncode, 1)
                self.assertIn("must not be a system prefix", result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
