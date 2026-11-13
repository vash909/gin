# Isolated plain-message sending

`include/gin_send.h` exposes `gin_send_plain()`, a bounded **synchronous native
API**. It is not wired into local `gin run` or receive-only `gin connect`;
FIFO-send integration remains 18.3. Never invoke
this adapter from a runtime, network or renderer callback. The native integration
fixture owns the FIFO and explicitly calls the adapter outside all callbacks;
this is not a new asynchronous runtime or a live Grappa qualification.

## Ownership and local policy

The owner retains one acquired `gin_root`, the existing global HTTP lifecycle,
all caller storage and all descriptors throughout the call. Never acquire a
second root handle or independently open/close `.gin.lock`: POSIX locks are
process-associated. Sending only inspects the supplied descriptors and lock
entry; it neither consumes them nor creates destinations or local output.
Holding the lock in this process remains the owner's lifecycle obligation.

Supply a nonzero `uint64_t` request ID, unique for the owner's lifetime. Every
result returns it unchanged, including invalid inputs. It is local operation
identity, not an on-wire nonce, Phoenix reference, socket epoch or deduplication
key. Reusing it is an owner error that the stateless adapter cannot detect.

Bind an explicit network **slug**, original wire target, channel types, known
CHANNEL/QUERY role and explicit PLAIN mode. These are trusted owner assertions,
not server attestations or information inferred from a FIFO path/text. For
queries use the destination peer, not an incoming message's source topic.
UNKNOWN/ENCRYPTED mode and unknown roles fail closed without discovery or POST.
`$server` and ambiguous channel-like query targets are not send destinations.

Local route, body, stored credential, exact origin binding, protocol policy and
per-request deadline checks precede network I/O. Bodies are 1..4096 bytes of
strict UTF-8 scalar text without C0 or DEL. Leading `/` is literal text, not a
command, action or shell invocation. The FIFO reader additionally rejects empty,
control-bearing, oversized and unterminated EOF records before dispatch. Writers
must respect the actual FIFO's `PIPE_BUF`: each cooperating writer submits the
complete LF-terminated record in one atomic write. A 4096-byte payload plus LF
may exceed that bound; the body cap is not an atomic-writer guarantee.

## Exact request sequence

Every call fetches fresh unauthenticated `GET /api/config`, checks the explicit
protocol floors, then verifies the stored credential through `gin_auth_identity`
(`GET /me`, preserving exact origin and subject UUID binding). There is no bearer
import/resume, `/boot` fetch, per-network discovery, cache, retry or persistence.
Each request has its own connect/total deadline; these are not a whole-sequence
deadline. The owner initializes and cleans up global HTTP exactly once.

Only then issue one authenticated HTTPS JSON POST:

```text
/networks/:network_slug/channels/:wire_target/messages
{"body":"the exact plain text"}
```

Each original path segment is independently RFC3986-percent-encoded; filesystem
encoding and numeric network IDs are not used. JSON contains only `body`.
Redirects are not followed. Request JSON is capped at `GIN_SEND_JSON_MAX`, path
at `GIN_SEND_PATH_MAX`, URL at `GIN_SEND_URL_MAX`, response body at
`GIN_SEND_RESPONSE_MAX` (65536), headers at `GIN_HTTP_HEADER_MAX` (65536), JSON
depth at `GIN_SEND_DEPTH_MAX` (32). Owned response storage is heap allocated.

## Results are not delivery receipts

- Complete HTTP 201 plus a valid single row object with positive integer `id`
  and exact `network` means `ACCEPTED_PERSISTED`. `message_id` is the **last
  persisted fragment only**, not an ID covering the whole input line.
- Complete HTTP 202 plus `{"ok":true}` means `ACCEPTED_NO_PERSIST`, with ID zero.
- Complete non-2xx HTTP returns `REMOTE_FAILURE`. This does **not** mean no
  message was sent: upstream may persist/broadcast earlier fragments before a
  later write fails.
- Unknown 2xx, malformed JSON, transport loss, TLS failure, timeout, response
  limits or an incomplete 201 are `UNCERTAIN`. An observed POST status is only
  diagnostic; incomplete transfer takes precedence over success-looking headers.
- `NOT_ATTEMPTED` means the POST API was not invoked. Attempt flags describe API
  invocation, not proof that particular bytes reached the wire.

No result proves upstream socket completion, recipient receipt or peer delivery.
Never automatically replay any attempted operation. There is no local input or
201-response echo: log output comes only from received events through the
separate renderer and append API outside its callback. WSS events and HTTP
acknowledgements are independent. The synchronous fixture services WSS only after
`gin_send_plain` returns: it proves server emission order around the HTTP response,
not client receipt or rendering before the acknowledgement.
Diagnostics are static identifiers, never tokens, URLs, input or raw errors.
Verified credentials and owned response/request buffers are cleared on exit.

## Response integrity under allocation failure

The bounded Jansson decoder remains responsible for whole-document validation.
A separate allocation-free lexical check compares the original top-level
`id`/`network` (201) or `ok` (202) evidence with the decoded values before any
result is published. It supports reordered and escaped keys, rejects duplicate
consumed keys and ignores nested or quoted decoys. The private helper requires
an ASCII network slug, matching the production naming policy.

An isolated allocation-failure test on Jansson 2.14 exposed a successful decode
with an altered integer and no error indication. The original-byte check rejects
that mismatch rather than publishing the altered message identity. Tests also
sweep allocation failures with escaped keys and mismatched network names.
This is a consumed-value integrity mitigation, **not** a repair of Jansson's
allocation handling or a memory-safety guarantee. It is not an independent proof
of ignored metadata's full syntax/duplicate policy under arbitrary internal OOM.
No production allocator hooks or dependency fork are introduced.

## Source and fixture qualification

The reviewed action is pinned to Grappa commit
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`: `messages_controller.ex`,
`messages_json.ex`, `resolve_network.ex`, `fallback_controller.ex` and
`lib/grappa/session/server.ex`. The controller returns a row for 201, or the
services/no-persist 202 acceptance shape. Session processing persists/broadcasts
each fragment before the upstream write; a later error can follow partial side
effects. No Phoenix send RPC is invented.

`tests/test_send.py` uses a real native driver, private root, FIFO, loopback TLS
CA and HTTPS server. Its combined WSS case uses existing root-first subscriptions
and the server emits synthetic echoes before and after the HTTP response. The
driver receives and drains both events after the synchronous POST returns, then
appends owned records outside callbacks. This proves server emission order only,
not client-side receipt before the HTTP acknowledgement. This exercises
adapter composition, not integrated CLI messaging, server attestation or live
interoperability. Real platform CI and explicit live-server qualification remain
separate requirements.
