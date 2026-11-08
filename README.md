# gin

A small, headless Unix frontend for [Grappa](https://github.com/vjt/grappa-irc),
inspired by [ii](https://tools.suckless.org/ii/). Portable C99 and POSIX.1-2008;
ordinary files and named pipes instead of another chat UI.

**Status: development scaffold, not a working Grappa client or a beta.**
The executable currently initializes a private filesystem tree only. It does
not authenticate, connect, consume FIFO input or send messages. Do not write to
the FIFO yet: without a reader, a shell writer blocks. REST and Phoenix Channels
are the planned transport, not direct IRC.

## Build and test

Only a C99 compiler and POSIX make are required to build. Tests additionally
require Python 3 (development only).

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

The intended beta interface from the [original proposal](https://github.com/vjt/grappa-irc/issues/2588)
is shown below. **This is a design example, not functionality available today:**

```sh
# Future: a running, authenticated gin process must already own the FIFO.
echo 'Hello from gin!' > "$HOME/gin/azzurra/grappa/in"
tail -f "$HOME/gin/azzurra/grappa/out"
```

## Design boundaries

- Independent companion repository: no Grappa checkout, submodule, BEAM, Node,
  frontend toolchain or server deployment is required to build gin.
- C99/POSIX core, foreground single-process design, bounded buffers and explicit
  errors. No TUI, database, plugin framework or custom cryptographic protocol.
- Planned isolated network adapters: libcurl (REST/TLS), libwebsockets
  (WebSocket/TLS), Jansson (JSON). They are not linked by the scaffold.
- Speak Grappa's documented REST/Phoenix contract and terminology; keep the
  upstream revision and compatibility requirements explicit.
- E2EE is deferred, **not TLS**. Encrypted conversations must eventually be
  rejected clearly until supported, never silently downgraded to plaintext.
- No live server, credentials or real messages are used by scaffold tests.

## Development

Start with [development setup](docs/DEVELOPMENT.md), [contribution rules](CONTRIBUTING.md)
and [agent guidance](AGENTS.md). The [20-step roadmap](ROADMAP.md) defines the
path to a first beta; post-beta E2EE is tracked separately.

- [Architecture](docs/ARCHITECTURE.md)
- [Grappa protocol contract and pinned references](docs/PROTOCOL.md)
- [Security policy](SECURITY.md)
- [Manual](man/gin.1)

CI exercises Linux GCC/Clang, macOS, FreeBSD, musl, BSD make, sanitizers,
static analysis and staged installation. Main is PR-only with a required CI
gate, including for administrators. Passing main builds produce source archives
and SHA-256 checksums, not signed binaries or production deployments.

MIT licensed. No Grappa source is vendored. Grappa is an independent upstream
project; gin is not an official Grappa frontend.
