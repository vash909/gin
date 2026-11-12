# Isolated account authentication API

The separate [verified-WSS native connection API](CONNECT.md) adds isolated
root-first subscriptions through libcurl >=8.23.0. The separate
[production startup owner](SESSION.md) now composes a subscription-only connected
CLI; chat reception/send/recovery integration remains deferred. No live Grappa
interoperability is claimed.

## Scope and prerequisites

`include/gin_auth.h` / `src/auth.c` implement synchronous account-user login,
bearer identity validation and current-bearer logout. They are reusable native C
APIs; the [production startup owner](SESSION.md) now explicitly composes raw
bearer enrollment and stored UUID-bound resume. `gin run` and its legacy
configuration remain local-only. The separate [discovery and gated-auth APIs](DISCOVERY.md)
provide the recommended fresh-preflight path. No WebSocket, messaging, OAuth,
TOTP or passkey interaction is implemented. Outside synthetic fixtures, the
low-level caller **must perform the protocol-discovery policy in PROTOCOL.md
before sending credentials**. This module declares no gin protocol version and passing
fixtures do not qualify compatibility with a live Grappa revision.

The wire subset was checked against Grappa commit
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`: `AuthController.login/2` and
`logout/2`, `AuthJSON.login/1`, `MeJSON.show/1`, the `Authn` plug, Router authn
scopes, and preserved CLIENT_PROTOCOL section 7. Immutable source links are in
[PROTOCOL.md](PROTOCOL.md). In particular logout requires authentication, not a
browser full session. Client-token management under `/me/client-tokens` requires
a browser full session and is not implemented here.

## Caller lifecycle

Initialize the HTTP global owner once before requests and clean it up after all
users stop; see [HTTP.md](HTTP.md). Every auth call requires an exact lowercase
HTTPS authority-only origin (maximum 1024 bytes), no userinfo, path, query or
fragment, plus trusted CA selection and explicit connect/total deadlines. Fixed
endpoint composition prevents caller-supplied paths. Even loopback credentials
require verified TLS >=1.2. No redirect is followed, no alternate origin is
retried, and no proxy, netrc or cookie ambient authentication is used.

- `gin_auth_login(settings, identifier, password, out)` sends only JSON
  `identifier` and `password` to `POST /auth/login`, with Content-Type
  application/json and no previous bearer. Identifier is 1..255 bytes and
  password 1..4096 bytes, valid UTF-8; Jansson performs escaping. A per-client
  token may occupy password and the server returns that same opaque bearer.
- `gin_auth_resume(settings, bearer, out)` presents a directly configured bearer
  to `GET /me`; it does not exchange or rotate it. No expiry or session kind is
  inferred from the opaque token. Caller must bind the directly supplied token
  to the chosen origin before this call; use `identity` for stored credentials.
- `gin_auth_identity(settings, credential, out)` rejects origin mismatch before
  any request. `/me` has top-level `kind`, `id`, `name`, not nested `subject`.
  The returned account UUID must equal the stored UUID byte-for-byte. A benign
  display-name change is accepted in output; persistence of that change is
  explicit. UUID case spelling changes are conservatively rejected.
- `gin_auth_logout(settings, credential)` sends `DELETE /auth/logout` for the
  current bearer. Only completed 204 with empty body reports success. It never
  mutates the caller credential or local file, even after success. A 503 is not
  revocation, logout or permission to forget. Failed/uncertain POST and DELETE
  are never automatically retried: the server may already have acted.

Outputs must be separate from input credentials and credential-contained
strings. Every valid output argument is cleared on failure (identity input/output
aliasing is invalid and leaves the input unchanged). Caller inputs are never
consumed. No remote transaction implicitly calls the private store API. After a
successful login/identity, explicitly save under an already-held `gin_root`;
after confirmed logout, explicitly clear/forget only when the application wants
to discard local state. Local forget alone never revokes a server session.
See [CREDENTIALS.md](CREDENTIALS.md) for private plaintext storage and lock rules.

## Failure and resource boundaries

401 is typed `unauthorized`: invalid, revoked and expired sessions are
indistinguishable on the wire; no login retry occurs. 403 `client_token_scope`
is distinct from ordinary forbidden and requires a browser full session, not
another password attempt. 429 is typed throttling, without automatic retry.
202 with boolean `two_factor_required: true` returns the static
`unsupported-interactive-auth` outcome, clears output and never persists a
challenge as a session. Other malformed 202 replies fail closed.

Only `kind: user` is accepted. Visitors are unsupported, including a visitor
unexpectedly returned by login. The server may provision a visitor for an unknown
nickname; this client does not accept or persist it as an account. Use a known
account identifier; this subset does not promise side-effect-free bad login.

Jansson >=2.14 decodes response JSON using length-aware `json_loadb`, duplicate
rejection and complete-input validation. Responses cap at 65536 bytes; aggregate
headers at 65536; a string/escape-aware preflight caps nesting at 32 **before**
calling the decoder. Invalid UTF-8, embedded NUL, duplicate/escaped duplicate
keys, wrong consumed types and malformed syntax fail closed. Bounded unknown
fields are tolerated but not consumed. Account UUID must have the 36-byte UUID
shape; names are 1..255 UTF-8 bytes without controls; bearers are 1..4096 bytes
with the safe RFC 6750 alphabet and terminal padding. No JWT assumptions apply.
JSON request bodies also have the HTTP 16384-byte cap (escaping can expand input).
Errors are static strings; neither API prints responses, tokens or passwords.

Own response, serialized request and header buffers are wiped best-effort.
This is not a guarantee that Jansson/libcurl/TLS internals or allocator copies
are zeroed, and not secure disk erasure. Callers must not log input credentials,
response bodies or structs, or put secrets in argv/environment/URLs.

## Reproducibility

`make check` runs native auth and credential drivers, including stateful real
HTTPS login/save/identity/logout/revoked-identity fixtures and a 503 preserving
local state. Test secrets enter the auth driver via a checked private regular
0600 fixture file, not argv. Drivers never emit response bodies or credential
structs. Assertions stay enabled under NDEBUG. CA trust, wrong SAN, redirects,
origin mismatch, malformed data, size/depth bounds and typed failures are local
fixture evidence only. No real accounts or public server writes are used.

Host Jansson flags default to pkg-config, with both environment and explicit
make `JANSSON_CFLAGS` / `JANSSON_LIBS` overrides. Genuine musl builds must select
matching prefix headers/archive explicitly, never host pkg-config:

```sh
make CC=musl-gcc CURL_CONFIG="$GIN_MUSL_PREFIX/bin/curl-config" \
  JANSSON_CFLAGS="-I$GIN_MUSL_PREFIX/include" \
  JANSSON_LIBS="-L$GIN_MUSL_PREFIX/lib -ljansson" LDFLAGS=-static check
```

The pinned musl recipe additionally builds Jansson 2.15.1 from its official
release archive, SHA-256
`f9aa4b3ec8496fee69db94f06f3d76fd4a26b012ee9bf1e917078c2cd2841881`,
using musl-gcc, static libraries and at most two compiler jobs. Platform CI,
sanitizers and real-TLS Valgrind are required gates; a local Linux run is not
FreeBSD or live Grappa qualification.
