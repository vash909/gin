# Architecture

## Scope and project boundary

Gin is an **independent companion project**: a headless Unix file/FIFO frontend
for Grappa, inspired by ii. It is not an IRC server or a direct IRC client,
not a TUI, and not an official upstream frontend. Building it does not require
a Grappa checkout, Elixir/BEAM, Node, or a browser application toolchain.

The ecosystem proposal is upstream issue 2588. The maintainer recommended
putting it inside the Grappa tree to track upstream changes. This repository
instead follows the user's explicit independent-project requirement; it must
own its compatibility checks rather than imply upstream endorsed this placement.
The issue is discussion context, not an immutable contract.[17]

The reviewed contract is pinned to upstream
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`.
`Grappa.Protocol.version/0` and `min_version/0` return 63 and 1 at that revision.
Those numbers describe upstream, not implemented gin support. See
`PROTOCOL.md` for source attribution, request shapes and compatibility gates.[2]

## What exists versus what is intended

| Area | Implemented scaffold | Intended later |
| --- | --- | --- |
| CLI | `--help`, `--version`, `init`, read-only `check`, local-only `run`, explicit `-f` configuration | Connected operation |
| Filesystem | Private directories, FIFO/log, persistent root writer lock and runtime descriptor ownership | Remote lifecycle reconciliation |
| Input | Bounded C reader API, safe existing-FIFO access and fair poll scheduling | Explicit command grammar and sending |
| Output | Creates/reuses log without truncating it | Escaped timestamped events and visible errors |
| Transport | Isolated bounded libcurl GET/POST/DELETE TLS and account authentication APIs, not connected to runtime | Grappa REST plus Phoenix Channels integration |
| Runtime | One foreground local-only process using POSIX `poll()` and signal self-pipe | Network adapter integration |
| Encryption | None | Fail-closed unsupported encrypted operations; separately reviewed future E2EE |

The isolated [REST/TLS API](HTTP.md) is implemented in `src/http.c` /
`include/gin_http.h`; it is synchronous and must not run inside poll callbacks.
The separate [account authentication API](AUTH.md) adds bounded JSON login,
identity and logout with explicit [private storage](CREDENTIALS.md), but no
protocol discovery, runtime connection or Grappa compatibility claim.

The implemented local CLI boundary is `src/gin.c:main()`, `src/config.c:gin_config_read()`
and `src/fs.c:gin_validate()` / `gin_init()`, declared in `include/gin_config.h`
and `include/gin_fs.h`. `src/input.c` / `include/gin_input.h` provide the
length-aware bounded input API; [INPUT.md](INPUT.md) defines exact byte, EOF,
writer atomicity and caller-owned lifecycle contracts. `run` integrates it through
`include/gin_runtime.h` / `src/runtime.c`; see [RUNTIME.md](RUNTIME.md).
The [CLI contract](CLI.md) defines explicit file loading,
precedence, bounded grammar and exit status. `check` calls the shared lexical
validator without probing or mutating the output filesystem. It is not a server
probe. `run` is a genuine local-only reader, not a connected client; nonempty
complete input fails with exit 1 because sending is unsupported.
Initialization holds a root writer lock only for its own descriptor lifecycle;
it does not authenticate, contact a server, open an input reader, or send a chat
message. Do not infer operational behavior
from the presence of directories or the word `in`.

## Implemented local projection

```sh
.build/gin init -d "$HOME/gin" -n azzurra -c '#grappa'
```

This creates or safely reuses:

```text
$HOME/gin/                  directory, 0700
├── .gin.lock               persistent regular file, 0600; POSIX advisory lock
└── azzurra/                directory, 0700
    └── grappa/             directory, 0700; target is #grappa
        ├── in              named pipe (FIFO), 0600
        └── out             ordinary regular file, 0600
