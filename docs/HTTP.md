# Bounded REST/TLS adapter

The [verified-WSS native connection API](CONNECT.md) provides root-first
subscriptions through libcurl >=8.23.0. The [production session owner](SESSION.md)
composes verified startup and live selected-window plaintext reception in the
CLI. Connected FIFO send/control and recovery remain deferred; no live Grappa
interoperability is claimed.

## Actual scope

`include/gin_http.h` and `src/http.c` provide a synchronous, length-aware
GET/POST/DELETE transport. The separate [account authentication API](AUTH.md) adds bounded JSON
login/identity/logout; [HTTP discovery](DISCOVERY.md) adds protocol preflight
and gated authentication. This transport itself does not parse JSON, negotiate
protocols, implement WebSockets or deliver messages. Response bodies
are opaque bytes, including NUL. The installed native CLI includes `connect`,
`connect-check`, and `credential-import`; `gin run` remains local-only and
fails closed on sending. Low-level HTTP callbacks must remain bounded and
nonblocking; synchronous HTTP calls run outside runtime callbacks. Chat
projection, message sending, reconnection, history, and E2EE integration
remain deferred and require separately reviewed scheduling/lifecycle work.

## API and ownership

One application owner calls `gin_http_global_init()` before creating threads or
using libcurl, then `gin_http_global_cleanup()` once all requests and other
libcurl users have stopped. A failed init needs no cleanup. These wrappers are
not a per-request reference counter; do not clean up another library's live
libcurl state. No runtime descriptor or signal handler is acquired or replaced.
Every GET creates and destroys a fresh easy handle: no shared connection,
cookie, credential, redirect, HSTS or Alt-Svc state. Settings, URL, explicit CA
path and response storage remain caller-owned and valid for the entire call.
The caller must have successfully initialized the adapter first. The API does
not support concurrent global init/cleanup or calls from signal handlers.

The caller supplies 1..1048576 bytes of body storage, a 1..65536-byte aggregate
header limit, and positive millisecond connect/total deadlines. Connect must
not exceed total; total must not exceed 60000ms. Bounds are validated before
network access. Headers count status lines, CRLF separators, informational
responses and trailers together, not just the final block. Callbacks check
multiplication overflow and remaining capacity before copying/counting. No body
terminator is appended. The header limit bounds accepted bytes, not libcurl's
own bounded internal parsing buffers; one over-limit header may be received by
libcurl before the callback rejects it.

`gin_http_result` separates static typed outcomes from `http_status`, body length
and accepted header length. `OK` requires a completed 2xx response. Completed
3xx/4xx/5xx responses return `STATUS`, with their bounded opaque body available;
that is not transport/application success. Transport/TLS/timeout/limit failures
return zero body length and clear bytes already copied during that request.
Old bytes outside the returned length are never response data. The status may
be present on failure and must never be used alone to infer completion.
`gin_http_error()` returns fixed strings only. Neither the adapter nor its
errors print URLs, response headers, certificate peer details or private bodies.
The test-only driver intentionally emits synthetic fixture bodies; it is not
installed or used for production logging.

## URL, trust and isolation policy

Normal URLs must use lowercase `https://`; certificate chain validation and
hostname validation are always enabled, with TLS >=1.2. `ca_file == NULL` uses
the TLS backend's default trust configuration; an explicit PEM CA file supports
private trust/local fixtures without switching verification off. There is no
insecure flag. Manage process/TLS-backend environment and trust stores as trusted
application configuration; the adapter is not a sandbox for a hostile same-UID
process or TLS backend.

Raw controls, spaces, non-ASCII URL bytes, backslashes, fragments (even empty)
and URL userinfo are rejected. Percent-encode HTTP path segments independently
of filesystem names. URL length is at most 4096 bytes. No arbitrary header, Unix-socket or resolver override surface is exposed.
The separate explicit `gin_http_request` API supports GET, POST JSON and DELETE,
with a 16384-byte request body cap and optional 4096-byte bearer. It requires
HTTPS even on loopback and an exact authority-only `bearer_origin` match before
network access. `gin_http_get` and its existing settings retain unauthed semantics;
old callers never acquire uninitialized auth fields.

Only explicit `allow_loopback_http = 1` permits plain HTTP, and only a raw
canonical authority `127.0.0.1` or `[::1]`, optionally followed by a decimal
1..65535 port. This is a narrow local fixture/development policy, not a general
insecure default. It never permits `localhost`, alternate numeric spellings,
IPv6 zones, percent-encoded hosts or DNS-based loopback exceptions. Policy is
checked before URL normalization. Other settings values than 0/1 are invalid.

