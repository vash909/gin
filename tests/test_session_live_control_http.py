"""Real JOIN must keep Native heartbeat ACKs and selected-room append live."""
import unittest
import test_session_live_http as held
import test_session_reconnect as reconnect


class JoinHandler(held.Handler):
    mutation_path = '/networks/NetA/channels'
    mutation_body = {'name': '#Other'}
    response_body = {'ok': True}
    response_status = 202


class LiveJoinHttpTracer(reconnect.ReconnectFixture):
    handler_class = JoinHandler
    read_operation = held.LiveHttpTracer.read_operation
    run_held = held.LiveHttpTracer.run_held

    def test_held_join_keeps_heartbeat_and_actual_selected_room_append_live(self):
        self.run_held(join=True)

    def test_signal_after_join_request_publishes_uncertain_before_graceful_stop(self):
        self.run_held(join=True, stop=True)


class PartHandler(held.Handler):
    mutation_method = 'DELETE'
    mutation_path = '/networks/NetA/channels/%23Other'
    response_body = {'ok': True}
    response_status = 202


class LivePartHttpTracer(reconnect.ReconnectFixture):
    handler_class = PartHandler
    read_operation = held.LiveHttpTracer.read_operation
    run_held = held.LiveHttpTracer.run_held

    def test_held_part_keeps_heartbeat_and_actual_selected_room_append_live(self):
        self.run_held(part=True)

    def test_signal_after_delete_request_publishes_uncertain_before_graceful_stop(self):
        self.run_held(part=True, stop=True)


if __name__ == '__main__':
    unittest.main()