```

`out` is preserved on repeated initialization and run. Only `gin run` consumes
`in`: **without it, shell redirection can block waiting for a reader**.
`tail -f out` has no incoming network events to show. The following is the
future interface from the proposal, not a runnable messaging demo today:[17]

```sh
# Future only: an authenticated foreground gin must already be running.
echo 'Hello from gin!' > "$HOME/gin/azzurra/grappa/in"
tail -f "$HOME/gin/azzurra/grappa/out"
```

### Naming is a projection, not an IRC parser

`src/naming.c` / `include/gin_naming.h` implement the shared reversible codec
used by both lexical validation and initialization. `struct gin_name` separates
the immutable input network slug and original wire target from canonical
Grappa identity, encoded network/window components and channel/nonchannel kind.
Nonchannel includes synthetic windows, not just proven DMs. No numeric network
ID is inferred; actual discovery remains roadmap 10.

Configured `-T CHANTYPES` / file `chantypes` is a distinct subset of `#&+!`,
default `#`, optionally empty. Default `#Grappa` -> `grappa`, query `Grappa` ->
`@grappa`; configured `&Foo` -> `+26foo`, `+Foo` -> `+2Bfoo`, `!Foo` -> `+21foo`.
Literal plus is escaped, keeping those namespaces disjoint from `#` channels.
Only ASCII A–Z target bytes fold, unconditionally for channel and nick/window
identity per `Grappa.PubSub.Topic.channel/3`.[8] Brackets and non-ASCII case stay
distinct; no RFC1459 fold or Unicode normalization is applied, independently of
IRC network CASEMAPPING. Original wire casing is retained separately, not
recoverable from a canonical filename.

Targets require strict UTF-8 scalars and reject C0/C1 controls, DEL, ASCII
whitespace, slash and empty/dot/dotdot bodies. All non-ASCII bytes become
uppercase `%HH`, preserving NFC/NFD distinctions on normalization-insensitive
filesystems. Network slugs retain ASCII letter/digit/underscore/hyphen input
syntax and exact case; uppercase letters are escaped (`Net` -> `%4Eet`), not
folded. Decoder APIs reject aliases by exact canonical re-encoding. Raw bounds
are unchanged; both expanded components must fit 255 bytes without truncation.
See [NAMING.md](NAMING.md) for API requirements, examples and the full policy.

**Pre-beta migration boundary:** uppercase-network paths changed. Select a new
root or explicitly review/manual-migrate existing logs. No legacy-path fallback,
automatic migration, renaming, deletion, database or metadata framework exists.

### Filesystem safety boundary

`root_dir()` resolves path components with directory descriptors and `openat()`;
`O_NOFOLLOW` applies to each opened component, not only the final path.
Root `..` components are rejected. Missing ancestor directories are not
recursively manufactured; the parent must exist. The root, network and window
must be private directories owned by the effective user. `private_dir()` /
`check_dir()` reject unsafe existing directories rather than silently chmodding
or replacing them. The initializer temporarily uses `umask(077)`.

`check_endpoint()` requires owner-matching, single-link endpoints of the
expected type and exact `0600` permissions. `in` is checked with
`fstatat(..., AT_SYMLINK_NOFOLLOW)`. `output_file()` uses non-following,
append-only opening, validates the resulting descriptor, and checks identity
when reusing an existing file. It never truncates an existing log. Errors
remain errors: gin does not delete suspicious objects to make startup succeed.

These checks do **not** make an untrusted/shared-writable ancestor safe and do
not defend against another hostile process running as the same user. Choose a
trusted parent, do not run as root, and do not treat a successful initialization
as a continuing runtime lock. Initialization can leave safe partial directories
after a failure; a later initializer completes valid layouts without replacing
the persistent lock inode or deleting logs. The kernel releases ownership on
close/process death, not on a stale timeout or PID-file check. There are no
whole-tree transactions or power-loss durability guarantees.