Protocol allowlists restrict transfers to HTTPS (plus that explicit local HTTP
policy). Redirect following and unrestricted authentication are disabled;
redirects return `STATUS` without contacting the destination, including HTTPS
to HTTP downgrades. Future authentication must preserve this no-follow contract
and add explicit origin-scoped credentials, never credentials in URLs or blind
redirect retries. Only the explicit request API accepts credentials. Proxy environment values
are overridden with an empty proxy; netrc and HTTP/proxy authentication are
disabled. Fresh handles do not enable a cookie engine. libcurl easy does not
read the curl command-line tool's `.curlrc`. No Accept-Encoding is requested and
content decoding is disabled, so compressed bodies remain opaque and cannot
expand past the body cap. Chunk transfer framing is decoded normally.

## Deadlines and signals

`CURLOPT_CONNECTTIMEOUT_MS` bounds connection establishment and
`CURLOPT_TIMEOUT_MS` covers the full transfer, including slow-drip responses,
not merely idle gaps. `CURLOPT_NOSIGNAL=1` preserves process signal ownership.
Initialization fails closed without libcurl's SSL and ASYNCHDNS features:
with NOSIGNAL a synchronous resolver cannot enforce DNS timeouts. A threaded
resolver may continue its own lookup after transfer timeout; deadlines are not
a hard real-time guarantee for resolver-thread teardown, OS scheduling, CA-file
I/O or global cleanup. Caller-provided CA files must be trusted regular files,
not blocking special files. libcurl attempts to suppress socket SIGPIPE without
altering handlers, but its documented platform corner cases still apply; any
process-wide SIGPIPE policy belongs to the application owner, not this adapter.

## Build and reproducibility

The standalone HTTP adapter retains its **>=7.85.0** floor, but the whole
application requires **>=8.23.0**, TLS, asynchronous DNS and WS/WSS for the
[connection API](CONNECT.md). `CURLOPT_PROTOCOLS_STR` and
`CURLOPT_REDIR_PROTOCOLS_STR` were added in 7.85.0; compile-time and runtime
version checks reject older libraries. Linux host coverage includes 7.88.1.
The executable still uses strict C99/POSIX make; authentication adds Jansson
>=2.14. See [AUTH.md](AUTH.md) for portable flag overrides.
`CURL_CONFIG` defaults to `curl-config`. Ordinary `make` still accepts an
explicit `CURL_CONFIG=` on its command line. `scripts/bootstrap.sh` and the
host builds in `scripts/check.sh` also honor an environment `CURL_CONFIG`
(default `curl-config`) and pass it explicitly to host `make`/`bmake`;
bootstrap checks `curl-config --checkfor 8.23.0` plus SSL/AsynchDNS/WS/WSS.
Use `CURL_CONFIG_MUSL` only for the genuine-musl gate in `scripts/check.sh`.
`CURL_CFLAGS` and `CURL_LIBS` may be
overridden explicitly, for example with matching `pkg-config --cflags libcurl`
and `pkg-config --libs libcurl` output. Use `--static` for static pkg-config
linking and restrict PKG_CONFIG_LIBDIR to the target toolchain. Never combine
musl-gcc with host glibc libcurl headers/libraries and call that musl coverage.

For a genuine user-prefix musl TLS toolchain, set absolute `GIN_MUSL_PREFIX`
and a private `TMPDIR`, then run `sh scripts/build-musl-http.sh`. This is a
Linux developer musl toolchain recipe, not a FreeBSD requirement. The recipe pins
curl 8.23.0, mbedTLS 3.6.7 and Jansson 2.15.1 release archives and SHA-256
digests, builds them with musl-gcc (at most two compile jobs), and installs
without sudo. It checks
upfront for curl, tar, xz, bzip2, sha256sum, cmake, make and musl-gcc. Then run:

```sh
make clean
make CC=musl-gcc CURL_CONFIG="$GIN_MUSL_PREFIX/bin/curl-config" \
  JANSSON_CFLAGS="-I$GIN_MUSL_PREFIX/include" \
  JANSSON_LIBS="-L$GIN_MUSL_PREFIX/lib -ljansson" LDFLAGS=-static check
CURL_CONFIG_MUSL="$GIN_MUSL_PREFIX/bin/curl-config" sh scripts/check.sh
```

The full gate requires CURL_CONFIG_MUSL explicitly and fails if missing; it
never silently skips the adapter or substitutes host glibc. CI installs curl
development packages on Linux/FreeBSD and builds the pinned real musl
TLS dependencies separately. Local HTTP/HTTPS Python-stdlib fixtures generate
ephemeral CA/SAN certificates with OpenSSL; subprocesses and sockets have
bounded timeouts and are cleaned up. NDEBUG does not disable C assertions.
`GIN_HTTP_PREFIX` is test-only and permits real Valgrind execution, including a
verified TLS request. No real credentials, Grappa services or public writes are
used, and local fixture results do not establish Grappa interoperability.

Official API references: [NOSIGNAL](https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html),
[total timeout](https://curl.se/libcurl/c/CURLOPT_TIMEOUT_MS.html),
[protocol allowlist](https://curl.se/libcurl/c/CURLOPT_PROTOCOLS_STR.html), and
[global lifecycle](https://curl.se/libcurl/c/curl_global_init.html).
