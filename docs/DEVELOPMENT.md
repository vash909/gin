# Development

The separate [verified-WSS native connection API](CONNECT.md) adds isolated
root-first subscriptions through libcurl >=8.23.0. The CLI remains local-only;
No live Grappa interoperability is claimed.

The [isolated discovery and gated-auth APIs](DISCOVERY.md) are exercised by
`make check` in every existing compiler/platform lane. Decoder unit tests and
real native HTTPS fixtures use synthetic policies, not a qualified gin wire
declaration. No CLI/network-runtime integration is introduced.

## Requirements

The executable needs a C99 compiler, POSIX.1-2008 headers/libc and POSIX make.
The whole application requires libcurl >=8.23.0 with TLS, asynchronous DNS
and WS/WSS enabled. Older stock packages are insufficient; use the pinned
[native curl recipe](CONNECT.md) when necessary. Account authentication requires Jansson >=2.14. Tests use Python 3 and OpenSSL, never at runtime. See
[HTTP.md](HTTP.md) for API limits, curl-config/pkg-config overrides and the
pinned user-prefix musl TLS recipe.
No SDK, Grappa checkout, BEAM, Node, containers or cloud credentials are needed.

On Debian/Ubuntu, inspect tools without changing the system:

```sh
sh scripts/bootstrap.sh
```

Explicitly install the development toolchain and planned adapter headers:

```sh
sh scripts/bootstrap.sh --install
```

This installs system packages through apt and sudo; it does not modify shell
startup files, download arbitrary installers or start services. It includes GCC,
Clang/static tools, make/BSD make, musl, GDB, Valgrind, cppcheck, ShellCheck,
Python 3, pkg-config and libcurl/Jansson/OpenSSL development packages.
libcurl and Jansson are linked; libwebsockets is not required. Package
installation alone does not establish the required curl version or WS support.

Set environment `CURL_CONFIG` to select the host `curl-config` checked by
`scripts/bootstrap.sh` and used by the host builds in `scripts/check.sh`;
it defaults to `curl-config`. Ordinary `make` still takes an explicit
command-line `CURL_CONFIG=`. Use `CURL_CONFIG_MUSL` only for the genuine-musl
gate in `scripts/check.sh`. The pinned musl recipe in
[HTTP.md](HTTP.md) is a Linux developer toolchain recipe, not a macOS/BSD
requirement.

On macOS use the Command Line Tools, Python 3, Homebrew curl, Jansson, pkgconf and OpenSSL;
select it with `make CURL_CONFIG="$(brew --prefix curl)/bin/curl-config" check`.
On FreeBSD use
the base C compiler/make and packaged Python 3, curl, Jansson, pkgconf and OpenSSL. The full Linux development gate uses
Linux-specific developer packages, but the application itself does not.
Install optional analysis tools through the host package manager.

## Build and verify

```sh
make
make check
CURL_CONFIG_MUSL="$GIN_MUSL_PREFIX/bin/curl-config" sh scripts/check.sh
```

The full local gate runs documentation checks, ShellCheck, cppcheck, GCC,
Clang, musl, BSD make, ASan/UBSan, staged install/uninstall and a Valgrind
filesystem smoke test plus real foreground-runtime Valgrind tests. It is
sequential and bounded by the caller; it uses
`.build/`, does not contact Grappa and does not send any messages.

Individual portable checks:

```sh
make clean
make CC=clang check
make clean
bmake CC=cc check
make clean
make CC=musl-gcc CURL_CONFIG="$GIN_MUSL_PREFIX/bin/curl-config" \
  JANSSON_CFLAGS="-I$GIN_MUSL_PREFIX/include" \
  JANSSON_LIBS="-L$GIN_MUSL_PREFIX/lib -ljansson" LDFLAGS=-static check
```

