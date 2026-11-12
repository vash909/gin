# Grappa protocol contract

## Status and pinned reference

This is an implementation guide for **planned transport**, not a claim that
gin can connect. The repository provides local CLI/filesystem
initialization plus a bounded local input C API and local-only foreground
runtime. An isolated [bounded HTTP/TLS GET API](HTTP.md) now exists, but no
CLI connection, message sender or E2EE. The isolated
[HTTP discovery and gated-auth APIs](DISCOVERY.md) perform fresh preflights.
The isolated [connection API](CONNECT.md) now implements verified WSS and
root-first subscriptions. The isolated [renderer and append API](RENDER.md)
projects source-derived incoming records; CLI/runtime integration remains future work.
Separate [account authentication APIs](AUTH.md) implement bounded JSON and
private credentials outside runtime callbacks. The local input
limit is not a Grappa limit; see [INPUT.md](INPUT.md).
See `ARCHITECTURE.md` for the implemented boundary.

The reviewed upstream revision is
`vjt/grappa-irc@87daa4cca9b41fbd6c5f93cb40d6848193f425f9`.
The local research inputs were upstream `docs/CLIENT_PROTOCOL.md` and
`lib/grappa/protocol.ex`, with relevant source checked at the same revision.
`Grappa.Protocol.version/0` returns **63** and `min_version/0` returns **1**
at that pin; these are snapshot facts, not gin's implemented capabilities or
constants to substitute for live discovery.[1][2]

The complete upstream client document is also preserved as an
[unmodified offline snapshot](upstream/grappa/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/CLIENT_PROTOCOL.md),
with its original license and integrity manifest. The
[snapshot policy](upstream/grappa/README.md) governs reviewed baseline updates;
[compatibility records](COMPATIBILITY.md) distinguish this reference from actual
release qualification. This guide interprets that frozen reference; it is not
a rolling copy of upstream main.

Numbered sources below are immutable commit links, except the explicitly
labelled project-discussion reference in `ARCHITECTURE.md`. Module/function
names identify the authoritative implementation; code wins over prose.
The examples are small constructed wire illustrations, not captured sessions.

## Discovery and compatibility policy; isolated authentication

1. Fetch unauthenticated `GET /api/config` before sending credentials or opening
   a socket. Read `server`, diagnostic software `version`, `protocol_version`
   and `min_protocol_version`. `push_content_encoding` describes Web Push,
   **not** encrypted-query capability. Source:
   `GrappaWeb.ConfigController.show/2` and `Grappa.Protocol`.[18][2]
2. Support a configured per-client bearer as the preferred unattended
   credential. Alternatively `POST /auth/login` accepts
   `{"identifier":"account","password":"credential"}` and returns
   `token` plus `subject` on successful account login. A per-client token can
   occupy that password field, and the returned bearer is the same token;
   it may also be presented directly without login. A password login requiring
   a second factor returns 202, not an authenticated session. Source:
   `GrappaWeb.AuthController.login/2`, `account_login/3`; contract section 7.[20][1]
3. Use `Authorization: Bearer <token>` for authenticated REST. Keep secrets
   out of URLs and diagnostics. Distinguish invalid/revoked authentication
   (401) from `403 client_token_scope`; the latter requires a full session,
   not credential retries. Pairing and credential-management routes are
   full-session surfaces. Source: contract section 7 and
   `GrappaWeb.Router`'s `:full_session` scopes.[1][3]
4. Fetch authenticated `GET /me` for subject identity and bulk cursors/counts,
   and `GET /boot` for `networks`, `channels` keyed by slug, and `heads` keyed
   by slug then window. `/boot` was introduced at protocol 4; a missing head
   means no history entry, not a second response format. It reuses the
   standalone resource shapes. Source: `GrappaWeb.MeController.show/2`,
   `GrappaWeb.BootJSON.index/1`, `Grappa.Protocol`.[19][10][2]

Gin's intended minimal transport requires `/boot`, so its **proposed baseline
server floor is 4**, with per-feature gates for anything newer. This is a design
floor, not a compatibility claim. The actual client declaration must describe
the subset implemented and tested when transport lands; the scaffold declares
nothing. Track three separate values:

