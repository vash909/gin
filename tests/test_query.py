"""Actual libcurl query API over the existing ephemeral TLS authority."""
import base64
import contextlib
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile
import threading
import unittest
import test_connect as connect
from ws_fixture_frames import server_frame, take_client_frame

class Handler(connect.Handler):
    def do_GET(self):
        if self.path == "/api/config" and self.server.mode == "config-floor":
            self.server.records.append((self.path, dict(self.headers)))
            raw = json.dumps(dict(connect.CONFIG, protocol_version=3)).encode()
            self.send_response(200); self.send_header("Content-Length", str(len(raw))); self.end_headers(); self.wfile.write(raw)
        else:
            super().do_GET()

    def upgrade(self):
        srv = self.server
        srv.upgrades += 1
        key = self.headers["Sec-WebSocket-Key"]
        accept = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        self.send_response(101); self.send_header("Upgrade", "websocket"); self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept); self.send_header("Sec-WebSocket-Protocol", "phoenix"); self.end_headers(); self.wfile.flush()
        self.connection.settimeout(3)
        buf = bytearray()
        def read():
            while True:
                item = take_client_frame(buf)
                if item:
                    if item[0] == 10: continue
                    assert item[0] == 1 and item[1]
                    return json.loads(item[2])
                data = self.connection.recv(16384)
                if not data: return None
                buf.extend(data)
        def emit(f): self.connection.sendall(server_frame(json.dumps(f).encode()))
        try:
            join = read(); srv.joins.append(join)
            jr, ref, topic, event, body = join
            assert event == "phx_join" and jr == ref
            emit([jr, ref, topic, "phx_reply", {"status":"ok", "response":{"protocol_version":3 if srv.mode == "root-floor" else 7}}])
            query = read()
            if query is None: return
            srv.queries.append(query)
            active_ref = "4" if srv.mode == "reconnect" and srv.upgrades == 2 else "2"
            assert query == [jr, active_ref, topic, "open_query_window", {"network_id":7, "target_nick":"Bob"}]
            reply = [jr, active_ref, topic, "phx_reply", {"status":"ok", "response":{}}]
            snapshot = [None, None, topic, "event", {"kind":"query_windows_list", "windows":{"7":[{"network_id":7,"target_nick":"Bob","opened_at":"fixture"}]}}]
            if srv.mode == "before": emit(snapshot)
            if srv.mode == "correlated":
                for slot, value in [(0,None),(0,"old"),(1,None),(1,"wrong"),(2,"foreign"),(1,jr)]:
                    wrong = list(reply); wrong[slot] = value; wrong[4] = {"status":"error", "response":{}}; emit(wrong)
            if srv.mode in ("error", "unknown"): reply[4]["status"] = "error" if srv.mode == "error" else "future"
            if srv.mode == "malformed": reply[4]["response"] = None
            if srv.mode == "disconnect" or (srv.mode == "reconnect" and srv.upgrades == 1): return
            if srv.mode == "reconnect": emit([srv.joins[0][0], "2", topic, "phx_reply", {"status":"error", "response":{}}])
            if srv.mode != "silent": emit(reply)
            if srv.mode == "correlated": emit([jr,"2",topic,"phx_reply",{"status":"error","response":{}}])
            if srv.mode == "after": emit(snapshot)
            if srv.mode in ("before", "after"):
                emit([None,None,topic,"event",{"kind":"query_windows_list","windows":{}}])
            while self.connection.recv(1024): pass
        except (OSError, ValueError): pass
        finally: self.close_connection = True

class QueryTests(unittest.TestCase):
    certdir: Path
    setUpClass = classmethod(connect.ConnectTests.setUpClass.__func__)
    tearDownClass = classmethod(connect.ConnectTests.tearDownClass.__func__)
    tls = connect.ConnectTests.tls
    @contextlib.contextmanager
    def fixture(self, mode):
        srv = connect.Server(("127.0.0.1",0), Handler)
        srv.mode = mode; srv.records = []; srv.joins = []; srv.queries = []; srv.upgrades = 0
        srv.socket = self.tls("good").wrap_socket(srv.socket, server_side=True)
        t = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval":.02}); t.start()
        try: yield srv, f"https://127.0.0.1:{srv.server_port}"
        finally: srv.shutdown(); srv.server_close(); t.join(3); self.assertFalse(t.is_alive())
    def test_query_matrix(self):
        for mode in ("ok","correlated","before","after","error","unknown","malformed","silent","disconnect","config-floor","root-floor","rejected","reconnect"):
            with self.subTest(mode=mode), self.fixture(mode) as (srv, origin):
                binary = os.environ.get("GIN_QUERY_DRIVER", str(connect.ROOT / ".build/query_driver"))
                with tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary).resolve() / "root"
                    p = subprocess.Popen(shlex.split(os.environ.get("GIN_QUERY_PREFIX","")) +
                        [binary,origin,str(self.certdir / "ca.pem"),mode,str(root)],
                        stdout=subprocess.PIPE,stderr=subprocess.PIPE)
                    try:
                        assert p.stdout is not None
                        self.assertEqual(p.stdout.readline(), b"fifo-ready\n")
                        fifo = root / "%4Eet" / "room" / "in"
                        fd = os.open(fifo, os.O_WRONLY)
                        try:
                            record = b"/raw JOIN #Room\n" if mode == "rejected" else b"/query Bob\n"
                            self.assertEqual(os.write(fd, record), len(record))
                        finally:
                            os.close(fd)
                        out, err = p.communicate(timeout=35)
                        self.assertEqual(p.returncode,0,err.decode(errors="replace")); self.assertEqual(err,b"")
                        self.assertEqual(out.decode().strip(),f"query {mode} verified")
                    finally:
                        if p.poll() is None:
                            p.kill(); p.communicate()
                expected = 0 if mode.endswith("floor") or mode == "rejected" else (2 if mode == "reconnect" else 1)
                self.assertEqual(len(srv.queries), expected)
                self.assertEqual(srv.upgrades, 2 if mode == "reconnect" else 1)
                self.assertNotIn("Authorization",srv.records[0][1])
                self.assertEqual([r[0] for r in srv.records][:2],["/api/config","/me"])
if __name__ == "__main__": unittest.main(verbosity=2)