Use strict warning flags supplied by the Makefile even when overriding CFLAGS.
Sanitizers and Valgrind are development-only, not runtime requirements.
ASan uses a non-PIE executable in the Linux gate to avoid address-space mapping
collisions on hosts with restrictive/randomized mappings; this is not a
production build setting. CI tests the non-sanitized executable as well.

## Pure Phoenix TEXT testing

The [Phoenix API](PHOENIX.md) adds no dependency or runtime command. Every
`make check` lane runs `.build/test_phoenix` (real codec/owned cleanup),
`.build/test_phoenix_stream` (real collector + real codec, split and fault cases),
`.build/test_phoenix_collect` (standalone decode facade, collector contract only)
and `.build/test_phoenix_refs` (independent local counter). The facade must not
link the actual codec; otherwise it defines decode twice. The real core unit
includes `src/phoenix.c` for test-only allocation failure/wipe checks; the real
stream unit includes `src/phoenix_stream.c` and separately links the core.
Makefile prerequisites include the implementation files, without compiling an
included source twice. Isolated Jansson allocator injection is test-only and
restores the allocator after freeing every DOM. Some internal library allocation
failures may be classified as malformed JSON; no perfect OOM classification is
claimed. All four units also run under Valgrind in CI and `scripts/check.sh`;
normal platform, musl, NDEBUG and sanitizer `make check` lanes include them.
Fixtures need no server and do not qualify a real WebSocket connection.

## Owned topology testing

The [topology API](TOPOLOGY.md) adds no dependencies. `make check` includes the
real pure snapshot/ownership unit, a separate read-accessor-facade lookup contract
unit, and synthetic verified-HTTPS fetch fixtures. The pure unit includes the
actual core translation unit to inject deterministic owned-allocation failures;
never link that unit with a second `src/topology.c`. The lookup facade unit links
only lookup plus naming, not real parser accessors. Production has no allocation
fault flags or replacement global Jansson allocator.

`scripts/check.sh` runs these through the existing GCC, Clang, genuine static
musl, BSD make and sanitizer lanes, adds an explicit `-DNDEBUG` lane, and runs
pure/lookup/fetch tests under Valgrind. `GIN_TOPOLOGY_DRIVER` selects a private
fetch driver; `GIN_TOPOLOGY_PREFIX` instruments fixture requests. None of this
qualifies a live Grappa deployment. Linux, macOS and FreeBSD CI remain required.

## Install without root for inspection

```sh
make DESTDIR="$PWD/.build/stage" PREFIX=/usr install
.build/stage/usr/bin/gin --version
make DESTDIR="$PWD/.build/stage" PREFIX=/usr uninstall
```

Normal installation can use `PREFIX="$HOME/.local"` without sudo. The manual
is installed under `share/man/man1`. No service or shell startup integration is
installed. Running gin or activating future network access is always explicit.

## CLI/configuration scope

The [CLI contract](CLI.md) covers implemented `init`, read-only `check` and
explicit bounded `-f` parsing. `tests/test_cli.py` exercises these through the
same `make check` path used by compiler, sanitizer and platform CI gates.
`tests/test_naming.c` is compiled and run by **every** `make check` compiler,
sanitizer, BSD make and platform CI path. It tests canonical round trips,
malformed/alias decoding, ASCII/Unicode distinctions, output capacities and
raw/expanded boundaries. UTF-8 boundary cases measure bytes and expansion.
Black-box tests cover CHANTYPES/config precedence, case-disjoint network paths,
strict UTF-8, encoded-path symlink/hardlink/mode rejection and log/inode reuse.
See [NAMING.md](NAMING.md) for API and pre-beta migration policy.
Configuration loading and naming link only libc. Tests use private synthetic files;
foreign-owner tests require root and are skipped in ordinary developer runs.
`tests/lock_holder.c` and `tests/test_locking.py` also run in every `make check`
path. The pipe-controlled C child keeps assertions enabled under `-DNDEBUG`;
explicit readiness/release handshakes and confirmed child exit replace sleeps.
They cover held-root API use, cross-target/relative-alias contention,
independent roots, ordinary and SIGKILL release, persistent inode/data reuse,
partial-layout recovery, unsafe locks and read-only checks during ownership.
Children have bounded watchdogs and are always reaped. See
[LOCKING.md](LOCKING.md) for the process-associated lock ownership contract.
`tests/test_input.c`, `tests/fifo_consumer.c` and `tests/test_input.py` run in
every `make check` path, including sanitizers. They exercise real pipes/FIFOs,
fixed buffer bounds, invalid-record discard/resync, coalescing, EAGAIN, observed
EOF and invisible writer handoff, cooperating atomic writers, safe access and
root-lock retention. See [INPUT.md](INPUT.md) for the byte policy and limitations.
Assertions remain enabled under `-DNDEBUG`. New foreign-owner cases run in the
existing isolated privileged CI lane; non-root local runs skip them.
No test needs credentials or a live server. Foreground `run` now implements
local-only input, not sending. `tests/test_runtime.py` and its test-only C
consumer run in every make check path. See [RUNTIME.md](RUNTIME.md) for the
callback API, CPU/resource tests, EOF policy and signal ownership contract.

