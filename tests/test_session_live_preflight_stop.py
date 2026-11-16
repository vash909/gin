"""A ready-state HTTP preflight stop never enters its mutation request."""
import unittest
from typing import cast
import test_session as startup
import test_session_live_http as held
import test_session_reconnect as reconnect

BOOT = ['/api/config', '/me', '/boot', '/api/config', '/me',
        '/socket/websocket?vsn=2.0.0&client_proto=36', '/api/config', '/me',
        '/networks/NetA/channels/%23Room/messages?limit=200&after=0']


class ConfigHandler(held.Handler):
    preflight_path = '/api/config'

    def do_GET(self):
        srv = cast(held.Server, self.server)
        if srv.ready_observed.is_set() and self.path == self.preflight_path:
            prefix = [] if self.preflight_path == '/api/config' else ['/api/config']
            assert srv.paths == BOOT + prefix
            expected_auth = None if self.preflight_path == '/api/config' else 'Bearer ' + startup.TOKEN
            assert self.headers.get('Authorization') == expected_auth
            srv.paths.append(self.path)
            srv.posted.set()
            assert srv.release_post.wait(90), 'held preflight barrier expired'
            # Stop must finish without any response headers/body. Failure cleanup
            # kills and reaps the child before releasing this barrier.
            self.close_connection = True
            return
        super().do_GET()

    def do_POST(self):
        raise AssertionError('POST entered after aborted preflight')

    def do_DELETE(self):
        raise AssertionError('DELETE entered after aborted preflight')


class IdentityHandler(ConfigHandler):
    preflight_path = '/me'


class LivePreflightStop(reconnect.ReconnectFixture):
    handler_class = ConfigHandler
    read_operation = held.LiveHttpTracer.read_operation
    run_held = held.LiveHttpTracer.run_held

    def run_preflight(self, kind, path):
        self.handler_class = ConfigHandler if path == '/api/config' else IdentityHandler
        self.run_held(stop=True, join=kind == 'JOIN', part=kind == 'PART', preflight=path)

    def test_send_config_stop_reports_not_attempted(self):
        self.run_preflight('SEND', '/api/config')

    def test_send_identity_stop_reports_not_attempted(self):
        self.run_preflight('SEND', '/me')

    def test_join_config_stop_reports_not_attempted(self):
        self.run_preflight('JOIN', '/api/config')

    def test_join_identity_stop_reports_not_attempted(self):
        self.run_preflight('JOIN', '/me')

    def test_part_config_stop_reports_not_attempted(self):
        self.run_preflight('PART', '/api/config')

    def test_part_identity_stop_reports_not_attempted(self):
        self.run_preflight('PART', '/me')


if __name__ == '__main__':
    unittest.main()
