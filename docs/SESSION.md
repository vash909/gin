# Operational startup, live reception and session ownership

`gin connect`, `gin connect-check` and `gin credential-import` are production
commands backed by the native authentication, discovery, topology and verified
WSS APIs. `connect` appends eligible live plaintext records for **only the
selected network/window**, reads its FIFO and dispatches SEND/JOIN/PART/QUERY.
Reconnect and history recovery remain unwired; this is not a beta or complete
messaging client. The old `init`, `check` and local-only `run` retain their
behavior and four-key settings format. No existing invocation silently starts
network access. Tests use synthetic loopback authorities, not a live Grappa
server; interoperability remains unqualified.

## Explicit commands and private enrollment

```text
gin connect [-f SESSION_FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]
            [-e HTTPS_ORIGIN] [-m plain] [-a CA_FILE] [-R STATUS_FD] [-O OP_FD] [-E]
gin connect-check [same options; lexical validation only]
gin credential-import -d ROOT -e HTTPS_ORIGIN -s PRIVATE_BEARER_FILE [-a CA_FILE]
```

Flags may occur once, take no positional operands and do not read implicit
configuration or credential environment variables. `-E` is the only flag without
an argument. Relative paths use the invocation cwd, not the settings directory.
CLI strings are not trimmed. `-R` and `-O` take canonical decimal fd numbers
>=3 and must differ when both are supplied. `connect-check` checks this syntax
but does not probe descriptors. At runtime both require already nonblocking
pipes with write capability; they are borrowed/duplicated close-on-exec without
changing caller flags. `-O` additionally requires actual PIPE_BUF >=192.

`ROOT` also selects exactly one private store, `ROOT/.gin.credentials`, for one
exact origin and account. There is no second root, arbitrary store filename,
automatic enrollment, account switch or token refresh. Missing/unsafe storage
fails closed. See [credential storage](CREDENTIALS.md) for exact owner/mode,
symlink/hardlink, UUID, atomic replacement and durability limits.

Create the bearer input out of band in a trusted location: an effective-user-owned,
single-link regular file with exact mode `0600`, no final symlink, containing
**1..4096 exact RFC 6750 bearer bytes**. No whitespace, newline, CR or NUL is
trimmed; an editor's automatic trailing newline must be disabled. Never put the
token in argv, environment, a URL, ordinary settings, a diagnostic or shell history.
The following example assumes that private file already exists; its contents
are deliberately not shown:

```sh
gin credential-import -d ./chat -e https://grappa.example.invalid -s ./bearer.private
gin connect -d ./chat -n NetA -c '#Room' -e https://grappa.example.invalid -m plain
```

Import reads and closes the safe file **before** acquiring the root, runs fresh
anonymous config followed by raw-bearer `/me` identity validation, then explicitly
saves the authenticated account credential through the held root. It may replace
a safe existing store; auth/preflight failure leaves that store unchanged.
A save error after rename can mean the new value was already published: do not
silently retry or promise rollback. Import does not rotate a token or remotely
log out. Its per-request connect/total defaults are 10000/20000ms.

Ordinary connect instead loads stored origin/UUID binding and validates it with
bound identity APIs. It never uses raw-bearer import to bypass a changed UUID,
rewrites a display-name change or mutates the store. Process termination is not
revocation, secure erasure or remote logout.

## Separate connected settings

Both connected commands accept a separate safe `0600` file. Legacy commands
reject these additional keys; use separate files rather than migrating a local
`run` into network access implicitly.

```text
root=./chat
network=NetA
target=#Room
origin=https://grappa.example.invalid
mode=plain
# Optional trusted regular CA bundle; omit for compiled trust.
# ca_file=./private-ca.pem
connect_ms=1500
total_ms=3000
join_ms=3000
startup_ms=30000
```

Exact permitted keys are `root`, `network`, `target`, `chantypes`, `origin`,
`mode`, `ca_file`, `connect_ms`, `total_ms`, `join_ms`, `startup_ms`. Existing
local naming bounds still apply; origin is at most 1024 bytes, CA path at most
4095 and mode at most 15. Origin must be an exact lowercase HTTPS authority,
without userinfo, path, query or fragment. Only explicit `mode=plain` is accepted.
It is an **owner assertion**, not server attestation that a conversation is
unencrypted; no plaintext fallback, detection completeness or E2EE is claimed.