- Server current protocol `S`, from discovery/join reply.
- Server minimum accepted client declaration `M`.
- Gin's implemented declaration `C` and its required server floor `R`.

The intended checks are `C >= M` and `S >= R`, not equality of all versions.
Feature gates compare `S >= introduction_version`. Never gate on the software
release string. Current protocol increases on every wire-shape change, including
additions; the server's minimum is independent. Ignore unknown fields/events
without making unused fields mandatory. Removed fields include archive
`row_count` (v8) and `dm_conversation_id` (v37); neither is a gin identity.
Source: `Grappa.Protocol` module documentation and `version/0`, `min_version/0`.[2]

## Three naming axes: never substitute one for another

| Axis | Example | Use |
| --- | --- | --- |
| Grappa network slug | `azzurra` | REST per-network path and topic network segment |
| Grappa numeric network ID | `7` | Explicit numeric payloads, including query-window commands/lists |
| IRC target / window identity | `#grappa`, `Guest87449`, `$server` | Resource window, channel-topic segment, or synthetic server window |
| Local filesystem component | `grappa`, `@guest87449`, `@%24server` | gin projection only; never a wire target |

Despite its name, REST `:network_id` resolves a **slug** through
`GrappaWeb.Plugs.ResolveNetwork.call/2`; query-window verbs instead guard their
`network_id` as an integer in `GrappaWeb.GrappaChannel.do_handle_in/3`.
Keep an explicit ID-to-slug map from network discovery, not decimal conversion
or a filename heuristic.[13][7]

`GET /networks` and `GET /networks/:network_id/channels` remain standalone
resource doors. The latter returns channel entries containing `name`, `joined`
and `source`; neither a local directory nor a topic subscription is evidence
that IRC JOIN succeeded. Source: `GrappaWeb.NetworksJSON.index/1` and
`GrappaWeb.ChannelsController.index/2`.[14][6]

Percent-encode HTTP path segments independently, so `#grappa` is not a URL
fragment. Do not reuse gin's local percent-encoding as an HTTP encoder. Preserve
the original target, its topic key, and its filesystem component separately.
These are gin design requirements, not extra upstream API fields.

## WebSocket authentication and serializer

Dial `/socket/websocket?vsn=2.0.0&client_proto=C` over verified TLS for a remote
server. `client_proto` is the Grappa integer declaration; `vsn=2.0.0` selects
Phoenix's serializer. They are unrelated versions. Source:
`GrappaWeb.UserSocket.check_protocol_version/1` and the reference client's
`ws_upgrade_request()`.[11][16]

Offer `phoenix` and `base64url.bearer.phx.<standard-unpadded-Base64(raw-token)>`
in `Sec-WebSocket-Protocol`. Despite the historical prefix, Phoenix requires
standard Base64, not the URL-safe alphabet (`aa~` becomes `YWF+`). Pinned Phoenix
`734c8d1c32253627e234c8a1cb831367191ff5dd`, `assets/js/phoenix/socket.js:398-399`
uses `btoa` with padding removed; `lib/phoenix/transports/websocket.ex:111-116`
uses `Base.decode64!` with padding false. This corrects gin's former interpretation
based only on the C reference. See [connection policy](CONNECT.md).
Do not append the raw bearer, add `Bearer ` inside the encoding, or send
`?token=`. Phoenix exposes the decoded token as `connect_info.auth_token`;
`GrappaWeb.UserSocket.extract_token/1` accepts that source only. Endpoint
configuration enables socket-level `auth_token: true`.[11][12]

A readable declaration below the server floor is rejected with 426 and an
`upgrade_required` JSON body containing both version numbers. Bad/missing auth
normally produces 403; version checking runs first. An absent or unreadable
`client_proto` is treated as current, and declarations above the server's
current version are accepted. Therefore a successful upgrade proves neither
that a declaration parsed nor that the server meets gin's feature floor.
Always send an unambiguous decimal integer and perform discovery checks.
Source: `GrappaWeb.UserSocket.connect/3`, `check_protocol_version/1`,
`handle_ws_error/2`.[11]