`gin_root_acquire()` retains root and POSIX `F_SETLK` lock descriptors;
`gin_root_init()` initializes windows through that held root and closes its
children without releasing ownership; `gin_root_release()` ends ownership.
`gin_init()` wraps these calls after full lexical validation. The lock precedes
all network/window/FIFO/log mutations. See [LOCKING.md](LOCKING.md) for exact
inode checks, failure behavior, trusted-parent/local-filesystem scope and the
POSIX per-process duplicate-close caveat. The local-only runtime retains this
ownership through input and wake descriptor cleanup.

## Intended runtime: small core, isolated network adapter

```text
shell writers             foreground gin (planned)               Grappa
    │                       ┌─────────────────────┐                  │
    └── window/in FIFO ────► │ bounded input       │                  │
                            │ explicit commands   ├── REST/TLS ─────►│
                            │ resource/state map  │◄─ Phoenix/TLS ───┤
    ┌── window/out file ◄─── │ escaped log output  │                  │
    │                       │ poll + timers       │                  │
 tail/scripts               └─────────────────────┘                  │
```

The design is portable C99 with POSIX.1-2008 interfaces, one process, foreground
operation and `poll()`, not a daemon/TUI framework. No threads, Linux-only
`epoll`/`inotify`, database, shell execution or language runtime are required by
the core design. Signal handlers should only request orderly shutdown; normal
control flow releases descriptors and private state.

Adapter dependencies:

| Dependency | Status and responsibility |
| --- | --- |
| libcurl >=7.85.0 | Linked; REST requests, verified TLS and bounded responses/deadlines |
| Jansson >=2.14 | Linked; JSON encoding/decoding at the authentication boundary |
| libwebsockets | Planned and unlinked; WebSocket/TLS upgrade, frame handling and service integration |

libcurl and Jansson are build requirements. libwebsockets remains a future dependency choice, not a tested integration.
The core should consume typed resource/events and return explicit effects;
keep library handles and callbacks inside the adapter. Integrating two network
libraries with one `poll()` loop needs a tested descriptor/timer ownership
contract. Do not hide blocking login, DNS or history operations inside the FIFO
reader and call that a nonblocking design. No Elixir or Node dependency should
leak into build/install steps for this independent client.

### State ownership and identity

Maintain separate fields for authenticated subject/topic label, numeric network
ID, network slug, original target, canonical topic key, local path component,
window kind, server message/cursor identity and socket join generation. A
numeric ID in a query-window command is not a slug in a REST URL; a local
`grappa` directory is not IRC `#grappa`. Upstream uses
`GrappaWeb.Plugs.ResolveNetwork.call/2` for slug lookup and
`GrappaWeb.GrappaChannel.do_handle_in/3` for guarded numeric query payloads.[13][7]

Filesystem presence is a local projection, not authoritative remote membership.
Future startup must acquire one writer lock per root, discover state, join the
user-root topic, and reconcile subscriptions/windows. User-root lifecycle
updates must be processed even when no per-window subscription exists yet;
otherwise pending/invited/failed windows disappear. Upstream's
`push_user_snapshot/2` and `push_window_state_if_known/4` distinguish root
snapshots from window subscriptions.[7]

A local init/open does not request JOIN. A Phoenix topic join does not request
IRC JOIN either. Future JOIN/PART commands use the resource actions described
in `PROTOCOL.md`; successful membership waits for root lifecycle events.
Query open/close uses the user topic and `query_windows_list` snapshots.
There is no invented `parted` event to wait for.[6][7][1]

Closing a remote window should stop relevant subscriptions/input acceptance,
not erase local logs. A lost transport should leave logs readable and report
unavailability instead of pretending the user left IRC. Retaining/moving
closed-window directories, tombstones and restarts need an explicit policy
before runtime implementation; the initializer currently removes nothing.

### FIFO runtime, log and delivery rules still to implement

- The [bounded input API](INPUT.md) handles local LF records; the
  [runtime](RUNTIME.md) implements fairness, EOF backoff and signals. Bound future
  queues, HTTP bodies and JSON depth separately; no transport exists yet.
