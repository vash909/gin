"""Actual retained WSS teardown with one private remove-before-detach failure."""
import os
import unittest
from unittest import mock
import test_session as startup
import test_session_liveness as live


class CleanupFaults(live.LivenessFixture):
    def command(self, *args):
        ordinary = os.environ.get('GIN_SESSION_BINARY', str(live.REPO / '.build/gin'))
        private = os.environ.get('GIN_NATIVE_FAULT_BINARY', str(live.REPO / '.build/gin_cleanup_fault'))
        binary = private if args and args[0] == 'connect' else ordinary
        return [*startup.PREFIX, binary, *map(str, args)]

    def test_remove_failure_after_live_ws_is_reported_and_reclaimed(self):
        with mock.patch.dict(os.environ, {'GIN_CONNECT_FAULT': 'remove-fail'}):
            self.run_case('positive', cleanup_error=True)


    def test_remove_failure_after_detach_is_reported_without_double_free(self):
        with mock.patch.dict(os.environ, {'GIN_CONNECT_FAULT': 'remove-fail-after'}):
            self.run_case('positive', cleanup_error=True)


if __name__ == '__main__':
    unittest.main()