File grammar retains the legacy 16384-byte file and 8192-byte physical-line
bounds, space/tab trimming, whole-line comments, LF/CRLF, no NUL/controls/DEL,
no interpolation/quoting, owned single-link regular exact0600 and no final
symlink. Duplicate, unknown and empty keys fail, except explicitly empty
`chantypes`. Raw bounds/text and present origin/mode/timers are validated even
when CLI flags override them; naming applies to effective values. Partial files
are allowed if flags supply all required effective settings. Default chantypes
is `#`; the root/network/target/origin/plain assertion must be provided.

Timers are canonical positive decimal milliseconds: connect, total and join
are <=60000; startup is <=600000; effective total must be >=connect. The defaults
shown above are operational settings, not test-only switches. `connect-check`
validates only the file and effective lexical settings: no root/store/lock/CA/fd
probe, HTTP request, socket, mkdir, FIFO open or log write. A nonexistent CA or
unopened but syntactically valid status fd can pass check and fail connect.

## One foreground owner and actual READY

The owner is fixed to one selected root/network/window. It acquires one root
once, retains the credential and keeps that root through all session resources.
Before READY it initializes the selected window if needed and opens its FIFO
reader. Existing logs are preserved; a missing selected log is created empty.
It does not call the old local runtime or acquire a second root handle. Other
discovered windows and control arguments do not create additional output trees.
HTTP initialization/cleanup surrounds all users. A configured CA must be a
trusted regular file, not a device/FIFO; a CA inode alias of the held lock is
refused without opening/closing it. Trusted parents and no hostile same-UID
namespace changes remain caller obligations.

Startup performs fresh config -> UUID-bound `/me` -> `/boot`, then connection
start separately repeats fresh config -> bound `/me` -> same-authority verified
WSS. TLS >=1.2, certificate/hostname checks, exact origin and no proxy/netrc/
cookie/redirect/insecure fallback remain the existing native policy. The bearer
is offered only in the Phoenix encoded subprotocol, never an upgrade
Authorization header or URL. Numeric network IDs, exact slugs, original targets,
canonical targets and local components remain distinct.

Connected startup and credential-import declare **C=36, R=36**. This conservative
receive subset requires the pinned protocol-36 `dm_with` evidence, not just the
protocol-4 `/boot` shape. It does not implement every feature through 36, declare
captured upstream current63 or wire the native history APIs into this owner.
Standalone native APIs still take explicit caller policy.
Checks are independently C>=server minimum and server current>=R, plus the root
join protocol floor. A higher additive server current version is not rejected
merely for being newer; an unmet minimum fails before credential use. This is
local subset qualification, not live-server compatibility evidence.

A selected channel must exist in boot with remote CHANNEL role and joined=true;
autojoin alone or an on-disk filename is insufficient. A requested nonchannel
may be subscribed pending query evidence, but becomes eligible only after a
validated full current-epoch root `query_windows_list` proves that exact
network/canonical QUERY. Boot/own nick never establish a peer. Missing inventory
is UNKNOWN and times out. During initial discovery, empty then valid replacement
may establish the route before readiness; intervening chat is never retroactively
authorized. Once a valid selected query disappears, the session fails even if a
later snapshot re-adds it. DM admission also qualifies the planned source topic
and `dm_with`; a selected subscription or payload target alone is insufficient.
The exact-deduplicated plan contains at most four topics: root, selected network,
selected channel/peer and the boot own-nick query topic. Own-nick transport is
neither peer inventory evidence nor an extra output destination.

