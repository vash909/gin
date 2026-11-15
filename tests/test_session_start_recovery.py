"""Initial native preflight transport failure and subsequent online recovery."""
import unittest
import test_session_reconnect as recovery


class StartRecovery(recovery.ReconnectFixture):
    def test_initial_native_transport_failure_uses_same_lifetime_retry_budget(self):
        self.run_case('initial_close')

    def test_pre_ready_join_transport_loss_recovers_with_same_lifetime_budget(self):
        self.run_case('joining_close')

    def test_initial_topology_transport_failure_shares_total_retry_budget_with_native(self):
        self.run_case('topology_close')

    def test_all_sixteen_bootstrap_retries_leave_zero_native_retries(self):
        self.run_case('topology_sixteen')


if __name__ == '__main__':
    unittest.main()
