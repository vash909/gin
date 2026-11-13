# Pure Phoenix v2 TEXT API

The [verified-WSS native connection API](CONNECT.md) provides root-first
subscriptions through libcurl >=8.23.0. The [production session owner](SESSION.md)
composes verified startup and live selected-window plaintext reception in the
CLI. Connected FIFO send/control and recovery remain deferred; no live Grappa
interoperability is claimed.

## Scope and sources

`include/gin_phoenix.h`, `src/phoenix.c`, `src/phoenix_stream.c` and
`src/phoenix_refs.c` implement a pure text codec, reply identity helpers, an
explicit-message fragment collector and an owner-local reference generator.
There is no I/O, RFC6455 parser, Phoenix binary serializer, socket handshake,
authentication, subscription state, pending-request registry, heartbeat timer,
reconnect logic or runtime integration. `gin run` remains local-only. No new
library is required: the codec uses the existing Jansson >=2.14 dependency.

The source-derived fixtures follow Grappa
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`, whose `mix.lock` pins Phoenix 1.8.9.
Authoritative serializer references use the full Phoenix commit, not a mutable
release tag:

- [Phoenix v2 JSON serializer](https://github.com/phoenixframework/phoenix/blob/734c8d1c32253627e234c8a1cb831367191ff5dd/lib/phoenix/socket/serializers/v2_json_serializer.ex):
  map-payload text encoding at lines 28–34 and 95–101; replies at 63–72;
  first-five-of-at-least-five text decoding at 112–121.
- [Grappa wire splitter](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/frontends/shottino/wire.c):
  `wire_frame_split()`, lines 1458–1466, also accepts at least five slots.
- [Reference client](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/frontends/shottino/shottino.c):
  join/push references at 8399–8457; heartbeat at 9944–9950.
- [Pinned dependency record](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/mix.lock),
  line 59. These are documentation references, not copied runtime sources or
  evidence of a live-server session.

## Supported text schema

The encoder emits exactly five slots:

```text
[join_ref, ref, topic, event, payload]
```

The decoder accepts **at least five** slots, ignoring additional slots only
*after validating the complete JSON document*. Unknown object keys and event
names are accepted. An unknown event remains opaque; it does not become a
successful operation or bypass the object-payload policy.

Gin intentionally supports a stricter subset than the generic upstream decoder:

- Both references must be JSON string or null, without numeric/boolean
  coercion. NULL, `""`, `"null"`, `"0"`, `"01"` and `"1"` are distinct.
- Topic and event must be nonempty strings. Metadata must be strict UTF-8,
  without Unicode C0/C1 or DEL controls. Empty reference strings are permitted.
- Payload must be an object. Text message encoding upstream requires a map;
  the generic upstream text decoder itself does not enforce this restriction.
- `phx_reply` payload requires a nonempty string `status` and object `response`.
  Unknown keys remain allowed. `"ok"` and `"error"` have explicit enum values;
  every other valid status is `GIN_PHOENIX_REPLY_UNKNOWN`, **never success**.
- Complete-input/EOF checking, strict UTF-8 everywhere, duplicate-key rejection
  after unescaping and rejection of decoded NUL apply even to ignored additions.
  Payload controls other than NUL are JSON data, not safe rendered log output.

Constructed golden envelopes, not captured or transmitted messages:

```json
["1","1","grappa:user:fixture","phx_join",{}]
["1","2","grappa:user:fixture","fixture_event",{}]
["1","1","grappa:user:fixture","phx_reply",{"status":"ok","response":{}}]
[null,"3","phoenix","heartbeat",{}]
[null,"3","phoenix","phx_reply",{"status":"ok","response":{}}]
```

A join uses equal join/request references; a later push retains its join
reference and allocates a new request reference. Heartbeat uses null join
reference. These fixtures establish encoding, not successful joins or liveness.

`gin_phoenix_reply_matches()` requires an actual `phx_reply` and exact,
case-sensitive topic, join reference and request reference, preserving null
versus string distinctions. It can match error and unknown-status replies.
The caller must separately require reply status OK, a live connection epoch
and its own pending-operation state. Matching alone proves none of those.

## Local resource policies

| Resource | Maximum |
| --- | --- |
| Raw complete message and final encoded message | 1048576 bytes (1 MiB) each |
| Total JSON container depth | 32, including outer envelope array |
| Encoder payload container depth | 31 |
| Each reference | 64 UTF-8 bytes |
| Topic | 4096 UTF-8 bytes |
| Event | 128 UTF-8 bytes |
| Reply status | 64 UTF-8 bytes |
| Frame-owned storage | 4194304 bytes (4 MiB) |
| Collector pending heap capacity | 1048576 bytes |

These are gin policies, not upstream maxima, and not the binary serializer's
255-byte field limits. A string/escape-aware nesting scan precedes recursive
JSON parsing, including ignored fields. Serialization counts through a bounded
callback before allocating its output; it never dumps an unbounded result merely
to check its size. Arithmetic checks remaining capacity before addition.

Frame-owned accounting includes the frame, copied metadata with terminators,
and separately serialized payload/response views. It excludes transient Jansson
DOM/library allocations, caller input, collector capacity and encoded output.
The DOM, including ignored additions, is freed before publishing a frame.
The raw input cap is not a total-process-memory guarantee.

## Ownership, arguments and outcomes

Decode requires a fresh NULL `struct gin_phoenix_frame *` output; encode requires
an empty `{NULL,0}` byte output. Occupied outputs return INVALID unchanged.
Failures publish nothing. Inputs must remain readable, stable and disjoint from
output during the call. C-string inputs must be terminated; metadata inspection
is bounded by its maximum plus terminator. NULL byte pointers are allowed only
with zero length. An empty complete document is MALFORMED; a bad local argument
is INVALID; a policy excess is LIMIT. Bad local encoder schema/payload is INVALID,
whereas a peer's bad JSON/schema is MALFORMED.

Successful frames deeply own their metadata and compact complete JSON object
views, independent of input mutations or freeing an encoded buffer. Getters
borrow storage until `gin_phoenix_frame_free()`. Use view **length**, not assumed
NUL termination. A decoded user-topic payload view is available to pure topology
parsing with its byte count; no automatic transport acquisition or dispatch is
introduced. `frame_fields(NULL)` and `frame_reply(NULL)` return NULL;
`frame_reply(nonreply)` returns NULL.

Frame free best-effort wipes gin-owned bytes before freeing. Byte free wipes,
frees and clears both members, and accepts NULL; a cleared byte object can be
freed again. This is not a promise to erase Jansson/library temporary copies.
Error strings are fixed, with no frame data, credentials or logging.
Explicit library/owned-allocation failures preserve NOMEM. Some internal Jansson
2.14/2.15 allocation failures can be reported as syntax errors or recovered
internally; perfect OOM classification is not promised. Production does not
replace process-global library allocators.

## Explicit TEXT message collector

The collector accepts fragments of **one already opcode-validated WebSocket
TEXT message**. `final` must be exactly 0 or 1 and means the explicit end of that
message, never end of a socket read. It is not a TCP, NDJSON or RFC6455 parser.
UTF-8 bytes, escapes and surrogate escapes may be split arbitrarily.

Every valid non-final feed returns NEED_MORE and no frame, even if the entire
prefix already looks like valid JSON. Only final calls the real decoder. An
empty final flushes pending bytes; without pending bytes it is MALFORMED. Two
concatenated JSON roots are invalid, not two messages. A final decoder error
consumes that boundary, permitting the next message to recover.

Limit/OOM while accumulating wipes pending bytes and latches dropping through
the observed final boundary. All otherwise valid tail feeds return that error;
a valid-looking suffix is never reinterpreted as a new frame. A final consumes
the boundary and permits the next message. Invalid local arguments (including
occupied output, NULL/nonzero bytes and invalid final) preserve pending/drop
state and do not consume a boundary. Explicit reset clears both states; a future
connection owner, not this module, decides connection epochs.

## Local request references

Initialize with `GIN_PHOENIX_REFS_INITIALIZER` or reset, then call
`gin_phoenix_refs_next()` with at least `GIN_PHOENIX_REF_CAP` (21) bytes. It emits
locale-independent ASCII decimal `1` through `UINT64_MAX` with NUL terminator.
Invalid arguments/capacity and exhaustion leave state/output unchanged; the last
value marks exhaustion, never wraps or reuses a reference. Reset is an explicit
owner decision for a new epoch. State is caller-exclusive, not process-global.
Remote references remain opaque 64-byte strings, not integers to normalize.

## Tests and qualification boundary

`make check` runs the real codec/ownership/fault unit, real collector+codec unit,
separate collector-only decode facade and independent reference-counter unit.
The core unit includes `src/phoenix.c` for private allocation injection; the real
stream unit includes `src/phoenix_stream.c` and links the actual codec separately.
Do not compile either included source twice. Fault switches and isolated Jansson
allocator replacement exist only in tests, not production.

Coverage includes golden envelopes, exact limits/+1, depth, ignored additions,
UTF-8/NUL/duplicates, nullable identities, all two-chunk splits and one-byte feeds,
explicit-final dispatch, reset/recovery, invalid local argument preservation and
limit/OOM drop-through-boundary. All four units are included in normal platform,
musl, NDEBUG, sanitizer and Valgrind gates. Required native FreeBSD CI and
independent review remain necessary; Linux fixture success is not live Grappa
qualification or evidence that roadmap steps 12–20 are implemented.