## GitHub workflow

Work on a branch and open a PR. Main requires a PR and the successful `gate`
check, with up-to-date status checks and admin enforcement. No minimum review
approval is required for a single-maintainer repository. Never bypass checks.

CI runs:

- Linux GCC/Clang and macOS Clang builds/tests and staged install/uninstall.
- FreeBSD base compiler/make tests in a pinned VM action.
- musl and BSD make compatibility tests.
- ShellCheck, cppcheck, documentation checks, Valgrind and ASan/UBSan on Linux.
- A fail-closed aggregate `gate`; a failed/skipped/cancelled prerequisite fails
  the gate rather than allowing a partial matrix through.

Workflow permissions default to read-only; third-party actions are pinned to
full commit SHAs. Fork PR code has no deployment credentials. Passing main
builds upload a source archive and SHA-256 checksums with short retention.
These are source snapshots, not a working-client release or signed binaries.
There is no automatic production deployment or release publication. The beta
release procedure is roadmap item 20 and must wait for real interoperability.

CI does not exercise a live Grappa server. Protocol fixture checks and opt-in
live qualification are separate future work; never describe a filesystem test
as a successful chat exchange.

## Refresh upstream references

Read the [offline snapshot and update policy](upstream/grappa/README.md), then
review upstream changes at an explicit commit against that reference. Verify
the copied files without a network connection:

```sh
(cd docs/upstream/grappa/87daa4cca9b41fbd6c5f93cb40d6848193f425f9 && sha256sum -c SHA256SUMS)
```

On macOS, use `shasum -a 256 -c SHA256SUMS` in the snapshot directory. Checksums
verify file bytes, not upstream authenticity or runtime compatibility.
Documentation-only changes need provenance, license, checksum and link review,
not a local application test run. Existing required PR CI still runs normally.

Keep old snapshots unchanged, add a new revision directory and update the active
pointer and gin's guides together through a PR. Review the source linked in
[PROTOCOL.md](PROTOCOL.md) before raising a feature floor; add implementation
fixtures when behavior changes. Do not import Grappa's runtime source tree or
auto-regenerate a claimed contract from upstream main. Each future release must
record the qualification evidence specified in [COMPATIBILITY.md](COMPATIBILITY.md).

The isolated [incoming-event renderer and append API](RENDER.md) projects
owned, bounded records and supports held-root append outside callbacks. Native
loopback WSS fixtures exercise it; the CLI remains local-only and no live Grappa
qualification is claimed.

Plain-send native tests are part of `make check`: `tests/test_send.py` composes
a real FIFO, HTTPS and WSS receive path outside callbacks. `GIN_SEND_PREFIX`
selects an instrumentation wrapper and `GIN_SEND_DRIVER` a private driver.
See [SEND.md](SEND.md) for limits and scope; fixtures are not live qualification.
