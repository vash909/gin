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
filesystem smoke test. It is sequential and bounded by the caller; it uses
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

Read Grappa's current client protocol and the linked source at the revision
recorded in [PROTOCOL.md](PROTOCOL.md). Before changing a feature floor, add
fixtures for the actual field/action shapes and update the reference revision
explicitly. Do not import Grappa's entire source tree or auto-regenerate a
claimed contract from whatever happens to be on upstream main.