## Exact Phoenix v2 frame shape

Text encoding emits **exactly five-element arrays**, not Phoenix v1 objects.
The [isolated pure codec](PHOENIX.md) accepts at least five elements on decode,
matching the pinned Phoenix serializer and Grappa splitter, but validates the
whole JSON document including ignored trailing elements. Its bounded string/null
references, object payloads and strict metadata rules are intentional local
supported-subset restrictions, not generic upstream-decoder requirements:

```text
[join_ref, ref, topic, event, payload]
```

References below are JSON strings; `null` is a real JSON null. A join establishes
a topic's join reference. Later pushes retain that `join_ref` while allocating
a distinct request `ref`. A wrong join reference can be discarded silently.
Source: reference client `ws_v2_frame()`, `ws_join()`, `ws_push_user()`;
`wire_frame_split()` in `frontends/shottino/wire.c`.[16][22]

```json
["1","1","grappa:user:alice","phx_join",{}]
["1","1","grappa:user:alice","phx_reply",{"status":"ok","response":{"protocol_version":63}}]
["2","2","grappa:user:alice/network:azzurra/channel:#grappa","phx_join",{}]
[null,null,"grappa:user:alice","event",{"kind":"window_pending","network":"azzurra","channel":"#grappa","state":"pending"}]
["1","3","grappa:user:alice","open_query_window",{"network_id":7,"target_nick":"bob"}]
[null,"4","phoenix","heartbeat",{}]
[null,"4","phoenix","phx_reply",{"status":"ok","response":{}}]
```

The user join reply's `response.protocol_version` comes from
`GrappaWeb.GrappaChannel.join_reply/2`; channel join replies instead include
`read_cursor` and `window_counts`, not an empty response. Correlate replies with topic, request
reference and current join generation; `phx_reply` is not a chat event.
Handle unsuccessful replies, `phx_error` and `phx_close` separately. A
`phx_join` subscribes to a Grappa topic: it is not an IRC JOIN command.[7][16]

## Topics, events and window lifecycle

`Grappa.PubSub.Topic.user/1`, `network/2`, `channel/3` define:[8]

```text
grappa:user:{user}
grappa:user:{user}/network:{slug}
grappa:user:{user}/network:{slug}/channel:{target}
```

Join the authenticated **user-root topic first**, then required network/window
topics. Use the authenticated subject's topic label, not an IRC nick guessed
from the local path. Topic target folding is ASCII `A-Z` only; it is not
Unicode or RFC1459 bracket folding. A DM topic names the folded peer nick.
Source: `Grappa.PubSub.Topic.channel/3`, `parse/1` and
`GrappaWeb.GrappaChannel.join/3`.[8][7]

Live window-state events `window_pending`, `window_invited`, `joined`,
`join_failed` and `kicked` belong on the **user topic**. A per-channel join
can push an already-known state snapshot to that socket, but it does not carry
those live broadcasts. Channel topics carry messages, members, modes, topic
and read-cursor updates. Source: `GrappaWeb.GrappaChannel.push_user_snapshot/2`,
`push_window_state_if_known/4`; `Grappa.Session.Wire.window_pending/2`,
`joined/2`; contract sections 4 and 9.[7][21][1]

For planned channel controls, `POST /networks/:network_id/channels` with
`{"name":"#grappa"}` requests JOIN and returns 202, not proof of membership;
`DELETE /networks/:network_id/channels/:channel_id` requests PART. Query
controls are user-topic verbs `open_query_window` / `close_query_window`, with
numeric `network_id` and `target_nick`. The user-topic `query_windows_list`
is a full snapshot (`windows` grouped by numeric ID, JSON object keys rendered
as strings), not an incremental append. Source:
`GrappaWeb.ChannelsController.create/2`, `delete/2`,
`GrappaWeb.GrappaChannel.do_handle_in/3`,
`Grappa.QueryWindows.Wire.render_grouped/1`, `windows_list_payload/1`.[6][7][15]

