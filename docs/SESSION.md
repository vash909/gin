# Operational startup, live reception and session ownership

`gin connect`, `gin connect-check` and `gin credential-import` are production
commands backed by the existing native authentication, discovery, topology and
verified-WSS APIs. `connect` now receives eligible live plaintext records for
**only the selected network/window** and appends them to its log. Connected
FIFO send/control, reconnect and history recovery are not wired in; this is not
a complete messaging client or a beta. The old `init`, `check` and local-only `run` retain their
behavior and four-key settings format. No existing invocation silently starts
network access. Tests use synthetic loopback authorities, not a live Grappa
server; interoperability remains unqualified.

## Explicit commands and private enrollment

```text
gin connect [-f SESSION_FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]
            [-e HTTPS_ORIGIN] [-m plain] [-a CA_FILE] [-R STATUS_FD] [-E]
gin connect-check [same options]
gin credential-import -d ROOT -e HTTPS_ORIGIN -s PRIVATE_BEARER_FILE [-a CA_FILE]
```

Flags may occur once, take no positional operands and do not read implicit
configuration or credential environment variables. `-E` is the only flag without
an argument. Relative paths use the invocation cwd, not the settings directory.
CLI strings are not trimmed. `-R` takes canonical decimal fd numbers >=3.

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

The owner acquires one root once and keeps it through all session resources.
It does not call the old local runtime or acquire a second root handle. Accepted
output creates/accesses only the selected window through that held root. Its
FIFO is not a connected input reader; no other discovered window is projected.
HTTP initialization/cleanup is explicit and surrounds all users. Effective CA
selection must be a trusted regular file, not a device/FIFO; a CA inode alias of
the held lock is refused without opening/closing it. Trusted parents and no
hostile same-UID namespace changes remain caller obligations.

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
announcement. No arbitrary stdout/stderr writes occur while owning resources.
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

## Shutdown, EOF and deadline limits

SIGINT/SIGTERM request orderly stop. Handlers only set a flag and write an
errno-preserving nonblocking wake byte. Existing synchronous aggregate fetch/
start APIs are **not immediately cancellable**: later internal requests can
still occur before the function returns. The owner checks stop and monotonic
startup policy between aggregate phases and after each service call. A 50ms
maximum service wait is a scheduling quantum, not a hard duration guarantee.
Request/startup thresholds do not bound filesystem stalls, resolver-thread
cleanup, OS scheduling or library teardown in hard realtime.

Without `-E`, stdin is ignored, including `/dev/null`. Explicit `-E` requires
an already nonblocking stdin pipe read end: only an observed zero-byte read is
an orderly stop. Buffered data fails with a fixed unsupported-input error; HUP
alone is not EOF. This is not a message/command pipe. Inherited descriptor flags
are not changed. There is no chat FIFO reader in this milestone.

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

## What remains outside this receive-only owner

FIFO send/control integration is 18.3; heartbeat/reconnect/history/restart
integration is 18.4. Native adapters for those operations do not make them
available through `connect`. The owner neither decrypts nor claims complete
encryption detection; only the explicitly asserted plaintext subset is supported.
No auto retry or outgoing replay is enabled. Without Phoenix heartbeat policy,
this step makes **no sustained healthy-session lifetime promise**: server close
is terminal. There is no live Grappa qualification, E2EE implementation,
exactly-once persistence, recipient-delivery guarantee or beta release claim.
