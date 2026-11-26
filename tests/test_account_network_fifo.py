#!/usr/bin/env python3
"""Network FIFO JOIN/PART tracers; acceptance is not membership or local removal."""
import unittest
import os
import test_account_fifo_send as fifo


class NetworkFIFO(fifo.FIFOFixture):
    def test_network_fifo_query_open(self):
        self.run_channel(False, network_query=True)

    def test_network_fifo_part(self):
        self.run_channel(False, network_part=True)

    def test_network_fifo_join(self):
        self.run_channel(False, network_join=True)


class NetworkFIFOCharacterization(fifo.FIFOFixture):
    """Post-implementation typed cancellation characterization, not original RED."""
    @unittest.skipUnless(os.environ.get('GIN_FIFO_OBSERVER'), 'private typed observer required')
    def test_network_fifo_query_held_stop(self):
        self.run_channel(True, network_query=True)

    @unittest.skipUnless(os.environ.get('GIN_FIFO_OBSERVER'), 'private typed observer required')
    def test_network_fifo_part_held_stop(self):
        self.run_channel(True, network_part=True)

    @unittest.skipUnless(os.environ.get('GIN_FIFO_OBSERVER'), 'private typed observer required')
    def test_network_fifo_held_stop(self):
        self.run_channel(True, network_join=True)


if __name__ == '__main__':
    unittest.main(verbosity=2)
