# Foreground local runtime

`gin run [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]`
uses the same explicit configuration, precedence and lexical validation as
`init` and `check`. Validation finishes before output-tree mutation. It
initializes/reuses exactly one configured window, holds one non-waiting root
writer lock throughout runtime resource ownership, and opens its safe existing input FIFO.
It remains a foreground single process: no fork, daemon, service or network.

Startup writes a static **starting local input; transport unavailable; sending
unsupported** diagnostic to stderr before acquiring any runtime resources. It
announces intent, not readiness: initialization can still fail. The CLI has no
readiness-output protocol; a bounded successful nonblocking writer open of the
actual FIFO establishes a local reader (not transport availability). Stdio can
block before acquisition or after cleanup, never in a callback or while owning
the root lock. In these outer phases the caller's original signal dispositions
apply; orderly exit 0 describes termination handled inside the runtime only.
Inherited stderr flags are never changed. A startup diagnostic failure returns
1 without starting the runtime. Error diagnostics are emitted only after all
runtime resources are released. The first nonempty complete record fails closed with exit 1 and
an unsupported-sending diagnostic. Empty LF records are ignored. Invalid or
oversized complete records, and partial records terminated by observed EOF,
fail with exit 1 and a static invalid-input diagnostic. No diagnostic contains
input bytes. No input, acknowledgement or placeholder is written to `out`;
existing log bytes and inodes are preserved. SIGINT/SIGTERM stop with exit 0
unless cleanup fails. Startup, I/O and cleanup failures return 1; invalid
settings return 2. A successful FIFO write is never delivery or acceptance.

The [input byte policy and cooperating writer contract](INPUT.md) still apply:
query the actual FIFO's PIPE_BUF and submit a complete LF record in one atomic
write within both that bound and the separate local payload limit. No commands,
shell evaluation, REST, TLS, authentication, Phoenix, history or sending exist.
This does not establish Grappa protocol compatibility. The separate
[synchronous REST/TLS API](HTTP.md) is not called from this runtime or its
callbacks; network integration remains future work.

## API, ownership and scheduling

`include/gin_runtime.h` / `src/runtime.c` expose `gin_runtime_run()`. The runtime
owns the held root, input descriptor and both wake-pipe descriptors. The input
is O_RDONLY/nonblocking/close-on-exec; there is no O_RDWR keepalive or artificial
writer. Wake ends use portable pipe plus fcntl NONBLOCK/CLOEXEC. All input/wake
descriptors close before lock/root release, with first error preserved; close
is never retried. It never independently opens/closes another lock inode fd.
The [POSIX per-process locking caveat](LOCKING.md) remains a caller obligation.

Callbacks receive READY, LINE, EMPTY or REJECTED and borrowed length-aware
record storage. Return 0 to continue, 1 to stop cleanly, or -1 with errno to
fail. Callbacks must be bounded and nonblocking; no retained record pointer,
blocking work, recursive runtime invocation, descriptor manipulation, signal
changes, fork or threads. The API is deliberately process-singleton and
single-threaded, not a generic multi-instance reactor. Call with SIGINT and
SIGTERM unblocked if they should terminate the runtime. Other signals that
interrupt poll/read fail closed with EINTR rather than an unbounded retry loop.

One parser call is one quantum (at most 512 scanned bytes and one read).
Signals are checked between every quantum and callback. Buffered coalesced
records and MORE schedule the next quantum directly, without waiting for
kernel readiness. AGAIN waits in POSIX poll. EOF retains the securely opened
FIFO inode and excludes it from poll for a monotonic 100ms backoff; poll waits
on the self-pipe with the remaining positive timeout. This avoids level-triggered
HUP spin and supports later writers with at most about 100ms scheduling delay.
It never reopens a pathname or treats HUP alone as EOF. POLLERR/POLLNVAL and
read/poll errors fail closed. There are no zero-time busy polls or broad queues.

## Signals and teardown

Setup blocks SIGINT/SIGTERM while creating the wake pipe and installing saved
sigactions. Handlers only set a volatile sig_atomic_t flag and perform one
errno-preserving nonblocking write. Both termination signals are masked inside
the handler. The pipe read end remains open whenever the handler can run, so
no handler write races with closing/reusing its fd. A full wake pipe is harmless:
the flag remains set and the pipe is already readable. No handler uses stdio,
allocation or application cleanup. The calling mask is restored for the loop.

Teardown blocks the termination signals, restores both previous dispositions,
disables the handler's fd, closes input and wake ends, releases the held root,
then restores the original mask. Pending signals at that final boundary obey
the previous dispositions; they are not silently consumed. This contract relies
on a single thread and no concurrent signal/descriptor changes by callbacks.

## Evidence and limits

`tests/test_runtime.py` and the test-only `runtime_consumer.c` exercise actual
processes/FIFOs/poll, coalesced records, observed EOF/new writer, partial rejection,
empty/invalid policy, opaque high bytes, lock contention/reacquisition, signals,
unsafe startup and unchanged log bytes/inodes. Readiness and framing use pipe
handshakes; every child is bounded and reaped. CPU tests measure the exact
child's wait4 user+system resource usage over bounded select intervals, with
no /proc dependency and a generous spin threshold. C assertions stay enabled
under NDEBUG. GIN_RUNTIME_PREFIX is development-test-only and permits actual
Valgrind execution of start() CLI/consumer children; it is not a public CLI mode.
The same-process API helper (including deliberately tiny RLIMIT_NOFILE tests)
and deterministic boundary/stdio helpers run without that prefix. The API helper
checks descriptor counts and handler/mask restoration directly, not via Valgrind.
Boundary tests compile only runtime.c with portable function-name substitutions,
link the real parser, and raise real SIGTERM at initial unmask and successful
record parsing. A separate gin.c stdio seam raises SIGTERM immediately before
real blocking fputs on a full pipe; ordinary CLI error-output tests also prove
root reacquisition under backpressure. These are development-only linked test
executables, not production test modes. Privileged ownership tests run
in the existing isolated root CI lane. Linux local results do not substitute
for real macOS and FreeBSD CI. No live-server interoperability is claimed.
