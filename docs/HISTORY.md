# History and bounded gap recovery

This is the isolated native API in `include/gin_history.h`, not a connected
`gin run`, live Grappa qualification, E2EE support or a delivery receipt. The CLI
remains local-only. Caller-owned credentials, topology, subscription identity,
HTTP global lifecycle and one held output root remain separate responsibilities.

## Source contract and supported subset

The reference is Grappa `87daa4cca9b41fbd6c5f93cb40d6848193f425f9`:

- `lib/grappa_web/controllers/messages_controller.ex`, `index/2` and parameter
  parsers: authenticated GET on the shared channel/query/status resource.
- `lib/grappa/scrollback.ex`, focused fetch/window predicates: actual ID ordering,
  subject/network isolation, query peer matching and presence filtering.
- `lib/grappa/scrollback/wire.ex`, `to_json/2` and `message_payload/2`: the same
  persisted row ID in REST and the nested `message` push.
- `lib/grappa/protocol.ex`: source-derived feature floors, not tested historical
  interoperability. Retained upstream snapshots are unchanged.

The exact path is:

```text
/networks/SLUG/channels/WINDOW/messages?limit=N[&before=ID|&after=ID]
```

`SLUG` is the original case-sensitive network slug, never the decimal network ID.
Each original path segment is separately percent-encoded using RFC3986
unreserved bytes and uppercase hex. `#` is `%23`; a Unicode target keeps its
UTF-8 bytes. Local filename encoding and topic folding are not HTTP encoders.
There is no invented `/queries` endpoint. `$server` is an explicit read-only
status destination, not a send target.

The supported cursor union is latest, before ID and after ID. Latest requires
anchor zero, before requires a positive int64, and after accepts a positive
int64 or explicit zero for the beginning. Native requests always emit a limit
in `1..200`; the suggested default is 50. The server rejects a supplied limit
above 200 rather than silently clamping it. Around/time selectors, count probes,
archive discovery and read-cursor mutation are not part of this API.

HTTP 200 contains a **flat array**, not an object wrapping `messages` and not a
page with `has_more`, `next_cursor` or a retention watermark. Latest/before are
strictly ID-descending; after is strictly ID-ascending with strict anchor bounds.
This follows the actual focused fetch code, not stale controller prose about
time ordering. Equal timestamps and backwards server clock steps do not change
the chosen local ID ordering. Server time is retained for presentation only.

The native fetch enforces the conservative server floor 4 and raises it to 36
for query history requiring `dm_with`. Time selectors would require 40 and are
intentionally not emitted. The client declaration and required server floor are
separate policy values. No historical server release is qualified by these
source-derived checks. Channel E2EE is listed as protocol 63 by the pinned
implementation, despite an older retained prose reference to 62; plaintext
history does not claim that encrypted capability.

## Ownership, route and input validation

Constructors require a fresh NULL output and never replace an occupied object.
Pages own all strings and immutable rows; callers may free the source JSON,
frame or scope immediately afterward. Borrowed views expire only when their
owning page/recovery snapshot is replaced successfully or freed. Failure keeps
old rows, pagination cursor and borrowed views intact, except an explicit
resource/deadline terminal state.

A scope includes exact HTTPS origin, subject UUID, numeric network ID and slug,
original target, channel types, own nick, explicit role/plaintext policy and
nonzero socket epoch. Pure parsing is **not authentication**. The native GET
checks the supplied owned topology and stored credential binding before any
traffic, then performs fresh anonymous configuration discovery, bound `/me`,
and one focused authenticated GET. Query lookup requires a received query
inventory and known peer. An own nick or local NONCHANNEL filename is not a
query inventory. The native subset deliberately excludes archived windows
absent from the supplied topology; it does not discover them implicitly.

REST row routing is separate from the live transport source. An inbound DM can
retain our old nick in `channel`, while explicit `dm_with` identifies the known
peer. A null peer can describe a non-chat query-window event, but cannot route
an ordinary message. Live conversion additionally checks the actual Phoenix
topic, nullable join reference and observed/current epoch against a trusted
CURRENT JOINED context. A null broadcast reference remains valid. REST rows are
never wrapped in a fabricated authenticated/joined frame.

Whole JSON input is bounded, duplicate-free, NUL-free and UTF-8-valid. IDs and
timestamps remain int64; no float conversion or magnitude-based time inference.
Body null differs from empty, and absent/null/string `dm_with` remain distinct.
A separate allocation-free original-byte check protects consumed values when
supported Jansson versions recover from a scratch allocation failure. Successful
fault-injected parses must retain their exact original consumed values. This
does not repair Jansson memory safety or attest ignored real-valued metadata
under arbitrary OOM. Object-key evidence walks insertion order linearly rather
than searching every DOM key for every input key.

## Bounded recovery and identity

Create a recovery attempt from a caller-supplied trusted previous processing
anchor, or zero with an explicit unknown-before result. Never derive that anchor
from `/me` read cursors, unread counts, boot head maxima or a subscription ACK.
Read cursors describe user reading, not delivery. Boot heads are limited,
time-ordered startup data, not complete focused DM history.

