"""Offline native-curl recipe guards; fake downloader, no installation/network."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

SCRIPT = Path(os.environ.get("GIN_HOST_CURL_SCRIPT", Path(__file__).resolve().parents[1] / "scripts/build-host-curl.sh")).resolve()


class RecipeGuards(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.bin = self.root / "bin"
        self.bin.mkdir()
        fake = self.bin / "curl"
        fake.write_text('#!/bin/sh\nprintf "%s\\n" "$PWD" >> "$FAKE_CURL_LOG"\nexit 90\n')
        fake.chmod(0o700)
        self.log = self.root / "download.log"
        self.tmp = self.root / "build"
        self.tmp.mkdir()
        self.env = dict(os.environ, PATH=str(self.bin) + os.pathsep + os.environ["PATH"],
                        TMPDIR=str(self.tmp), FAKE_CURL_LOG=str(self.log))

    def run_recipe(self, prefix):
        env = dict(self.env, GIN_CURL_PREFIX=str(prefix))
        return subprocess.run(["sh", str(SCRIPT)], env=env, capture_output=True, text=True, timeout=10)

    def test_system_prefix_rejected_before_download(self):
        result = self.run_recipe("/usr/local")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists(), "system prefix reached downloader")

    def test_relative_prefix_rejected_before_download(self):
        result = self.run_recipe("relative-prefix")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())

    def test_fixed_old_build_symlink_never_followed(self):
        outside = self.root / "outside"
        outside.mkdir()
        (self.tmp / "gin-host-curl-8.23.0").symlink_to(outside, target_is_directory=True)
        result = self.run_recipe(self.root / "prefix")
        self.assertNotEqual(result.returncode, 0)  # Fake downloader deliberately stops.
        self.assertTrue(self.log.exists(), "valid private prefix never reached fake downloader")
        paths = [Path(line).resolve() for line in self.log.read_text().splitlines()]
        self.assertTrue(paths)
        self.assertTrue(all(self.tmp in path.parents for path in paths), "build escaped TMPDIR")
        self.assertEqual(list(outside.iterdir()), [])

    def test_symlink_prefix_rejected_before_download(self):
        link = self.root / "prefix-link"
        link.symlink_to("/usr/local", target_is_directory=True)
        result = self.run_recipe(link)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
