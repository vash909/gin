#!/usr/bin/env python3
"""Real loopback requests through the C API, never public services."""
import contextlib
import http.server
import os
from pathlib import Path
import shlex
import socket
import ssl
import struct
import subprocess
import tempfile
import threading
import time
from typing import Any, Optional, cast
import unittest

ROOT = Path(__file__).resolve().parents[1]
PREFIX = shlex.split(os.environ.get("GIN_HTTP_PREFIX", ""))


class FixtureHTTPServer(http.server.ThreadingHTTPServer):
    requests: list
    redirect: Optional[str]
    set_cookie: bool
    partial_second: bool


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def setup(self):
        super().setup()
        self.connection.settimeout(3)

    def do_GET(self):
        srv = cast(FixtureHTTPServer, self.server)
        srv.requests.append((self.command, self.path, dict(self.headers)))
        if srv.redirect is not None:
            self.send_response(302)
            self.send_header("Location", srv.redirect)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        if srv.set_cookie and len(srv.requests) == 1:
            self.send_header("Set-Cookie", "token=secret; Path=/")
        self.send_header("Content-Length", "5")
        self.end_headers()
        if srv.partial_second and len(srv.requests) == 2:
            self.wfile.write(b"xy")
            self.wfile.flush()
            self.close_connection = True
            return
        if srv.partial_second:
            self.wfile.write(b"abcde")
            return
        self.wfile.write(b"a\x00b\xffc")


@contextlib.contextmanager
def server(tls=None, redirect=None, set_cookie=False, partial_second=False):
    srv = FixtureHTTPServer(("127.0.0.1", 0), Handler)
    srv.daemon_threads = False
    srv.requests = []
    srv.redirect = redirect
    srv.set_cookie = set_cookie
    srv.partial_second = partial_second
    if tls:
        srv.socket = tls.wrap_socket(srv.socket, server_side=True)
    thread = threading.Thread(target=srv.serve_forever, kwargs={"poll_interval": 0.02})
    thread.start()
    try:
        yield srv
    finally:
        srv.shutdown()
        srv.server_close()
        thread.join(timeout=3)
        assert not thread.is_alive()


@contextlib.contextmanager
def raw_server(response, drip=False, reset=False):
    listener = socket.socket()
    listener.bind(("127.0.0.1", 0))
    listener.listen(1)
    listener.settimeout(2)
    stopped = threading.Event()

    def serve():
        try:
            conn, _ = listener.accept()
            with conn:
                conn.settimeout(2)
                request = b""
                while b"\r\n\r\n" not in request and len(request) < 8192:
                    part = conn.recv(4096)
                    if not part:
                        return
                    request += part
                if drip:
                    for byte in response:
                        if stopped.wait(0.015):
                            break
                        conn.sendall(bytes([byte]))
                else:
                    conn.sendall(response)
                if reset:
                    conn.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        except (OSError, TimeoutError):
            pass

    worker = threading.Thread(target=serve)
    worker.start()
    try:
        yield f"http://127.0.0.1:{listener.getsockname()[1]}/"
    finally:
        stopped.set()
        listener.close()
        worker.join(timeout=3)
        assert not worker.is_alive()


class HTTPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.scratch = tempfile.TemporaryDirectory(dir=Path(tempfile.gettempdir()).resolve())
        cls.certdir = Path(cls.scratch.name)

        def openssl(*args):
            subprocess.run(["openssl", *args], cwd=cls.certdir, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)

        for name in ["ca", "bad"]:
            openssl("req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                    "-subj", f"/CN=gin-{name}", "-keyout", name+".key", "-out", name+".pem")
        for name, san in [("good", "DNS:localhost,IP:127.0.0.1"), ("wrong", "DNS:wrong.invalid")]:
            openssl("req", "-newkey", "rsa:2048", "-nodes", "-subj", "/CN=fixture",
                    "-keyout", name+".key", "-out", name+".csr")
            (cls.certdir / (name+".ext")).write_text("subjectAltName="+san+"\nextendedKeyUsage=serverAuth\n")
            openssl("x509", "-req", "-in", name+".csr", "-CA", "ca.pem", "-CAkey", "ca.key",
                    "-CAcreateserial", "-days", "1", "-extfile", name+".ext", "-out", name+".pem")

    @classmethod
    def tearDownClass(cls):
        cls.scratch.cleanup()

    def tls(self, name="good"):
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.minimum_version = ssl.TLSVersion.TLSv1_2
        ctx.load_cert_chain(self.certdir / (name+".pem"), self.certdir / (name+".key"))
        return ctx

    def request(self, url, ca: Any="-", local=1, connect=500, total=1000,
                body=1024, headers=4096, env=None):
        p = subprocess.run(PREFIX + [str(ROOT / ".build/http_driver"), url,
            str(ca), str(local), str(connect), str(total), str(body), str(headers)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=12, env=env)
        self.assertEqual(p.returncode, 0, p.stderr.decode(errors="replace"))
        self.assertEqual(p.stderr, b"")
        meta, data = p.stdout.split(b"\n", 1)
        code, status, length, header_length = meta.decode().split()
        self.assertEqual(int(length), len(data))
        return code, int(status), data, int(header_length)

    def test_settings_and_url_policy(self):
        with server() as srv:
            url = f"http://127.0.0.1:{srv.server_port}/"
            self.assertEqual(self.request(url, local=0)[:3],
                             ("invalid-settings", 0, b""))
            for bad in [url + "#", url + "#secret", url.replace("127.0.0.1", "user:secret@127.0.0.1"),
                        url.replace("127.0.0.1", "localhost"), url.replace("127.0.0.1", "127.1"),
                        url.replace("127.0.0.1", "2130706433"), url.replace("127.0.0.1", "127.0.0.2"),
                        url.replace("127.0.0.1", "0x7f000001"), url.replace("127.0.0.1", "0177.0.0.1"),
                        url.replace("127.0.0.1", "localhost."), url.replace("127.0.0.1", "%31%32%37.0.0.1"),
                        url.replace("127.0.0.1", "[::1%25lo]"),
                        url.replace("127.0.0.1", "[::ffff:127.0.0.1]"),
                        "file:///etc/passwd", "ftp://127.0.0.1/", "gopher://127.0.0.1/",
                        "https://127.0.0.1/\\x01secret", "https://127.0.0.1/a b", "https://127.0.0.1/\\\\x"]:
                with self.subTest(url=bad):
                    self.assertEqual(self.request(bad)[:3], ("invalid-settings", 0, b""))
            for control in [1, 9, 10, 13, 31, 127]:
                self.assertEqual(self.request(url + chr(control) + "secret")[:3],
                                 ("invalid-settings", 0, b""))
            for kwargs in [dict(connect=0), dict(total=0), dict(total=60001),
                           dict(connect=1001), dict(body=0), dict(body=1048577),
                           dict(headers=0), dict(headers=65537), dict(local=2)]:
                with self.subTest(settings=kwargs):
                    self.assertEqual(self.request(url, **kwargs)[:3],
                                     ("invalid-settings", 0, b""))
            self.assertEqual(srv.requests, [])

    def test_http_status_and_empty(self):
        for status, expected in [(204, "ok"), (401, "http-status"), (404, "http-status"),
                                 (500, "http-status"), (302, "http-status")]:
            with self.subTest(status=status), raw_server(
                    f"HTTP/1.1 {status} Test\r\nContent-Length: 0\r\n\r\n".encode()) as url:
                self.assertEqual(self.request(url)[:3], (expected, status, b""))

    def test_body_header_caps_and_partial_failure(self):
        header = b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n"
        for cap, expected in [(5, "ok"), (4, "limit")]:
            with self.subTest(body=cap), raw_server(header + b"12345") as url:
                result = self.request(url, body=cap)
                self.assertEqual(result[0], expected)
                self.assertEqual(result[2], b"12345" if cap == 5 else b"")
        for cap, expected in [(len(header), "ok"), (len(header)-1, "limit")]:
            with self.subTest(headers=cap), raw_server(header + b"12345") as url:
                result = self.request(url, headers=cap)
                self.assertEqual(result[0], expected)
                self.assertEqual(result[2], b"12345" if expected == "ok" else b"")
        for data in [header + b"12", b"not HTTP\r\nsecret", b"HTTP/1.1 nope\r\n\r\n",
                     b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\n12"]:
            with self.subTest(data=data), raw_server(data) as url:
                self.assertEqual(self.request(url)[0::2], ("transport", b""))
        with raw_server(header + b"12", reset=True) as url:
            self.assertEqual(self.request(url)[0::2], ("transport", b""))
        interim = b"HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 103 Early Hints\r\nX-A: value\r\n\r\n"
        with raw_server(interim + header + b"12345") as url:
            self.assertEqual(self.request(url, headers=len(header))[0], "limit")
        with raw_server(b"HTTP/1.1 200 OK\r\nX-Huge: " + b"x"*8000 + b"\r\n\r\n") as url:
            self.assertEqual(self.request(url)[0], "limit")
        chunked = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\na\x00b\r\n2\r\ncd\r\n0\r\n\r\n"
        with raw_server(chunked) as url:
            self.assertEqual(self.request(url, body=5)[:3], ("ok", 200, b"a\x00bcd"))
        with raw_server(chunked) as url:
            self.assertEqual(self.request(url, body=4)[0::2], ("limit", b""))

    def test_many_headers_trailers_and_no_content_decoding(self):
        header = b"HTTP/1.1 200 OK\r\n" + b"X-Small: x\r\n" * 100 + b"Content-Length: 0\r\n\r\n"
        with raw_server(header) as url:
            self.assertEqual(self.request(url, headers=len(header))[0], "ok")
        with raw_server(header) as url:
            self.assertEqual(self.request(url, headers=len(header)-1)[0::2], ("limit", b""))
        header = b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
        with raw_server(header + b"1\r\nx\r\n0\r\nX-Trailer: secret\r\n\r\n") as url:
            self.assertEqual(self.request(url, headers=len(header))[0::2], ("limit", b""))
        compressed = b"not-a-valid-gzip-stream"
        header = b"HTTP/1.1 200 OK\r\nContent-Encoding: gzip\r\nContent-Length: 23\r\n\r\n"
        with raw_server(header + compressed) as url:
            self.assertEqual(self.request(url)[0::2], ("ok", compressed))

    def test_connect_deadline_during_tls_handshake(self):
        with raw_server(b"") as url:
            start = time.monotonic()
            result = self.request(url.replace("http:", "https:"), connect=100, total=1000)
            self.assertEqual(result[0::2], ("timeout", b""))
            self.assertLess(time.monotonic() - start, 1)

    def test_total_deadline_and_refused_connection(self):
        start = time.monotonic()
        with raw_server(b"HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\n12345", drip=True) as url:
            self.assertEqual(self.request(url, connect=100, total=120)[0::2], ("timeout", b""))
        self.assertLess(time.monotonic() - start, 3)
        with socket.socket() as reserved:
            reserved.bind(("127.0.0.1", 0))
            port = reserved.getsockname()[1]
        # Close before connecting: bound-but-not-listening need not send RST.
        url = f"http://127.0.0.1:{port}/"
        self.assertEqual(self.request(url)[0::2], ("transport", b""))

    def test_tls_trust_and_hostname(self):
        ca = self.certdir / "ca.pem"
        # Instrumented OpenSSL setup needs a separate fixture budget.
        # Explicit timeout tests retain their original settings.
        budget: dict[str, Any] = dict(connect=3000, total=6000) if PREFIX else {}
        with server(self.tls()) as srv:
            url = f"https://127.0.0.1:{srv.server_port}/"
            self.assertEqual(self.request(url, ca=ca, local=0, **budget)[:3], ("ok", 200, b"a\x00b\xffc"))
            self.assertEqual(self.request(url, ca=self.certdir / "bad.pem", **budget)[0::2], ("tls", b""))
            self.assertEqual(self.request(url, **budget)[0::2], ("tls", b""))
            self.assertEqual(self.request(url, ca=self.certdir / "missing.pem", **budget)[0::2], ("tls", b""))
            self.assertEqual(len(srv.requests), 1)
        with server(self.tls("wrong")) as srv:
            self.assertEqual(self.request(f"https://127.0.0.1:{srv.server_port}/", ca=ca, **budget)[0::2],
                             ("tls", b""))
            self.assertEqual(srv.requests, [])

    def test_redirect_no_second_request_or_ambient_credentials(self):
        with server() as target:
            location = f"http://127.0.0.1:{target.server_port}/secret"
            with raw_server(("HTTP/1.1 302 Found\r\nLocation: " + location +
                             "\r\nSet-Cookie: token=secret\r\nContent-Length: 0\r\n\r\n").encode()) as url:
                self.assertEqual(self.request(url)[:3], ("http-status", 302, b""))
            self.assertEqual(target.requests, [])
            with server(self.tls(), redirect=location) as origin:
                self.assertEqual(self.request(f"https://127.0.0.1:{origin.server_port}/",
                                             ca=self.certdir / "ca.pem")[0], "http-status")
            self.assertEqual(target.requests, [])
            home = self.certdir / "home"
            home.mkdir(exist_ok=True)
            (home / ".netrc").write_text("machine 127.0.0.1 login private password secret\n")
            (home / ".netrc").chmod(0o600)
            (home / ".curlrc").write_text("user = private:secret\ninsecure\n")
            env = dict(os.environ, HOME=str(home), CURL_HOME=str(home),
                       ALL_PROXY="http://127.0.0.1:1", all_proxy="http://127.0.0.1:1",
                       http_proxy="http://127.0.0.1:1", HTTPS_PROXY="http://127.0.0.1:1",
                       NO_PROXY="", no_proxy="", GIN_HTTP_REPEAT="1")
            self.assertEqual(self.request(location, env=env)[0], "ok")
            self.assertEqual(len(target.requests), 2)
            for method, path, headers in target.requests:
                self.assertEqual(method, "GET")
                self.assertNotIn("Authorization", headers)
                self.assertNotIn("Proxy-Authorization", headers)
                self.assertNotIn("Cookie", headers)
                self.assertNotIn("Accept-Encoding", headers)

    def test_failed_partial_response_clears_reused_storage(self):
        env = dict(os.environ, GIN_HTTP_REPEAT="1", GIN_HTTP_EXPECT_ZEROED_PREFIX="2")
        with server(partial_second=True) as srv:
            result = self.request(f"http://127.0.0.1:{srv.server_port}/", env=env)
            self.assertEqual(result[:3], ("transport", 200, b""))
            self.assertEqual(len(srv.requests), 2)

    def test_set_cookie_does_not_persist_between_requests(self):
        env = dict(os.environ, GIN_HTTP_REPEAT="1")
        with server(set_cookie=True) as srv:
            result = self.request(f"http://127.0.0.1:{srv.server_port}/", env=env)
            self.assertEqual(result[:3], ("ok", 200, b"a\x00b\xffc"))
            self.assertEqual(len(srv.requests), 2)
            for method, path, headers in srv.requests:
                self.assertNotIn("Cookie", headers)

    def test_get_opaque_body(self):
        with server() as srv:
            result = self.request(f"http://127.0.0.1:{srv.server_port}/api/config")
            self.assertEqual(result[:3], ("ok", 200, b"a\x00b\xffc"))
            self.assertEqual(len(srv.requests), 1)
            self.assertEqual(srv.requests[0][0:2], ("GET", "/api/config"))


if __name__ == "__main__":
    unittest.main(verbosity=2)
