# Native liveness and bounded reconnect policy

## Scope and ownership

`include/gin_liveness.h` extends the isolated connection API. It does not make
`gin run` a connected client. Native loopback fixtures are not live Grappa
interoperability, history recovery or recipient-delivery evidence.

The single owner configures liveness once while the connection is IDLE, before
its first start. `gin_connect_settings`, connection results and query receipts
retain their existing layouts. Unconfigured sessions retain explicit close/start
behavior, except that recognized session severance is always terminal.

A fully initialized `gin_liveness_settings` supplies:

- Heartbeat interval and due-to-reply timeout: each 1..60000 milliseconds.
- Retry base and cap: `1 <= base <= cap <= 60000` milliseconds.
- Maximum retries: 0..16, excluding the initial start.

An illustrative owner policy is `{30000, 10000, 1000, 30000, 5}`. These are local
policy choices, not negotiated server values. Zero retries still permits
heartbeats. Invalid settings leave the session unchanged. There is no implicit
environment configuration, credential refresh, timer thread or production
clock/transport test hook.

## Phoenix heartbeat

The application heartbeat is distinct from WebSocket ping/pong:

```json
[null, "NEW_REF", "phoenix", "heartbeat", {}]
[null, "NEW_REF", "phoenix", "phx_reply", {"status":"ok","response":{}}]
```

The request uses the existing session-wide monotonically increasing reference
counter. The pending heartbeat also belongs to the current local socket epoch;
there is no epoch field on the wire. Only the exact topic, null join reference,
request reference and known successful reply complete it. Unknown bounded
response metadata is not made mandatory or dumped to application output.
Unrelated, duplicate or stale replies do not satisfy liveness. A matching reply
before the entire request is written, or after its deadline, cannot acknowledge
it. Matching error/unknown-status replies fail closed.

The first interval starts at the successful WSS upgrade, including while child
subscriptions are JOINING. Its deadline starts at the scheduled due time, not
when a late caller eventually services it. Application traffic, ping/pong and
unrelated replies do not reset it. A successful reply schedules one next
interval; missed intervals never create a catch-up burst.

The heartbeat has separate bounded storage (at most 128 encoded bytes), offset
and completion tracking. It does not occupy or consume the query receipt slot.
One wire frame is written at a time: a partially written join/query must finish
before a heartbeat begins; at the next complete-frame boundary a due heartbeat
has priority. No frame may be inserted into another frame's unsent suffix.
A new query is BUSY without allocating a reference while a heartbeat is due,
queued or being written. A fully written pending query or an unconsumed terminal
query receipt does not prevent heartbeat transmission. Heartbeat bytes never
make an untouched query UNCERTAIN.

`gin_connect_service` remains bounded WSS/poll work, not synchronous HTTP.
The service wait is clamped to the relevant join/query/heartbeat deadline or
heartbeat due time. Zero-wait calls still enforce expiry. Existing operation and
byte bounds remain in force. Time is monotonic; clock failures, backward
observations and unrepresentable deadlines fail closed rather than masquerading
as elapsed time or a retry opportunity.

## Explicit bounded retries

`gin_connect_liveness_status` returns an owned read-only snapshot, including a
retry wait and the retained failure reason. It does not reconnect, consume a
receipt or perform HTTP. It is safe for the read-only callback boundary.

The owner calls `gin_connect_retry` outside callbacks and signal handlers when
recovery is due. Only a transient failure, elapsed delay, remaining budget and
empty query receipt slot permit an attempt. ATTEMPTED means the fresh start was
invoked, not that the connection is READY. Inspect the connection's actual state
and result. `service` never secretly invokes this synchronous sequence.

For retry index `k`, starting at one, the delay is:

```text
min(retry_cap_ms, retry_base_ms * 2^(k-1))
```

The helper computes this with capped doubling, without overflowing a shift or
multiplication. This native scope deliberately uses deterministic capped
backoff, **without jitter**. Jitter is neither a Phoenix wire requirement nor an
unimplemented guarantee. The owner must not create an external immediate-retry
loop around this policy.

The budget belongs to the entire configured session lifetime. Reaching READY,
a valid heartbeat, or a successful preflight never resets it. Configured
subsequent starts share the retry gate rather than bypassing the budget.
Explicit close permanently cancels the configured retry scope; an intentional
new scope requires a newly constructed connection.

Each allowed attempt repeats anonymous `/api/config`, exact-origin and
UUID-bound `/me`, verified WSS upgrade, then user-root-first subscriptions with
new references and an advanced local epoch. Only the static desired subscription
plan is restored. There is no topology/history refresh or gap-free delivery
promise here.

Transient transport loss and genuine liveness/join/query timeouts can schedule
recovery. Nested discovery/auth transport errors must be distinguished from the
same outer result carrying a terminal identity, schema or security failure.
TLS refusal, unsupported protocol, changed identity, invalid credentials, scope
refusal, malformed protocol, active channel `phx_error`/`phx_close`, local limits,
OOM and clock failures do not trigger automatic retry. A provisional HTTP status
must not hide an incomplete transfer, timeout or TLS error.

## Session severance and uncertainty

At the pinned Grappa revision, the known user-root broadcast is:

```json
[null, null, "grappa:user:CURRENT_SUBJECT", "event", {"kind":"web_session_severed","code":"rate_limit_flood"}]
```

Interpret it only on the current authenticated JOINED root with a null or
matching active root join reference, including when child joins remain pending.
The known pair terminates the connection with SESSION_EXPIRED and is not sent
to the application callback. Missing/wrong-type consumed fields for the known
kind fail closed; an unknown string code is not invented evidence of expiry.
Foreign topics, stale non-null generations, IRC disconnect events and arbitrary
payloads containing `401` cannot impersonate it. A generic socket close alone
does not establish which bearer was revoked. A subsequent fresh authentication
refusal is independent evidence. There is no automatic login or token rotation.

Transport/liveness failure preserves pending operation identity and a terminal
NOT_SENT or UNCERTAIN receipt according to actual query writes. An unconsumed
receipt blocks reconnect until the owner explicitly takes it; taking a receipt
never replays an operation or resets the budget. Reconnect restores subscriptions
only: it never silently repeats `open_query_window`, messages, JOIN or PART
mutations, and it never claims exactly-once sending or remote rollback.

## Primary references and verification boundaries

The Grappa reference remains
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`; retained upstream snapshots are
unchanged. The session-severance publisher is
[RequestBudget](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/request_budget.ex)
through the [rate-limit wire serializer](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/rate_limit/wire.ex)
and [user socket](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/user_socket.ex).
Phoenix heartbeat handling is pinned to
[Socket](https://github.com/phoenixframework/phoenix/blob/734c8d1c32253627e234c8a1cb831367191ff5dd/lib/phoenix/socket.ex)
and its [reference client](https://github.com/phoenixframework/phoenix/blob/734c8d1c32253627e234c8a1cb831367191ff5dd/assets/js/phoenix/socket.js).

The test gate includes a pure backoff unit, private translation-unit clock/I/O
fault tests, and actual verified local HTTPS/WSS fixtures. Private shims prove
owner scheduling and partial-write behavior, not libcurl/TLS interoperability;
native fixtures cover that separate transport boundary. Platform, sanitizer and
Valgrind qualification remains required. No test uses a live server, real
credentials or production messages.
