# Isolated verified-WSS connection and subscriptions

`include/gin_connect.h` / `src/connect.c` provide an owned, single-owner native
API, not a connected CLI. The local-only `gin run` remains unchanged. No real
Grappa revision has been qualified. Synthetic local fixtures are not live-server
interoperability evidence.

## Dependencies and lifecycle

The whole application now requires libcurl >=8.23.0 with SSL, asynchronous DNS,
and both WS/WSS protocols. Header and runtime checks fail closed; distribution
version numbers alone do not establish WebSocket support. There is no
libwebsockets dependency, direct TLS-backend API, or home-grown client framing.
The existing genuine-musl curl 8.23.0 recipe is unchanged. For older native Linux
packages, `scripts/build-host-curl.sh` builds the pinned official archive into
an explicit user prefix with OpenSSL and no system installation:

```sh
GIN_CURL_PREFIX="$HOME/.local/gin-curl" TMPDIR="$HOME/private-build" \
  sh scripts/build-host-curl.sh
make CURL_CONFIG="$HOME/.local/gin-curl/bin/curl-config" check
```

The caller owns `gin_http_global_init()` before start and cleanup after free.
`new` validates and deep-copies settings, credentials and the requested topic
plan without network access. Each explicit `start`, including after close or
failure, synchronously repeats anonymous `/api/config`, exact-origin and stored
UUID-bound `/me`, then verified same-authority WSS upgrade. Each request has its
own HTTP connect/total deadlines, not one aggregate deadline. Never invoke start
inside a poll callback or signal handler. No automatic storage, token rotation,
redirects, proxies, netrc, cookies, insecure TLS or signal-handler mutation.

`start` queues only the user-root join. `service(0..1000)` drains libcurl's
already-buffered TLS/WS data before at most one bounded poll, limits work to 64
operations and 1 MiB, and holds one outbound frame and one pending join. Short
writes retain the exact unsent tail. Join timeout covers queueing and reply,
independent of service wait budget. Uncertain writes fail without replay.

## Subscription and event policy

User root precedes implicit networks and their windows; exact constructed topics
are deduplicated within the total cap of 64. Topic subject and network case are
preserved, only target ASCII A-Z is folded. A target may contain slash. Topic
subscription is not IRC JOIN. The fresh authenticated identity supplies the
subject label, never a local nick or filesystem name.

A join requires exact topic, join reference and request reference, status `ok`,
and for root a positive integer `response.protocol_version >= R`. All required
joins must succeed before READY; any rejection, unknown reply status, active
channel error/close, binary message or transport failure fails the whole session.
Close clears active joined generations. Rejected status may remain visible.

Only joined-topic nonreply application frames reach the borrowed-frame callback.
Null join-reference broadcasts are allowed; nonnull stale generations are
ignored. `phx_reply` never reaches the application. Complete TEXT messages use
the existing explicit-FIN collector; ping/pong do not terminate partial text.
libcurl supplies WebSocket control handling, including automatic pong. No
Phoenix heartbeat, automatic backoff/reconnect, sender, renderer, history or
E2EE support is added here (roadmap 13 onward).

Explicit close is abortive/idempotent, not remote logout/PART. Explicit restart
repeats preflight and root-first joins, advances the attempt epoch and preserves
monotonic request refs for the whole session. No invented epoch bytes can
distinguish otherwise identical broadcasts. Free closes curl before releasing
headers/state and best-effort wipes owned secrets, not library allocator copies.

## Authentication encoding correction

The historical `base64url.bearer.phx.` prefix is **not** an alphabet specification.
Phoenix uses standard Base64 without terminal padding, not URL-safe Base64:
`aa~` becomes `YWF+`. The offer is `phoenix,base64url.bearer.phx.YWF+`, and the
selected subprotocol must be exactly `phoenix` once. Tokens never enter URLs,
Authorization upgrade headers or diagnostics. Source-reviewed Phoenix commit
`734c8d1c32253627e234c8a1cb831367191ff5dd`:

- `assets/js/phoenix/socket.js`, lines 398-399: browser `btoa`, remove `=`.
- `lib/phoenix/transports/websocket.ex`, lines 111-116: `Base.decode64!`, padding false.

The retained upstream documentation snapshot is unchanged. Gin's former
interpretation based only on the reference C client was incorrect for tokens
whose encoding differs between the two alphabets.

## Local tests and remaining qualification

`make check` includes native HTTPS config/identity followed by actual libcurl
WSS, root-first joins, duplicate plan entries, correlation and delayed FIN with
interleaved ping, root/child refusal, unknown status, binary/close/timeout,
403/426/302, selected-protocol guards, CA/SAN refusal, UUID binding and repeated
same-process preflight/reconnect with increasing refs. Private injected I/O
units supplement native tests with short/AGAIN sends and work/poll bounds.
`GIN_CONNECT_PREFIX` supports Valgrind; `GIN_CONNECT_DRIVER` selects a private
native build. No credentials or live endpoint are used. Real macOS/FreeBSD CI,
independent review and the final integrated gate remain required.
