# Isolated HTTP protocol discovery and gated authentication

## Scope and caller policy

`include/gin_protocol.h`, `src/protocol_parse.c` and `src/protocol.c` implement
bounded discovery and comparison, and composition with the existing native
account authentication APIs. They do not connect the CLI or foreground runtime,
open WebSockets, discover networks/windows, send messages or implement E2EE.
No gin wire declaration is inferred from the pinned server's current version.

The caller explicitly supplies positive integer `client_protocol` (`C`) and
`required_server_protocol` (`R`). Discovery requires an object with exact
`server: "grappa"`, positive integer `protocol_version` (`S`) and
`min_protocol_version` (`M`), both at most `INT_MAX`, with `M <= S`.
Compatibility requires **both `C >= M` and `S >= R`**. There is no equality
requirement or `C`/`R` ordering requirement: newer servers remain acceptable,
and a newer client declaration does not make an older server meet its floor.
`R` is the aggregate floor for everything the caller requires.

`gin_protocol_server_at_least(info, introduced)` compares only the server
protocol against a positive introduction floor. It does not claim that gin
implements that feature. Optional software `version` is diagnostic only; it is
copied only when it is a safe UTF-8 string of at most 255 bytes without C0/C1
controls or DEL. Otherwise it remains empty. Software releases, Phoenix
serializer `vsn`, and Web Push `push_content_encoding` are not Grappa protocol
compatibility gates or evidence of E2EE support.

The source-reviewed endpoint is `ConfigController.show/2` at the full revision
listed in [PROTOCOL.md](PROTOCOL.md), alongside `Grappa.Protocol` and
`UserSocket.check_protocol_version/1`. The captured 63/1 values describe that
upstream revision only, not constants in this implementation. The proposed
future `/boot` floor 4 is a design choice, not a claim of implemented `/boot`.

## Fetch and failure boundaries

After the existing application-owned `gin_http_global_init()`, call
`gin_protocol_fetch(settings, policy, out)`. The settings are the same
`gin_auth_settings` used by authentication: exact authority-only HTTPS origin,
CA selection, connect deadline and total deadline. A fresh unauthenticated
verified-TLS GET targets only `/api/config`; strict completed HTTP 200 is
required. No cache, retry, redirects, proxy, netrc, cookies, bearer or request
body is used. The shared [HTTP lifecycle and trust policy](HTTP.md) is unchanged.
The synchronous calls must not run inside runtime poll callbacks.

Bodies cap at 65536 bytes and aggregate headers at 65536 bytes. A string-aware
preflight bounds JSON nesting to 32 before Jansson decoding. JSON must consume
the complete input, be valid UTF-8, and have no embedded NUL or duplicate keys,
including escaped duplicates. Floats, booleans, strings, nonpositive values and
integer overflow cannot stand in for versions. Bounded unknown fields are
ignored, without bypassing whole-document validity checks.

Every valid info output is zeroed on failure. Errors are typed and static:
invalid settings, malformed config, unexpected server, either unsatisfied
floor, TLS, timeout, limit, transport and remote status. No peer bytes, response
bodies, URLs or credentials are logged. Own buffers are wiped best-effort, not
a guarantee about library/allocator copies. Inputs and output must not alias.

## Recommended gated authentication path

Prefer `gin_protocol_login()` or `gin_protocol_resume()` over composing the
low-level calls manually. Each call clears credential output, performs a new
same-origin discovery using the same settings, and only after compatibility
success invokes the respective existing authentication API once. There is no
implicit persistence or automatic credential retry. `gin_protocol_resume()`
imports an explicitly supplied bearer; the caller must bind that input to the
intended origin. It is **not** a substitute for validating a stored credential:
perform fresh discovery with the same settings, then use `gin_auth_identity()`
to retain its exact-origin and stored-subject UUID checks. Likewise perform
fresh discovery before the existing bound-credential logout API. No stored
credential is implicitly loaded, rebound or replaced by these wrappers.

The returned `gin_protocol_auth_result` separates phases:

- `protocol != GIN_PROTOCOL_OK`: authentication was not attempted,
  `auth_attempted == 0`, credential and server outputs are empty, and `auth`
  must not be interpreted as an authentication result.
- `protocol == GIN_PROTOCOL_OK`: the server snapshot is available and
  `auth_attempted == 1`; **authentication succeeds only when `auth ==
  GIN_AUTH_OK`**. In particular 401 and unsupported second-factor responses
  remain authentication failures with cleared credential output. The flag means
  the low-level authentication API was invoked, not that an HTTP authentication
  request necessarily reached the wire: local input validation can still return
  `GIN_AUTH_INVALID` without sending that request.

The tested sequential flows are `GET /api/config` then `POST /auth/login`, or
`GET /api/config` then authenticated `GET /me`. Rejected discovery sends only
the unauthenticated GET; invalid settings/policy send nothing. Repeating a
wrapper repeats discovery, even if a previous call succeeded. The low-level
[auth APIs](AUTH.md) remain available with their discovery precondition; they
are not silently changed into discovery wrappers.

Discovery is an advisory snapshot and can become stale during a deployment.
A future WebSocket implementation must still declare `C` unambiguously, handle
server floor rejection, and enforce `S >= R` again from its connection's
server information. An accepted socket alone cannot establish this: upstream
accepts declarations above `S`, and absent/unreadable declarations do not prove
negotiation. No WebSocket enforcement is implemented by this HTTP module.

## Local evidence, not live qualification

`make check` includes the pure decoder matrix and a real native C driver against
Python-stdlib HTTPS fixtures. They cover independent floors, malformed/empty
and hostile JSON, exact body/depth bounds, TLS trust/SAN rejection, strict
status/redirect handling, deadlines, limits and transport failures. Fixture
records verify request order, credential-free discovery, rejected-config
non-authentication, fresh preflights and authentication phase failures.
Synthetic secrets enter through checked private regular 0600 files, never
argv/environment or output. Assertions remain active under `NDEBUG`.
`GIN_PROTOCOL_PREFIX` permits a test-only native driver prefix such as Valgrind.
These are local fixtures, not qualified Grappa server interoperability or a
connected-client claim; see [COMPATIBILITY.md](COMPATIBILITY.md).
