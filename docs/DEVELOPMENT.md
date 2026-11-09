# Development

## Requirements

The executable needs a C99 compiler, POSIX.1-2008 headers/libc and POSIX make.
The current scaffold links only libc. Tests use Python 3, never at runtime.
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
Python 3, pkg-config and the libcurl/libwebsockets/Jansson/OpenSSL development
packages. The latter libraries prepare for future adapters; they are not
required or linked by today's core.

On macOS use the Command Line Tools plus Python 3; on FreeBSD use the base C
compiler/make and the packaged Python 3. The full Linux development gate uses
Linux-specific developer packages, but the application itself does not.
Install optional analysis tools through the host package manager.

## Build and verify

```sh
make
make check
sh scripts/check.sh
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
make CC=musl-gcc check
```

Use strict warning flags supplied by the Makefile even when overriding CFLAGS.
Sanitizers and Valgrind are development-only, not runtime requirements.
ASan uses a non-PIE executable in the Linux gate to avoid address-space mapping
collisions on hosts with restrictive/randomized mappings; this is not a
production build setting. CI tests the non-sanitized executable as well.

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