There is **no `parted` event**. Own-PART removes window state; its `part`
scrollback row is a different fact. Reconcile channel/window lists rather than
waiting for an invented terminal event. Replies such as `whois_bundle` are
requester-only on the user topic; missed command replies must be requested
again. Source: contract sections 4 and 9.[1]

## Messaging: two kinds, one resource door

A Phoenix frame's event name is `event`; its payload's `kind` selects the
Grappa event. For chat that outer kind is `message`, and the nested
`message.kind` selects `privmsg`, `notice`, `action`, or a presence/history row.
Do not look for an outer `privmsg`/`action` event. Source: contract section 9
and the reference client's `wire_narrow()` / `wire_narrow_message()` parsers.[1][22]

The planned ordinary FIFO line maps to
`POST /networks/azzurra/channels/%23grappa/messages` with
`{"body":"Hello from gin!"}`, **not** a made-up Phoenix `message` verb.
A normal persisted send returns 201 and a single message map; history GET
returns a flat array. Some services sends return 202 with no persisted echo.
Source: `GrappaWeb.MessagesController.create/2`, `finish_send/3`,
`GrappaWeb.MessagesJSON.show/1`, `index/1`.[4][5]

A future `/me` command uses that same resource door with a CTCP ACTION body,
for example JSON `{"body":"\u0001ACTION waves\u0001"}`; it does not add an
invented `action: true` field. Upstream can return `message.kind = "action"`
while preserving the CTCP wrapper in `body`; strip it only for presentation.
Source: reference client `handle_command_dispatch()`'s `/me` branch,
`ctcp_action_text()` and `render_row()`.[16]

Optional `ctcp_target`, `notice_target` and `statusmsg_target` are separate
relay semantics and mutually exclusive, not substitutes for the URL window.
Gin must implement and test them explicitly before exposing such controls.
Source: `GrappaWeb.MessagesController.create/2`.[4]

## Native heartbeat/reconnect policy; planned history recovery

Send an application-level `heartbeat` on topic `phoenix` with null join ref
and a fresh request ref; a WebSocket ping alone does not replace it. The
reference C client uses a 25-second cadence in `ws_connect()` / `ws_pump()`;
this is an implementation example, not a negotiated server timeout.[16]

The opt-in [native liveness API](LIVENESS.md) uses a monotonic heartbeat/reply
deadline, finite lifetime retry budget, deterministic capped exponential backoff
without jitter, and fresh join references after reconnect. Jitter is not a wire
requirement or a guarantee of this local policy. The owner explicitly invokes
due synchronous retries outside service/callbacks; user-root subscriptions
precede dependent subscriptions. Stale references cannot authorize a new push.
Revoked credentials, recognized session severance, scope refusal and an unmet
protocol floor are terminal. Unconsumed query receipts block reconnect, and no
uncertain operation is silently replayed: HTTP failure can follow server effects.
The CLI remains local-only. The upstream reference's `ws_try_reconnect()` also
refreshes state, and `getSocket()` rebuilds on bearer rotation; gin does not claim
those broader recovery capabilities here.[16][9]

Prefer `/boot` plus `/me` for cold start and broad resynchronization instead
of a per-channel fetch storm. Paginate focused gaps using
`GET /networks/:network_id/channels/:channel_id/messages?after=<id>`;
use `before=<id>` for older history and `around=<id>` for a centered page.
Only one of the six ID/time cursors may be supplied. HTTP page size defaults
to 50; a supplied limit must be 1..200, with larger values rejected rather than
silently clamped. Actual focused latest/before pages are ID-descending and after
pages ID-ascending despite older controller prose about timestamp order. Source:
`GrappaWeb.MessagesController.index/2`,
`parse_direction/1`, `parse_limit/1`, `GrappaWeb.BootJSON.index/1`.[4][10]

Time cursors `before_time`, `after_time`, `around_time` require server protocol
**at least 40**; older servers may silently return the latest page. Values
are decimal epoch milliseconds. Strict before/after time bounds can skip
same-millisecond rows: start by time, then page by ID; clock rollback needs
additional filtering if strict time bounds matter. Source:
`GrappaWeb.MessagesController.index/2` and contract section 5i.[4][1]

