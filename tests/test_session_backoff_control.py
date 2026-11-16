"""Deterministic private-owner controls at the actual backoff poll boundary."""
import unittest
import test_session_reconnect as recovery


class BackoffControls(recovery.ReconnectFixture):
    def test_signal_at_backoff_poll_stops_before_fresh_http(self):
        self.run_case('backoff_stop')

    def test_actual_stdin_eof_at_backoff_poll_stops_before_fresh_http(self):
        self.run_case('backoff_eof')

    def test_stdin_byte_at_backoff_poll_is_not_a_command(self):
        self.run_case('backoff_input')


if __name__ == '__main__':
    unittest.main()