The owner subscribes and copies live rows into a count/byte-bounded queue. After
service returns it may feed those owned rows and issue one next AFTER request
at a time. HTTP, output writes and connection reentry never occur inside a
network callback. Recovery helpers perform no HTTP, filesystem or clock reads.
The owner supplies monotonic milliseconds and checks the deadline **after** a
synchronous fetch before accepting its page. Fetch timeouts bound each request,
not the complete three-request sequence; the recovery admission deadline does
not interrupt an in-flight request. The owner must schedule HTTP budgets and
WebSocket service without claiming hidden background heartbeat servicing.

The REST pagination cursor advances only through accepted REST rows. A buffered
high-ID live message must not skip intervening history. Merged staged rows use
numeric ID ascending, regardless of timestamp or callback arrival order.

Deduplication uses persisted IDs inside the exact scoped retained state. Identical
text with different IDs remains two records. Repeated identity with identical
consumed fields becomes one row; conflicting content under the same identity
fails atomically. Additive ignored metadata is not a conflict key. There is no
body/time heuristic, IRC-global message identity or inferred server generation.

The local limits are:

- 200 rows per REST page; 1 MiB raw response; nesting depth 32.
- Body 16,384 bytes, sender 256 bytes, kind 64 bytes.
- 16 REST pages per attempt; 512 distinct admitted live IDs.
- 4,096 staged rows and 8 MiB retained snapshot storage.
- 60,000 ms recovery admission window, with overflow/backwards-clock rejection.

The snapshot budget includes its object, row array and copied strings. It does
not include caller/raw response storage, JSON DOM, transport/URL-parser storage,
recovery bookkeeping, or the old and replacement snapshots simultaneously
needed for atomic update. Limits are local policies, not upstream guarantees.
No silent eviction or truncation makes a failed operation appear successful.

A full final allowed page yields incomplete-limit. A short or empty after page
yields **visible drain at that request**, not complete original history, absence
of purged data or an atomic REST/WSS cut. Once drained/incomplete, this attempt
accepts no further feed. A changed binding, policy or socket epoch requires an
explicit new attempt; recovery never resets the liveness retry budget.

## Presentation, append and restart limits

`gin_history_render` projects a validated row through the existing escaped
presentation helpers; `gin_render_plain_message` is explicitly presentation,
not a new authentication shortcut. Only privmsg/notice/action produce ordinary
chat lines. Unknown row kinds are retained for identity/progress but IGNORE
ordinary display; unknown JSON is never dumped as fallback. Null body displays
as empty, and ACTION follows the existing optional closing-delimiter policy.

The owner appends complete owned records outside callbacks using the existing
`gin_render_append` and a checked existing output descriptor opened through the
same held root. Do not reopen/close the lock inode, create payload-named paths,
truncate logs, or restart a partially appended record. A partial append reports
its actual prefix and stops that sink; it cannot be rolled back automatically.

Buffer catch-up before releasing a sorted batch. An append-only file cannot
insert older history ahead of already-written live lines or establish global
file-wide chronological order. Late older data and overlapping attempts need an
explicit owner policy, not a silent file rewrite.

`fetch_after` is a pagination position, **not a durable processed checkpoint or
output ACK**. Only the owner can record processing progress after successful
output. No persistent checkpoint/database is added here. A new object after
restart needs a trusted caller anchor or explicit unknown baseline; in-memory
deduplication is not a cross-process exactly-once guarantee. Append-before-
checkpoint crashes can duplicate output; checkpoint-before-append can lose it.
Server database restore/ID reuse is not reliably detectable without a generation
token that this protocol does not supply.

## Encryption and honest guarantees

Unknown/encrypted mode refuses ordinary history handling for channels as well
as queries. Reserved `e2e_` kinds and CTCP E2E framing, including malformed forms,
refuse before display or recovery progress. This API neither polls nor
acknowledges the separate encrypted inbox and never falls back to plaintext.
Explicit owner mode is not server attestation; arbitrary ciphertext cannot be
recognized solely from arbitrary bytes. Full E2EE boundary qualification remains
roadmap item 18, not an implied cryptographic capability here.

IDs need not be contiguous per window or subject. Numeric jumps do not establish
missing messages. The server may filter presence rows, purge archives or fail to
persist an event. An empty page cannot distinguish caught up from already lost
history. There is no promised retention duration, minimum retained ID, anchor
existence flag or cross-REST/WSS replay barrier.

The contract is bounded best-effort recovery of currently retained, visible
server rows, with explicit limits/unknown state. It promises neither exactly-once
output, recipient delivery, original-gap completeness, durability, global
ordering nor absence of loss. No outgoing send/query/JOIN/PART is replayed.

## Qualification

Pure constructor/fault tests, real native HTTPS tests, and a composed native
WSS/HTTPS/held-root append fixture are separate boundaries. Shim and facade tests
are not live-server interoperability or native macOS/FreeBSD evidence. The
complete matrix and independent final-source review must qualify the final PR;
local privileged ownership skips remain explicit and require the isolated CI
lane. See [development](DEVELOPMENT.md) and [connection liveness](LIVENESS.md).
