# CLI and configuration

## Implemented commands

```text
gin --help
gin --version
gin init  [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]
gin check [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]
```

`init` creates/reuses the private local projection described in [gin(1)](../man/gin.1).
The original all-flags invocation remains supported:
`gin init -d ./chat -n azzurra -c '#grappa'`.
`check` validates configuration syntax and effective settings **only**. It does
not inspect the output tree, test filesystem accessibility, probe a server,
connect, create directories, change permissions, open FIFOs or write logs.
A syntactically valid nonexistent or symlinked root can pass `check` but fail
`init`. Neither command consumes FIFO input or sends messages.

`init` takes a non-waiting POSIX advisory exclusive lock on the persistent,
private regular file `ROOT/.gin.lock` before any network/window/FIFO/log
mutation. A competing writer on the same root returns exit 1, even for another
network/target. The lock is held until child descriptors are closed, then
released on exit; it is not a continuing runtime. The kernel also releases
ownership on process death. Never unlink/rename/truncate the file to recover:
its bytes are opaque, not a trusted PID record. Safe existing partial layouts,
logs and FIFOs are preserved, not cleaned up. `check` never acquires or probes
this lock. See [LOCKING.md](LOCKING.md) for the held-root API, cooperative
per-process semantics (including duplicate-close hazards), local-filesystem
scope, NFS/distributed exclusions and trusted-parent assumptions.

Every CLI flag takes one argument and may appear at most once, including `-f`.
Unknown flags, operands and commands are errors. CLI settings override file
settings regardless of flag order. Root, network and target are required;
CHANTYPES defaults to `#` and may explicitly be empty to disable channels.
Omitting `-f` reads no configuration file. There is no
implicit `~/.config` discovery and no configuration environment variables.
Relative `FILE` and root paths use the invocation's current working directory,
**not** the configuration file's directory. CLI values are not trimmed.

## Explicit configuration file

Create a regular file with mode exactly `0600`, owned by the effective user,
with exactly one hard link. For example, use a private editor and `chmod 600`
on a new file before invoking gin:

```text
# Local projection only; no credentials
root=./chat
network=azzurra
target=#grappa
```

Then run `gin check -f ./gin.conf` or `gin init -f ./gin.conf -c Alice`.
The latter overrides only the target. Files can provide any subset of settings;
CLI flags must supply anything missing. Duplicate keys (even identical ones),
unknown keys, empty root/network/target values and malformed lines are invalid
**even when CLI flags override every setting**. Bounds and raw-byte validation also apply to
overridden file content; naming/root lexical rules apply to effective values.

Grammar:

- Exact case-sensitive keys: `root`, `network`, `target`, `chantypes`; split at the first `=`.
  `chantypes` permits an empty value and otherwise at most four distinct prefixes
  from `#&+!`. CLI `-T` overrides it; duplicate empty keys/flags are still errors.
- Trim surrounding ASCII space and tab from each key and value.
- Ignore blank lines and lines whose first non-space/tab byte is `#`.
  No inline comments: `target=#grappa` keeps its `#` and `target=#a#b` keeps both.
- Accept LF, CRLF and an unterminated final line. A bare CR is invalid.
- Reject NUL, ASCII control bytes (except formatting tab/LF/CRLF) and DEL,
  including in comments. Non-ASCII bytes are not decoded or case-folded.
- No includes, interpolation, shell evaluation, escape or quoting syntax.
  Quotes, backslashes, `$` and `~` are literal bytes, not expansion operators;
  do not quote values as in a shell. Literal values still need to pass naming
  rules. Password/token options and keys are unsupported; never put secrets
  in argv or this file. Unknown keys are rejected without echoing their data.

Limits are byte counts, never character counts:

| Input | Maximum |
| --- | ---: |
| Whole file, including comments and line endings | 16384 (16 KiB) |
| Physical line, excluding LF or CRLF | 8192 (8 KiB) |
| Root value | 4095 |
| Network value | 255 |
| Raw target value | 256 |
| CHANTYPES value (empty allowed) | 4 |
| Encoded network or window filename | 255 |

File value limits apply after surrounding space/tab trimming. No input is
silently truncated. Target encoding may exceed 255 bytes before the raw limit
is reached. The [naming contract](NAMING.md) and [manual](../man/gin.1) define strict UTF-8
scalar validation, reversible canonical names, channel namespaces and limits.
Uppercase network letters are escaped, not folded; `Net` becomes `%4Eet`.
This is an incompatible pre-beta layout change: use a new root or explicitly
review/manual-migrate logs. Gin never renames, deletes or migrates old logs.
Root rejects empty values, ASCII controls/DEL and any `..` path component;
dot components and repeated slashes are accepted. `check` does not test a
filesystem's potentially smaller path/component limits.

## Safety and exit status

Configuration is opened read-only with `O_NOFOLLOW`, `O_NONBLOCK` and
`O_CLOEXEC`; metadata is validated through the opened descriptor before reading.
A final-component symlink, directory, FIFO, device, unsafe mode, foreign owner
or multiple hard links is rejected. A FIFO does not wait for a writer.
The reader owns and closes its descriptor, retries interrupted opens/reads and
accumulates short reads within the bound. It does not retry an interrupted
`close`, whose descriptor state is platform-dependent. The caller supplies
zero-initialized settings storage. Diagnostics do not echo keys, paths or values.

Keep configuration parent directories trusted: they are resolved normally,
may contain symlinks, and are not checked or repaired. This is not protection
against root, a hostile process sharing the effective UID, concurrent owner
changes or hostile filesystem semantics. Output initialization retains its
separate descriptor-relative, no-symlink security boundary.

| Exit | Meaning |
| --- | --- |
| 0 | Successful help, version, initialization or settings check; **not connectivity** |
| 1 | Configuration I/O/unsafe file, or operational filesystem failure |
| 2 | Malformed CLI/configuration, unknown command, missing/duplicate options, or invalid effective settings |

CLI parsing, complete file validation and effective lexical validation finish
before `init` touches the output tree. Invalid settings do not create or change
it. Operational filesystem failures may leave safe partial initialization,
which is repeatable but not transactional. Unsafe existing entries are never
repaired. Error precedence is CLI syntax, explicit file read/validation, then
effective settings: an unreadable explicit file returns 1 even if required
settings would also be missing. Successful `check` produces no output.

## Future foreground runtime (not implemented)

The intended invocation is
`gin run [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]`, using this same explicit
settings/precedence contract. It is a foreground process, with no implicit
fork, daemonization, background flag or installed service; a supervisor may
manage it externally. Roadmap item 6 must implement and test the actual event
loop and orderly signal shutdown before `run` becomes available. Later
transport/authentication milestones are also required for connected operation.
Today **`gin run` is unsupported and returns 2**, not a successful placeholder.
No credential flow is specified or accepted by this CLI milestone.