Transport admission validates planned source/current JOINED state, correlated
join reference and socket epoch. Recognized metadata and chat share one
copy-owned, arrival-ordered queue: at most **64 rows and aggregate 2MiB**, counting
owned fields and per-row accounting, not payload alone. Unknown metadata is
filtered without filling that queue. Topology replacement, route checks,
rendering and filesystem work happen outside the callback. Overflow of either
queue dimension is terminal. Queue limits differ from the unchanged native
service quantum of 64 operations/1MiB.
READY requires a nonzero current epoch, actual `GIN_CONNECT_READY`, **every**
required correlated topic JOINED, current route eligibility, explicit plain
policy and observed receive drain. A successful service quantum alone may stop
at the 64-operation/1MiB budget with unread metadata still buffered. The owner
requires the native local CURLE_AGAIN observation with no partial TEXT message;
otherwise it continues bounded service under the same startup deadline. This is
not an atomic server cut or a promise against later inventory changes. READY is
also not an output transaction: earlier admitted records may already have been
appended. HTTP success, server ACK emission, WSS upgrade and local-runtime READY
are not substitutes.

Optional `-R FD` borrows an already nonblocking pipe write end >=3, duplicates
it close-on-exec and preserves caller flags. After that predicate it attempts
one fixed `READY\n` record within the actual PIPE_BUF; retries on EINTR are
bounded. Full/broken pipe, zero or short progress is terminal, not an unbounded
spin or silent success. No status fd means internally READY without a textual
announcement. This pipe is READY-only and unchanged; operation reporting uses `-O` only. No arbitrary stdout/stderr writes occur while owning resources.
The new CLI commands ignore SIGPIPE for diagnostics; the reusable owner saves/
restores its caller's signal policy and rejects pre-existing pending SIGPIPE
before changing it. The library emits only static error codes, never payloads.

## Selected-window output and failure limits

Only eligible records for the selected network/window reach the existing native
plaintext renderer. Other planned transport topics do not create other logs.
Output uses descriptor-relative private-window guards and checked append through
the one held root. Existing bytes/inodes are preserved; no truncate, path repair,
credential/payload diagnostic dump or automatic migration is attempted.

A rendering, required-subscription, established-query-route or output failure is
terminal. Checked append accounts for a written prefix: an error can leave that
prefix in the log. There is no rollback, automatic replay, crash-atomic append,
durable checkpoint or exactly-once guarantee. Explicit `mode=plain` remains an
owner assertion; it is not complete encrypted-content detection or server
attestation, and there is no automatic plaintext downgrade or decryption.

## Connected FIFO, controls and OP1

Submit complete LF-terminated records in single atomic writes, within both the
actual PIPE_BUF and [input](INPUT.md)/[command](CONTROL.md) bounds. Literal text
is SEND; the closed grammar also accepts `/join CHANNEL`, `/part CHANNEL` and
`/query NICK`. `//` escapes a leading slash. There is no raw command passthrough,
PART reason or multiple-target expansion. Invalid/unsupported records terminate
the owner; empty records are ignored. No local echo or implicit retry occurs.

One bounded framing quantum runs per turn after WSS service, full owned-queue
projection and current receive drain, never through unfinished TEXT. A pending
query blocks further FIFO admission until its correlated terminal receipt is
taken. Commands do not replace the selected destination.

SEND uses the original selected wire target/role. SEND/JOIN/PART run fresh
anonymous config, UUID-bound `/me` and one typed HTTP mutation outside callbacks,
using the explicit plaintext assertion. JOIN/PART retain the original argument
and exact selected network slug; `202 {"ok":true}` is accepted pending membership,
not observed membership. A PART matching the selected canonical channel ends
the owner after its terminal report, without executing buffered following text.
Other JOIN/PART operations keep the same selected owner.

QUERY uses the existing asynchronous user-root `open_query_window` WSS operation
with the topology's numeric network ID and explicit PLAIN assertion. There is no
HTTP query endpoint or new local log/subscription/window on ACK alone. `join_ms`
also bounds the query from enqueue (1..60000 ms). ACK means acceptance only,
not inventory or delivery. Native query expiry fails the connection; the query
receipt still reports UNCERTAIN with TIMEOUT code 9. Error may follow effects.

### Optional operation receipts: `-O FD`

The separate nonblocking pipe receives bounded, payload-free LF records:

```text
OP1 <decimal-id> <KIND> <STATE> <decimal-code>
```

IDs are nonzero, increasing per run, never wrap and are not persistent. Code is
<=65535 and belongs to the kind's native enum, not HTTP status. Decimal fields
have no sign or leading zeros except code 0. Records are at most 192 bytes
including LF and exclude targets, text, credentials, URLs, refs and timestamps.

