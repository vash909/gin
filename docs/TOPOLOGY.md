# Owned network and window topology

`include/gin_topology.h` provides an isolated synchronous fetch API and pure,
immutable snapshot APIs. These are not connected to `gin run`, subscriptions,
filesystem projection, or message sending. No live Grappa revision is qualified.
The source-reviewed wire reference is Grappa
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`; `/boot` requires server protocol >=4.
This feature floor is not a gin client declaration or an exact-version check.

## Fetch and lifecycle

The caller owns the existing [HTTP global lifecycle and TLS policy](HTTP.md).
`gin_topology_fetch` requires positive explicit client/required server protocols;
it raises the effective required server floor to at least 4 without changing the
caller policy. Each call performs a fresh unauthenticated `/api/config`, then
credential-bound `/me`, then exactly one authenticated `GET /boot`. The existing
[account-only identity API](AUTH.md) enforces subject, origin and stored UUID
binding. There is no endpoint fan-out, cache, retry, login or persistence.
HTTP connect/total deadlines apply per request, not to the entire sequence.
This synchronous API must remain outside runtime poll callbacks.

Only HTTP 200 supplies boot data. Errors preserve protocol-floor, identity,
TLS, timeout, transport and response-limit distinctions. Completed boot responses
map 401 to unauthorized, 429 to throttled, and 403 to scope only when the whole
bounded valid JSON object has `error: "client_token_scope"`; other 403 responses
are forbidden. Other completed statuses are remote errors. Diagnostics are fixed
strings, never raw response bodies, peer URLs or credentials. The fetch adapter
best-effort wipes its raw response buffer; Jansson's internal copies are not
promised to be wiped.

## Boot is not query inventory

`gin_topology_parse_boot` requires `networks` (array), `channels` (exact-slug-keyed
object), and `heads` (object). Each network consumes only positive int64 `id`,
exact `slug`, and separate own `nick`; unused fields, including `kind`, are not
authentication evidence. Every network requires a channel array, even `[]`, and
unknown channel slugs fail. Channel rows require `name`, boolean `joined`, and
`source` equal to `autojoin` or `joined`. These values are retained explicitly;
a local path or history entry never establishes live IRC membership.

Heads are syntax-checked but neither retained nor interpreted. Missing history,
own nick, `$server`, or a locally nonchannel name does not establish a DM peer.
A boot-only snapshot has **unknown query inventory**, not known-empty queries.

`gin_topology_apply_queries` accepts the decoded user-topic event payload, after
Phoenix framing has been removed:

```json
{"kind":"query_windows_list","windows":{"7":[{"network_id":7,"target_nick":"Peer","opened_at":"opaque timestamp"}]}}
```

This is not a REST endpoint or a Phoenix frame decoder. Numeric object keys must
be canonical positive decimal int64 IDs: no leading zeros, signs, whitespace or
overflow. Each group must name a known network and each row ID must match its
group. `opened_at` is a nonempty string of at most 64 bytes, retained as opaque,
untrusted metadata; it is not an ordering clock or rendered output.

A valid application creates a new independently owned snapshot containing the
original networks/channels plus exactly the new queries. Omitted groups, `[]`,
and `windows: {}` remove old queries. Even an empty update makes query inventory
known. Invalid updates leave the base and its borrowed views untouched. Acquiring
this user-topic snapshot over WebSocket remains roadmap steps 11/12; REST fetch
alone does not discover live queries. Query identity says nothing about E2EE or
plaintext eligibility.

## Identity, paths, ownership and limits

Numeric IDs, exact slugs, own nicks, original targets, ASCII-folded canonical
targets and encoded path components are separate fields. Remote CHANNEL/QUERY
role comes only from the source inventory. Local `gin_name.kind` follows the
caller's copied CHANTYPES and need not match remote role: a remote channel such
as `&chan` can have a local NONCHANNEL classification. The existing
[naming codec](NAMING.md) supplies strict UTF-8 targets, ASCII-only folding and
255-byte components, not IRC CASEMAPPING or Unicode normalization.

Duplicate IDs, exact slugs, and canonical-target or local-path aliases within
one network fail with conflict rather than choosing arbitrary raw spelling.
Identical targets on different networks are valid. Networks are sorted by numeric
ID; windows by numeric network ID, canonical target bytes, then role. Windows
store numeric IDs, not pointers into sortable network arrays. Lookups require
exact IDs/slugs or already-canonical targets; they do not fold, allocate or do I/O.

Constructors require `out != NULL` and `*out == NULL`; they never overwrite an
owned output. Failure leaves an initially empty output NULL. All returned views
and strings belong to the snapshot and remain stable until `gin_topology_free`.
Both snapshots may be freed independently, in either order. `free(NULL)` is safe;
NULL counts are zero and NULL/out-of-range accessors return NULL. Accessors and
lookups perform no allocations or I/O.

Local policy caps are 1 MiB of JSON bytes, depth 32 checked **before** recursive
Jansson decoding, 128 networks, and 4096 total channels plus queries. The 8 MiB
owned-memory budget covers snapshot arrays and strings with checked arithmetic;
it does not include the Jansson DOM, libcurl state or raw response buffers.
All JSON, including unused additions and heads, must be complete, duplicate-free,
UTF-8 and NUL-free. Oversized/deep data fails before decoding; bounded additive
fields are ignored. Jansson explicit out-of-memory errors and own-allocation
failures map to NOMEM. Supported Jansson releases can misreport some internal
allocation failures as syntax errors; these remain failures with no snapshot,
not successful partial inventories. No process-global allocator is installed by
production code to override that library behavior.

Pure units use the actual core with an isolated allocation hook and a restored
Jansson allocator; lookup contract units separately use a read-only test facade.
Local TLS fixtures are synthetic-only. Neither constitutes live interoperability,
filesystem reconciliation or a connected CLI. Future projection must hold the
[root writer lock](LOCKING.md) before any filesystem write.