- Define an explicit small command grammar separately. Reject unsupported
  commands; never pass text to a shell or raw IRC path by default. Define the
  foreground FIFO lifecycle before advertising a running service.
- Emit escaped terminal-safe text: peer data must not inject ANSI controls,
  fake new status lines or paths. Distinguish `message.kind` from the outer
  event kind and present actions without duplicating CTCP wrappers.[1][16]
- Use the REST message door for ordinary sends. Correlate HTTP acknowledgement
  and pushed echo/history by server identity; never report a local write or
  202 as proof a peer received a message.[4]
- Keep queues bounded and make disconnection/write failures visible. Do not
  automatically retry uncertain sends and promise exactly-once delivery.
  Use heartbeat deadlines, backoff and fresh topic joins on reconnect.
- Fetch `/boot` and `/me` for broad resynchronization, then focused cursor
  pages; merge live/history overlap without deduplicating equal text.[10][19][4]

## Security and deferred encrypted conversations

Remote transport must verify certificates and hostnames. Protect credentials
in owner-only storage; no secrets in process arguments, FIFO diagnostics,
URLs or logs. Phoenix auth uses a base64url bearer subprotocol, separate from
public `client_proto` and serializer `vsn` query parameters. Source:
`GrappaWeb.UserSocket.extract_token/1` and reference `ws_upgrade_request()`.[11][16]

Encrypted queries are **unsupported**, not an excuse for a downgrade. The
future dispatcher must carry explicit conversation mode and reject every
unsupported encrypted send/action/history request before it can reach a plain
message, NOTICE, CTCP or raw-command adapter. Never label ciphertext plaintext,
acknowledge encrypted queue entries as successfully consumed without a defined
policy, or create plaintext logs implicitly. The scaffold has no detector,
crypto store or encrypted endpoint consumer yet.

Future E2EE is a separate design and review, not a flag on TLS or the existing
FIFO. Upstream `Grappa.Session.Wire.e2e_message/3` and the protocol's per-device
resources require identity, keys, pairing, verification, offline queues and
cross-client compatibility. Reuse audited cryptographic libraries, protect
keys and require explicit decisions about decrypted log exports. Keep this
work isolated from the dependency-free filesystem scaffold.[21][2]

## Verification and drift policy

Scaffold tests can verify CLI exit behavior, mapping, file types/modes,
unsafe-object rejection and preserved logs without credentials or a server.
They cannot establish Grappa interoperability. Future adapter tests need
pinned golden frames, malformed/unknown payloads, rejection statuses, heartbeat
loss, reconnect gaps, duplicate echoes and encrypted-query negative cases.
Live tests must be opt-in against a declared upstream revision and never use
public production credentials in CI.

Before claiming a supported release, review `Grappa.Protocol`, the client
contract, controllers and channel callbacks at a new explicit SHA; update
fixtures and feature floors together. Do not follow a mutable `main` link or
copy the upstream's current protocol number into gin to manufacture support.
The [preserved client document](upstream/grappa/README.md) and its manifest
are frozen by full upstream revision; update the active baseline only through
a reviewed PR and record real release qualification in
[COMPATIBILITY.md](COMPATIBILITY.md). Never overwrite a retained snapshot or
silently follow upstream main.
This independent repository must do the work that an in-tree build would
otherwise help expose.[2]

## Sources

[1] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/docs/CLIENT_PROTOCOL.md
[2] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/protocol.ex
[4] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/messages_controller.ex
[6] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/channels_controller.ex
[7] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/grappa_channel.ex
[8] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/pubsub/topic.ex
[10] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/boot_json.ex
[11] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/channels/user_socket.ex
[13] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/plugs/resolve_network.ex
[16] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/frontends/shottino/shottino.c
[17] https://github.com/vjt/grappa-irc/issues/2588
[19] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa_web/controllers/me_controller.ex
[21] https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/session/wire.ex