A valid operation publishes PENDING before preflight/submission and attempts at
most one terminal publication. Invalid input instead reports INPUT REJECTED,
without PENDING. Empty records produce no receipt. Without `-O`, the same
functional operations run without OP1 output; `-R` remains READY-only.

Allowed states, in addition to PENDING, are:

- INPUT: REJECTED only (no PENDING).
- SEND: NOT_ATTEMPTED, ACCEPTED_PERSISTED, ACCEPTED_NO_PERSIST,
  REMOTE_FAILURE or UNCERTAIN.
- JOIN/PART: NOT_ATTEMPTED, ACCEPTED_PENDING, REMOTE_FAILURE or UNCERTAIN.
- QUERY: NOT_ATTEMPTED, ACCEPTED_ACK, REMOTE_FAILURE, NOT_SENT or UNCERTAIN.

Local/pre-enqueue rejection is NOT_ATTEMPTED. After query enqueue, abortive close
preserves NOT_SENT if no bytes were written, otherwise UNCERTAIN; it never
relabels uncertain effects as not attempted. A pending query is closed and its
receipt taken/reported before connection free. The primary owner result remains
primary; publication failure makes an otherwise graceful stop fail.

Publication uses bounded nonblocking attempts, including bounded EINTR retries.
There is no receipt queue or backpressure wait. Failed PENDING publication aborts
before the operation; failed terminal publication cannot roll back effects.
Acceptance states do not promise recipient delivery, durable exactly-once
processing or safe automatic resend.

## Shutdown, EOF and deadline limits

SIGINT/SIGTERM request orderly stop. Handlers only set a flag and write an
errno-preserving nonblocking wake byte. Existing synchronous aggregate fetch,
start and send/control APIs are **not immediately cancellable**: later internal
requests can occur before an entered aggregate returns. The owner checks stop
between phases, after service and again after PENDING before HTTP/query
submission. This deterministic boundary is not atomic network cancellation.
The 50ms service wait is a scheduling quantum, not a hard duration guarantee.
Deadlines do not bound filesystem stalls, resolver cleanup or scheduling.

Without `-E`, stdin is ignored, including `/dev/null`. Explicit `-E` requires an
already nonblocking stdin pipe read end: only an observed zero-byte read is an
orderly stop. Buffered data fails; HUP alone is not EOF. Stdin is not a command
pipe and its descriptor flags are unchanged.

Clean FIFO writer EOF is different: the owner retains its reader and applies a
checked 100ms monotonic cooldown, without close/reopen or busy-spinning. A partial
record at EOF is rejected as invalid input and terminates the owner; it is not
joined to a later writer's bytes.

Teardown frees transport, snapshots and owned metadata, clears owned credential
copies, cleans HTTP, closes children, restores signal actions, releases the root and finally
restores the caller mask. Wake ends stay valid while the handler can
write. Close is not retried; the persistent lock is never unlinked. Cleanup
failures are surfaced, not hidden by successful earlier phases. Regular stderr
diagnostics happen only after cleanup, so a blocked error sink cannot retain
the root. These APIs are single-owner/single-threaded; do not combine their HTTP
or signal ownership concurrently with another Gin owner or fork a live handle.

Exit 0 means valid dry check, explicit enrollment success, or orderly foreground
stop, **not recipient delivery or necessarily reaching READY**. Exit2 is malformed
CLI/settings; exit1 is runtime/unsafe-file/credential/TLS/protocol/route/status/
input/resource/cleanup failure. Stopping does not automatically logout/PART,
delete credentials or remove/rename any existing logs.

## What remains outside this owner

Heartbeat/reconnect/history/restart integration is 18.4 and remains unwired.
Native adapters do not enable these features through `connect`. The owner does
not decrypt or claim complete encrypted-content detection; only the explicit
plaintext subset is supported. No auto retry or outgoing replay is enabled.
Without Phoenix heartbeat policy, there is **no sustained healthy-session
lifetime promise**: server close is terminal. There is no live qualification,
E2EE, exactly-once persistence, recipient-delivery guarantee or beta claim.
