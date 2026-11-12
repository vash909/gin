```text
       :####:   ######   ###   ##
   ######   ######   ###   ##
 :##:  .#     ##     ###:  ##
 ##:          ##     ####  ##
 ##.          ##     ##:#: ##
 ##           ##     ## ## ##
 ##  ####     ##     ## ## ##
 ##. ####     ##     ## :#:##
 ##:   ##     ##     ##  ####
 :##:  ##     ##     ##  :###
  #######   ######   ##   ###
   :####.   ######   ##   ###

 —. .. -.
```

# gin

A small, headless Unix frontend for [Grappa](https://github.com/vjt/grappa-irc),
inspired by [ii](https://tools.suckless.org/ii/). Portable C99 and POSIX.1-2008;
ordinary files and named pipes instead of another chat UI.

**Status: development scaffold, not a working Grappa client or a beta.**
The executable initializes a private filesystem tree and validates explicit
CLI/configuration settings. It does
not authenticate, connect or send messages. `gin run` is a genuine local-only
foreground FIFO reader with orderly signal shutdown and one held root lock.
It announces transport unavailability and fails with exit 1 on the first
nonempty complete input; no input is echoed to logs or claimed delivered.
See [foreground runtime](docs/RUNTIME.md) and the
[input policy and writer contract](docs/INPUT.md). Without a running reader,
a shell FIFO writer blocks. REST and Phoenix Channels
are the planned transport, not direct IRC.

## Build and test

A C99 compiler, POSIX make and TLS-enabled libcurl >=8.23.0 development
headers/libraries and Jansson >=2.14 are required to build. Tests additionally require Python 3
and OpenSSL (development only). See the [isolated REST/TLS API](docs/HTTP.md)
for dependency selection and genuine musl builds; the CLI remains local-only.

```sh
make
make check
.build/gin --help
.build/gin --version
```

## Try the implemented scaffold

Choose an existing private parent directory. The root itself must be either
absent or a private directory owned by your user. See the man page for exact
path and naming restrictions.

```sh
.build/gin init -d "$HOME/gin" -n azzurra -c '#grappa'
ls -ld "$HOME/gin/azzurra/grappa"
ls -l "$HOME/gin/azzurra/grappa/in" "$HOME/gin/azzurra/grappa/out"
```

`in` is a FIFO; `out` is a regular log file, never truncated by initialization.
Directories are private (`0700`), files and FIFOs private (`0600`). Repeated
initialization must preserve existing log contents and reject unsafe paths.
`init` holds a non-waiting POSIX advisory writer lock at `ROOT/.gin.lock`
before changing any network/window/FIFO/log entry and releases it on exit.
The private regular lock file persists unchanged; never delete it to recover.
The kernel releases the lock on close or process death. Existing safe partial
layouts can be completed without deleting logs. This is local-filesystem,
cooperative locking, not a running writer or distributed/NFS guarantee.
See [locking, recovery and API limits](docs/LOCKING.md), including POSIX
per-process duplicate-close semantics and the trusted-parent assumption.

The intended beta interface from the [original proposal](https://github.com/vjt/grappa-irc/issues/2588)
is shown below. **This is a design example, not functionality available today:**

```sh
# Future: a running, authenticated gin process must already own the FIFO.
echo 'Hello from gin!' > "$HOME/gin/azzurra/grappa/in"
tail -f "$HOME/gin/azzurra/grappa/out"
```

## CLI and explicit configuration

`gin check -d ./chat -n azzurra -c '#grappa'` validates settings read-only;
it is not a filesystem or server probe. `init` and `check` accept optional
`-f FILE` with exact `root`, `network`, `target`, `chantypes` key=value settings.
`-T CHANTYPES` overrides file `chantypes`: a distinct subset of `#&+!`, default
`#`; an empty value disables channels. Targets use strict UTF-8 and ASCII-only
identity folding. See the [reversible naming contract](docs/NAMING.md).
**Pre-beta layout change:** uppercase network letters are escaped (`Net` ->
`%4Eet`) to prevent case-insensitive filesystem collisions while preserving
slug case. Use a new root or a reviewed manual migration; gin never renames,
deletes or automatically migrates old logs. CLI flags
override file values; effective root/network/target settings are required. The file must be
owned by the effective user, single-link, regular and exactly `0600`, without
a final symlink. No implicit discovery, config environment variables or
credentials are supported. Relative paths use the invocation cwd.

See [CLI/configuration](docs/CLI.md) for grammar, bounds, security, exit codes
and the foreground/no-daemon `gin run` contract. `run` accepts the same settings
but implements local input only, not a connected messaging client.

## Design boundaries

- Independent companion repository: no Grappa checkout, submodule, BEAM, Node,
  frontend toolchain or server deployment is required to build gin.
- C99/POSIX core, foreground single-process design, bounded buffers and explicit
  errors. No TUI, database, plugin framework or custom cryptographic protocol.
- Isolated synchronous REST/TLS and account [authentication APIs](docs/AUTH.md),
  with private credential storage; libcurl and Jansson are linked. They are not
  wired into CLI/runtime callbacks. Isolated [HTTP protocol discovery and gated
  authentication](docs/DISCOVERY.md) are implemented, alongside isolated
  [owned network/window topology](docs/TOPOLOGY.md) fetch and pure snapshot APIs.
  REST boot leaves query inventory unknown until a decoded user-topic snapshot.
  The isolated [Phoenix v2 TEXT API](docs/PHOENIX.md) adds bounded encode/decode,
  explicit-message fragment collection, reply identity matching and local refs;
  the isolated [verified-WSS connection API](docs/CONNECT.md) adds root-first
  correlated subscriptions through libcurl. CLI/runtime integration remains future work.
- Speak Grappa's documented REST/Phoenix contract and terminology; keep the
  upstream revision and compatibility requirements explicit.
- E2EE is deferred, **not TLS**. Encrypted conversations must eventually be
  rejected clearly until supported, never silently downgraded to plaintext.
- No live server, credentials or real messages are used by scaffold tests.

## Development

Start with [development setup](docs/DEVELOPMENT.md), [contribution rules](CONTRIBUTING.md)
and [agent guidance](AGENTS.md). The [20-step roadmap](ROADMAP.md) defines the
path to a first beta; post-beta E2EE is tracked separately.

- [CLI/configuration contract](docs/CLI.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Grappa protocol contract and pinned references](docs/PROTOCOL.md)
- [Offline upstream documentation snapshots](docs/upstream/grappa/README.md)
- [Compatibility and release-record policy](docs/COMPATIBILITY.md)
- [Third-party attribution](THIRD_PARTY_NOTICES.md)
- [Security policy](SECURITY.md)
- [Manual](man/gin.1)

CI exercises Linux GCC/Clang, macOS, FreeBSD, musl, BSD make, sanitizers,
static analysis and staged installation. Main is PR-only with a required CI
gate, including for administrators. Passing main builds produce source archives
and SHA-256 checksums, not signed binaries or production deployments.

MIT licensed. Only attributed protocol documentation and its upstream license
are vendored; no Grappa runtime source is copied or linked. Grappa is an
independent upstream project; gin is not an official Grappa frontend.

The isolated [minimal window-control APIs](docs/CONTROL.md) add a closed FIFO
command frontend, REST JOIN/PART and one correlated WSS query-opening operation.
Real FIFO/HTTPS/WSS fixtures exercise them; unsupported commands fail closed,
`//` escapes literal leading slashes, and `gin run` remains local-only.

The isolated [plain-send native API](docs/SEND.md) adds fresh config and stored-identity
validation before one bounded REST POST. Real FIFO/HTTPS/WSS fixtures exercise
composition outside callbacks; `gin run` remains local-only, with no send queue,
retry or local echo.

The isolated [incoming-event renderer and append API](docs/RENDER.md) projects
owned, bounded records and supports held-root append outside callbacks. Native
loopback WSS fixtures exercise it; the CLI remains local-only and no live Grappa
qualification is claimed.

The opt-in [native liveness API](docs/LIVENESS.md) adds Phoenix heartbeats,
monotonic deadlines, finite owner-driven reconnect scheduling and fail-closed
session-severance handling. It never silently replays user operations or makes
`gin run` connected; history/gap recovery and live qualification remain future work.
