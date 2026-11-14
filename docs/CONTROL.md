# Minimal native window controls

## Scope and ownership

These native control APIs are composed by the [connected owner](SESSION.md)
from its selected FIFO: synchronous REST JOIN/PART and asynchronous WSS QUERY.
Control arguments do not change the selected destination or create local windows
on acceptance alone. Selected PART reports its outcome and ends the owner.
`gin run` remains a local-only reader. Local FIFO/HTTPS/WSS fixtures claim no
live Grappa interoperability or recipient delivery.
The upstream reference is the retained
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9` protocol snapshot and its pinned
[ChannelsController](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/channels_controller.ex),
[GrappaChannel](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/grappa_channel.ex) and
[IRC.Identifier](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/irc/identifier.ex)
implementations.

A native owner reads complete records with `gin_input_next`, parses them with
`gin_command_parse`, and dispatches only successful typed results. Synchronous
REST controls must run outside runtime/network callbacks. The owner retains
filesystem-root, HTTP-global and connection lifetimes; these APIs do not install
signal handlers, start threads, discover credentials or create a hidden queue.

## Closed input grammar

Input is length-aware, excludes the FIFO LF delimiter and is limited to 4096
bytes. Empty input has no action. Strict UTF-8 is required; ASCII C0 and DEL are
rejected. Plain input is a SEND value unchanged. An initial `//` removes exactly
one slash and produces SEND without recursively parsing its contents.

The only commands are lowercase verbs with exactly one ASCII space and one
nonempty argument:

```text
/join #Room
/part #Room
/query Bob
//join #Room
```

The last record sends literal `/join #Room` through the existing plain-send
adapter. The lower-level `gin_send_plain` API itself retains literal-body
semantics; adding a frontend does not reinterpret its existing callers.

There are no implicit current-window arguments, comma lists, channel keys, PART
reasons, query messages, quoting, shell escapes, raw IRC or generic server RPC.
Unsupported slash verbs, uppercase variants, missing/extra arguments, invalid
UTF-8 and oversized records are rejected as whole records. Check every return
code: failed parsing leaves the caller's output untouched and never authorizes
using an older result.

Original wire case is preserved. JOIN/PART require a configured channel prefix
and a conservative local 50-byte target bound; lone `#` and `&` are allowed,
lone `+` and `!` are not. Query peers follow the pinned wire-nickname rules,
not the narrower nickname-registration grammar: up to 128 bytes, including
leading-digit collision UIDs and `~`. Control/space bytes, comma, prefix/host
separators, dots, wildcards and channel sigils are excluded; leading `:` and `$`
are excluded. Existing canonical filename-component limits still apply.
An empty configured CHANTYPES disables channels, not ordinary messages/queries.
A nonchannel spelling is not evidence of an existing peer conversation.

## REST JOIN and PART

`gin_control_join` and `gin_control_part` borrow one live held root, the existing
Send settings/route types, an origin/UUID-bound stored credential and a nonzero
caller-unique operation ID. The route must explicitly be CHANNEL and PLAIN;
UNKNOWN/ENCRYPTED modes cannot become plaintext fallback. That mode is owner
policy, not server attestation.

The sequence is local validation, fresh anonymous `/api/config`, bound `/me`,
then at most one mutation. Credentials are not attached to anonymous config.
The controls use a conservative effective required server floor of at least 4;
this is not historical server-version runtime qualification. Deadlines are per
HTTP request, not one aggregate sequence deadline.

- JOIN: `POST /networks/:exact_slug/channels`, JSON containing only `name` with
  the original channel spelling.
- PART: `DELETE /networks/:exact_slug/channels/:original_channel`, no request
  body or reason parameter.

Each path segment is independently URI-encoded, not replaced by a filesystem
component. Both operations require a completed HTTP 202 response with validated
boolean `ok:true` for ACCEPTED. Other 2xx, malformed/duplicate/short responses
and transfer failures are not acceptance. The existing bounded original-byte
consumed-field guard is reused for 202; it does not repair Jansson or establish
arbitrary ignored-metadata fidelity under every allocation failure.

Acceptance is not observed membership. PART can remove persistent autojoin
before a later operation fails; a failed response is not rollback evidence.
Neither operation echoes input, changes logs/FIFOs, automatically retries or
claims delivery. Protocol/auth diagnostics start INVALID until their respective
phase is invoked. `attempted` records mutation-API invocation, not proof of
transmitted bytes.

## Query opening over the existing WSS session

`gin_connect_open_query` is a narrow typed operation, not a generic push API.
It requires READY, the active authenticated user-root generation, explicit
PLAIN policy, a positive numeric network ID resolved from an owned topology
using the exact slug, and valid peer input. Numeric IDs never come from FIFO
text or filesystem names. Both initial config and the root reply must meet the
effective floor 4. Discovery is fresh per explicit connection start, not falsely
claimed fresh for every operation on a persistent session.

The frame uses the active root join reference, a newly allocated request
reference, the authenticated root topic and `open_query_window` with integer
`network_id` and string `target_nick`. A root subscription acknowledgement is
not an operation acknowledgement. Only a reply matching the active epoch,
root generation, topic, join reference and request reference may complete the
operation. Its entire outbound frame must have been sent before acceptance.

There is one operation slot, including an unconsumed terminal receipt. A second
submission is BUSY without allocating another reference or enqueueing a frame.
The owner can inspect an owned `gin_connect_query_result` and explicitly consume
a terminal receipt with `gin_connect_query_take`. Mutating operations cannot be
called reentrantly from a frame callback.

The operation deadline covers enqueue, bounded partial writes and reply wait.
Short/AGAIN writes retain the exact unsent offset. Timeout, transport loss or
explicit close preserves a terminal receipt: NOT_SENT only if no bytes were
written, otherwise UNCERTAIN. A new start cannot silently discard an unconsumed
receipt. References/epochs do not reset or wrap; there is no replay. The opt-in
[liveness policy](LIVENESS.md) schedules owner-driven bounded reconnects only
after that receipt is consumed. Freeing a session explicitly abandons its receipt.
An explicit server error is REMOTE_FAILURE, not proof of no prior side effects.

A `query_windows_list` application event is a separate full replacement snapshot,
not an ACK. A snapshot can precede or follow acceptance; omitted/empty groups
remove older queries through `gin_topology_apply_queries`. ACK alone does not
change inventory. Copy borrowed callback data into bounded owner storage and
apply snapshots after service returns. Opening a query neither sends a chat
message nor proves peer presence, encryption state or recipient delivery.

Heartbeat/backoff and session-severance handling belong to the separate native
liveness policy. Connected CLI/runtime scheduling and history/gap recovery remain
future work. No new E2EE capability is introduced.
