"""Ownership rejection through the actual CLI; isolated privileged CI only."""
import os
from contextlib import AbstractContextManager
from pathlib import Path
from typing import Any
import unittest

import test_session_failures as negative


@unittest.skipUnless(os.geteuid() == 0, 'Foreign-owner cases require isolated privileged CI')
class SessionPermissions(unittest.TestCase):
    certdir: Path
    case: Any
    base: Path
    root: Path
    settings: Path
    tls = negative.SessionFailures.tls
    command = negative.SessionFailures.command
    setUp = negative.SessionFailures.setUp

    store = negative.SessionFailures.store
    args = negative.SessionFailures.args
    quiet = negative.SessionFailures.quiet
    run_cli = negative.SessionFailures.run_cli

    def fixture(self) -> AbstractContextManager[tuple[negative.NegativeServer, str]]:
        helper = negative.SessionFailures()
        helper.certdir = self.certdir
        return helper.fixture()

    @classmethod
    def setUpClass(cls) -> None:
        negative.SessionFailures.setUpClass.__func__(cls)

    @classmethod
    def tearDownClass(cls) -> None:
        negative.SessionFailures.tearDownClass.__func__(cls)

    def test_foreign_config_fails_before_root_or_requests(self) -> None:
        with self.fixture() as (srv, origin):
            os.chown(self.settings, 65534, -1)
            try:
                self.quiet(self.run_cli(self.args(origin)), 1)
                self.assertFalse(self.root.exists())
                self.assertEqual(srv.paths, [])
            finally:
                os.chown(self.settings, os.geteuid(), -1)

    def test_foreign_bearer_import_fails_before_root_or_requests(self) -> None:
        with self.fixture() as (srv, origin):
            source = self.base / 'bearer'
            source.write_bytes(negative.TOKEN.encode())
            source.chmod(0o600)
            os.chown(source, 65534, -1)
            try:
                self.quiet(self.run_cli(['credential-import', '-d', str(self.root),
                    '-e', origin, '-s', str(source), '-a', str(self.certdir / 'ca.pem')]), 1)
                self.assertFalse(self.root.exists())
                self.assertEqual(srv.paths, [])
            finally:
                os.chown(source, os.geteuid(), -1)

    def test_foreign_store_fails_without_rebinding_or_requests(self) -> None:
        with self.fixture() as (srv, origin):
            stored = self.store(origin)
            before = stored.read_bytes()
            os.chown(stored, 65534, -1)
            try:
                self.quiet(self.run_cli(self.args(origin)), 1)
                self.assertEqual(srv.paths, [])
                self.assertTrue(stored.read_bytes() == before, 'foreign store was modified')
                self.assertEqual(stored.stat().st_uid, 65534)
            finally:
                os.chown(stored, os.geteuid(), -1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
