# Local instance locking and recovery

## Implemented boundary

`gin init` is a short-lived writer, not a running messaging process. After all
CLI/configuration and effective lexical validation, it creates or safely opens
the root, acquires its writer lock, then creates/reuses network and window
directories, FIFO and log. It closes all child descriptors before releasing the
lock and root descriptor. Successful initialization does not leave a writer
running. `gin run` retains the same lock for the entire foreground local-only
FIFO/event-loop lifecycle; see [RUNTIME.md](RUNTIME.md). There is no daemon,
networking or message delivery. The bounded C input API is described in
[INPUT.md](INPUT.md).

`gin check` remains lexical and read-only: it does not open/probe the output
root, create or acquire a lock, or inspect whether another writer exists.
Configuration file reading is separate and still precedes initialization.
A lock conflict is an operational failure (CLI exit 1), not invalid settings.
Acquisition is non-waiting; gin does not retry until another writer exits.

## Persistent inode, kernel-held ownership

The reserved `ROOT/.gin.lock` is a regular, effective-user-owned, single-link
file with exactly `0600` permissions, including no special mode bits. It is
created exclusively when absent, otherwise checked before opening. Opens use
`O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC`, with no truncation or append.
The opened descriptor and descriptor-relative path must identify the same safe
inode before and after locking. A symlink, hard link, directory, FIFO, foreign
owner or unsafe mode fails closed without repair; FIFO rejection cannot wait
for a peer. Root traversal retains component-by-component directory descriptors
and no-follow checks. Root creation itself necessarily precedes root locking;
no network/window/FIFO/log mutation precedes acquisition.

The lock is a POSIX `fcntl(F_SETLK)` exclusive (`F_WRLCK`) whole-file record
lock: start zero, `SEEK_SET`, length zero (including future bytes). One cooperating
process can hold the writer lock per root inode, regardless of selected target,
network, absolute/relative spelling, repeated slashes or dot components. Separate
roots have separate locks. This is advisory cooperation, not a permission system:
other programs and older gin versions that ignore the protocol can still write.

The lock file is **never unlinked, renamed, truncated or replaced for recovery**.
Its contents have no meaning to gin and are never read or written. An existing
file, PID-looking content, old timestamp or nonempty file is not evidence of a
live writer or a reason to remove the file. Only the kernel lock decides.
Never delete it while any cooperating process might hold or acquire it: doing
so could let two processes lock different inodes under the same name.

The kernel releases process locks on close or process death, including SIGKILL.
A later initializer reuses the same validated inode without stale timeouts,
PID probing, killing another process, or deleting user data. Safe partial layouts
are completed by the existing repeatable initializer. Existing valid FIFO/log
inodes and log bytes survive. Unsafe objects still fail; no broad cleanup,
transactional rollback, power-loss durability or fsync protocol is provided.

## Reusable held-root API

`include/gin_fs.h` declares `struct gin_root` with `rootfd` and `lockfd` and the
`GIN_ROOT_INITIALIZER` value. All inputs/pointers must be non-NULL, strings
NUL-terminated; return values are zero or -1 with errno. Use:

1. `gin_root_acquire(path, &root)` on an initialized, empty handle. It validates
   the root lexically, creates/reuses and validates it, and retains both root
   and locked file descriptors. A failure closes its temporary descriptors and
   leaves the handle empty. Reacquiring into a live handle returns `EINVAL`
   without opening or closing another lock descriptor.
2. `gin_root_init(&root, network, target, chantypes)` to initialize one window
   using the held root descriptor, without reopening/relocking the root. Names
   are validated before child mutation. It closes its child descriptors on
   return, but retains the root and lock on both success and failure. It can
   be repeated for multiple targets. There is no retained FIFO reader or log
   writer. `gin_root_input_open()` separately opens an existing input and
   transfers fd ownership to its caller; close it before root release. See
   [INPUT.md](INPUT.md). A future runtime still owns scheduling and shutdown.
3. `gin_root_release(&root)` only after all caller-owned child descriptors are
   closed. It closes lock and root, consumes/resets the handle, reports close
   errors, and permits another release of an empty handle. Interrupted closes
   are not retried because descriptor state is platform-dependent.

`gin_init()` validates the complete effective settings before acquiring and
wraps this lifecycle for today's single initializer. The API uses bounded
existing root/naming policies, no new runtime dependency, and no thread safety
promise (creation temporarily changes the process umask). Handles must not be
copied, manually edited, or used after release.

### POSIX per-process caveat

These are **process-associated record locks**, not Linux open-file-description
locks or BSD `flock`. Same-process acquisitions do not exclude each other.
Closing **any descriptor for the lock inode in the owning process** releases
that process's locks on it, even a duplicated or independently opened descriptor
that never requested a lock. Therefore the caller must keep exactly one managed
handle per root in the process and must not independently open/close that lock
inode, acquire it through a second handle, call `gin_init()` again for the held
root, or read it as a config file during ownership. The live-handle guard is not
a process-wide registry and cannot make arbitrary duplicate descriptors safe.

Locks are not inherited across `fork`; do not use an inherited handle to mutate
the tree. Both descriptors are close-on-exec. A future runtime must acquire and
retain ownership in the actual writer process and audit every open/close path
before integrating adapters, reloads or subprocesses. See the
[POSIX fcntl specification](https://pubs.opengroup.org/onlinepubs/9699919799/functions/fcntl.html)
for process-associated advisory record-lock semantics.

## Scope and trust limits

Use a local filesystem implementing POSIX record locking correctly. There is
**no distributed, NFS, remote-mount or cross-host exclusion guarantee**. Gin
cannot prove a filesystem implements the required semantics and does not guess
mount types or fall back to PID files. Unsupported/failed locking is an error.

Keep the root's parent/ancestors trusted. Descriptor-relative traversal and
inode checks do not defend against root, malicious same-UID processes, hostile
filesystem semantics, or an untrusted parent renaming/replacing the root while
writers resolve it. They do not make arbitrary concurrent namespace replacement
safe. Gin never repairs permissions or changes ownership to recover. Do not run
it as root to bypass ownership failures.

## Test evidence

`tests/lock_holder.c` is a test-only C child controlled by stdin/stdout pipes;
assertions remain active under `-DNDEBUG`. `tests/test_locking.py` waits for its
explicit acquisition-ready message, tests CLI contention/check/independent
roots, commands ordinary release or sends SIGKILL, reaps each child, and retries
initialization only after confirmed exit. Watchdog timeouts bound failures;
there are no sleeps to guess lock timing. Tests also use a separately held
POSIX lock to check the CLI independently of the held-root API. Persistent
contents/inodes, partial states, hostile lock objects and invalid settings are
covered. Foreign-owner tests are skipped without privilege. All tests run in
`make check`, including compiler, sanitizer and platform paths; local Linux
results do not substitute for real macOS/FreeBSD CI.
