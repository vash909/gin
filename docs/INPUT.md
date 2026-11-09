# Bounded local FIFO input API

## Implemented boundary

The reusable C input API is integrated into the local-only foreground
[`gin run` runtime](RUNTIME.md), not a messaging service.
`gin init` still only initializes and exits;
`gin check` stays lexical/read-only. Neither command calls the input reader.
The runtime schedules bounded parser work using poll and a signal self-pipe.
There is no command grammar, shell expansion, protocol dispatch, networking,
log writing or delivery acknowledgement. Leading `/`, quotes, `$`, backslashes
and other printable bytes are literal data, not commands.

## Safe existing-window access

`gin_root_input_open(root, network, target, chantypes)` in `gin_fs.h` returns a
caller-owned read descriptor or -1 with errno. Inputs must be non-NULL and
strings NUL-terminated. Pass one live `gin_root` acquired in the current process,
never a copied/inherited/manually constructed handle. Keep its lock held through
the entire input descriptor lifetime; close all inputs before root release.
See [LOCKING.md](LOCKING.md): opening/closing another descriptor for the lock
inode can release the process-associated lock. This function never opens,
closes or reacquires that lock.

Shared naming prepares the exact encoded network/window identity. The held root,
network and window must be effective-user-owned directories of exact mode 0700.
Traversal is descriptor-relative with O_NOFOLLOW on both child directories.
The existing `in` must be a FIFO, effective-user-owned, exact 0600 including no
special bits, with one link. Its no-follow path metadata is checked before open;
opened descriptor and path metadata are checked again and must match the
original device/inode. Open uses O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC.
It does not wait for a writer, use O_RDWR keepalive, create directories/FIFOs,
chmod, repair, delete, open a log or mutate log/lock bytes. Temporary directory
and failed input descriptors are closed on every path; no close is retried.
Open interruptions are returned to the caller, not retried internally.

The root acquisition API may create a root/lock as already documented; **input
opening itself** creates nothing. Trusted ancestors, cooperative local locking
and the existing same-UID/root/hostile-filesystem exclusions still apply.
Pre/post checks do not promise protection against arbitrary concurrent namespace
replacement by a hostile owner, nor keep a pathname pinned after return.

## Framing, bytes and fixed bounds

Initialize a disjoint caller-owned `struct gin_input` with
`GIN_INPUT_INITIALIZER`, once per exclusively consumed nonblocking read fd.
Pass it and a caller-owned `struct gin_record` to `gin_input_next()`.
Do not edit/copy live parser state or switch it to another fd. Starting a new
stream deliberately requires fresh state. The API never opens/closes the fd.

- LF (0x0A) terminates a record and is not returned. Empty LF is explicitly
  `GIN_INPUT_EMPTY`; a zero-byte kernel read is EOF, not an empty record.
- At most **4096 payload bytes**, excluding LF, are accepted. This is local
  input policy, **not a Grappa limit** and not a pipe atomic-write guarantee.
  State has a fixed 4096-byte line and 512-byte read buffer; output has 4096
  bytes. No heap allocation, growth, queue, truncation or implicit terminator.
- Output is length-aware, **not a C string**. `record.length` is usable for LINE;
  it is zero for every other result. Stale bytes outside that length are not
  output. No diagnostic contains rejected bytes.
- NUL, all other C0 bytes (including tab, CR and ESC) and DEL are invalid.
  CRLF is therefore rejected as one whole record; CR is not stripped.
  Bytes 0x80–0xFF are opaque and preserved: no UTF-8 validation, normalization,
  Unicode control detection or terminal-safe rendering is claimed. Later
  protocol/output boundaries must define their own text validation/escaping.
- On the first invalid byte or byte 4097, the whole record becomes invalid.
  Discard through LF, then emit exactly one REJECTED and resynchronize. Never
  emit a prefix or reinterpret an invalid tail as another record. Discarding
  across any number of calls uses constant space. No rejection is emitted
  early that might be mistaken for completion before the LF boundary.

Each call scans at most 512 input bytes, makes at most one `read`, returns at
most one record/event and copies at most 4096 payload bytes. Unconsumed bytes
from a coalesced kernel read stay in the state for later calls; no complete line
is dropped. There is no internal read/retry/EOF loop.

| Result | Meaning / caller responsibility |
| --- | --- |
| LINE | One nonempty complete record; consume explicit length |
| EMPTY | One empty LF record; decide application policy later |
| REJECTED | One whole invalid record ended at LF, or partial/discarded input ended at observed EOF |
| MORE | Chunk consumed without a record boundary; schedule another bounded call |
| AGAIN | read returned EAGAIN/EWOULDBLOCK; partial/discard state preserved; wait for readiness |
| INTERRUPTED | read returned EINTR; state preserved; caller decides retry/scheduling |
| EOF | read returned zero with no pending partial record; do not busy-poll |
| ERROR | Other read failure; errno retained; caller owns recovery/close |

Each call forwards the actual nonblocking read outcome: a freshly opened FIFO
that has never had a writer can report AGAIN rather than EOF on some systems.
The EOF tests first connect and close a real writer; they do not assume initial
no-writer behavior is identical across kernels.

Only an **observed `read() == 0`** clears partial/discard state. A partial record
at that boundary yields one REJECTED; subsequent zero reads yield EOF, not
repeated rejections. Complete buffered records are delivered before another
read. The [runtime](RUNTIME.md) owns HUP, fairness, polling and retry scheduling;
MORE does not permit an unbounded tight event-loop drain, and EOF must not become
a spinning HUP loop. The current API does none of that scheduling itself.

## Cooperating writer contract and limits

For multiple writers, each writer must query `fpathconf(fd, _PC_PIPE_BUF)` on
the actual pipe and submit each complete LF-terminated record in **one write**
whose total size, including LF, fits that bound. Also obey the separate local
4096-byte payload limit. For example a 4096-byte PIPE_BUF allows at most 4095
payload bytes for an atomic LF record, despite the parser accepting 4096.
Nonblocking writers must handle EAGAIN without splitting a record or assuming
it was accepted. A successful atomic write is not application acceptance or
message delivery. The reader cannot verify which write calls produced bytes.

A FIFO has no writer/session IDs. If one writer closes and another opens before
the reader observes EOF, that handoff can be invisible: incomplete fragments
may concatenate. Arbitrary fragmented or oversized multiwrite records may
interleave; they are unsupported and indeterminate, not reconstructable by
this parser. No cross-writer-session isolation, invented PID/session framing,
exactly-once or anti-malicious-writer guarantee is provided. Partial-read tests
use one controlled writer; concurrent tests use only atomic complete records.

## Tests

`tests/fifo_consumer.c` holds the real root lock and opens the real FIFO, with
stdin/stdout handshakes, a watchdog and explicit fd cleanup. `test_input.py`
controls partial/coalesced reads, EAGAIN, EOF/new writers, invisible handoff,
empty/invalid/boundary records, discard/resync, atomic concurrent writers and
unsafe objects/ancestors. All are in `make check`, hence compiler, sanitizer,
BSD make and platform paths. Privileged foreign-owner coverage is added to the
existing isolated CI root-test directory; ordinary local runs skip it. EINTR
is returned explicitly but not deterministically induced on a nonblocking FIFO
in these integration tests. Actual macOS/FreeBSD CI is still required; local
Linux execution is not evidence those platforms passed.