Deduplicate live/backfill overlap by server message identity scoped to the
subject/network, never by body equality. Do not claim exactly-once delivery.
Use `dm_with` (v36) for DM row classification when available; a peer rename
does not move its old query/history. Keep a deliberate older-server fallback,
or raise the feature floor rather than requiring v36 accidentally. Source:
contract section 5h; `Grappa.Protocol`'s v36/v37 entries.[1][2]

The isolated [native history API](HISTORY.md) implements the deliberate subset
latest/before/after by ID, raising its query floor to 36. It does not emit time,
around or count selectors. The recovery planner advances its REST pagination
cursor only from accepted REST pages, never the buffered live maximum. A short
page means visible drain at that request, not complete original history.
Read cursors, boot heads and ID jumps do not prove processed/delivered continuity;
there is no retention watermark, server-generation token or atomic REST/WSS cut.
Caller-owned output/checkpoint policy remains separate, and append-only files
cannot reorder previously written logs. The CLI remains local-only.

## Encryption boundary: fail closed

**TLS is required for remote credentials; E2EE is a separate deferred feature.**
Gin does not implement encrypted queries, devices, pairing, key storage,
verification or encrypted history. Future encrypted-query input must fail
explicitly as unsupported. Never route it through the ordinary message POST,
`/me`, NOTICE, CTCP ACTION or raw-command paths as plaintext fallback.
Unknown events remain nonfatal, but that rule is not permission to downgrade
an encrypted operation or display ciphertext as an ordinary message.

Upstream E2EE is a client-side cryptographic conversation relayed through
user-topic `e2e_message` events and separate `/e2e/...` REST resources. At the
pin the protocol has per-device queues and keys, sibling copies and optional
channel addressing; the early v42/v43 routes are not a complete current
implementation recipe. Source: `Grappa.Session.Wire.e2e_message/3`,
`Grappa.Protocol` v42-v63 history, contract section 4f.[21][2][1]

Future E2EE needs a separate reviewed design: explicit conversation mode,
device identity and authenticated pairing, audited crypto dependencies,
protected key persistence, key/session lifecycle, acknowledgement timing,
recovery and interoperability fixtures. Pairing also needs a credential flow
that respects the upstream full-session gate. A plain FIFO path must never
silently stand in for an encrypted window. Plaintext log export of decrypted
content, if ever offered, requires explicit opt-in and a documented threat
model. None of this is provided by the filesystem scaffold.[3]

## Sources

[1] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/docs/CLIENT_PROTOCOL.md
[2] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/protocol.ex
[3] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/router.ex
[4] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/messages_controller.ex
[5] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/messages_json.ex
[6] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/channels_controller.ex
[7] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/grappa_channel.ex
[8] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/pubsub/topic.ex
[9] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/cicchetto/src/lib/socket.ts
[10] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/boot_json.ex
[11] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/user_socket.ex
[12] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/endpoint.ex
[13] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/plugs/resolve_network.ex
[14] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/networks_json.ex
[15] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/query_windows/wire.ex
[16] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/frontends/shottino/shottino.c
[18] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/config_controller.ex
[19] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/me_controller.ex
[20] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/auth_controller.ex
[21] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/session/wire.ex
[22] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/frontends/shottino/wire.c

## Isolated plain send

See [SEND.md](SEND.md) for the pinned plain REST action, local request identity,
last-fragment-only 201 ID, no-persist 202 and uncertain/partial-side-effect
semantics. There is no Phoenix send RPC or recipient-delivery acknowledgement.

## Isolated minimal controls

See [CONTROL.md](CONTROL.md) for the closed command grammar, exact REST JOIN/PART
202 acceptance, and typed `open_query_window` WSS request/reply identity. Its
query-operation ACK is separate from subscription ACKs and full query snapshots.
The conservative effective server floor is 4, not the pinned current version 63;
fixture results do not qualify historical or live server versions. No generic
server command, inferred membership, replay or connected CLI is introduced.
