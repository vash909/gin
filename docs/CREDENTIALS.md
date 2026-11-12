# Private credential store

`include/gin_credential.h` and `src/credential.c` implement local storage for
one account-user credential per held gin root. This module does not log in,
validate a session remotely, revoke a server token, or integrate networking.
The separate [production startup owner](SESSION.md) selects this fixed store
through ROOT and explicitly composes enrollment and bound session validation.
Authentication must additionally validate the HTTPS origin with libcurl's URL
parser before transmitting credentials.

## Format and bounds

`ROOT/.gin.credentials` contains one UTF-8 JSON object with exactly these keys:

| Key | Required value |
| --- | --- |
| `version` | JSON integer `1`, not a boolean or real number |
| `origin` | Exact HTTPS authority, at most 1024 bytes |
| `token` | Nonempty RFC 6750 bearer syntax, at most 4096 bytes |
| `subject_id` | Syntactic 36-byte UUID, preserving letter case |
| `subject_name` | Nonempty valid UTF-8, at most 255 bytes, no C0/C1 controls or DEL |

The file is at most 16384 bytes. Unknown/missing keys, duplicate keys, trailing
JSON/data, raw or escaped NUL, invalid UTF-8, and wrong field types are rejected.
Before Jansson decoding, a bounded string-aware scan rejects arrays and nested
objects: recursive decoder depth cannot be driven by deeply nested input.
Jansson >=2.14 is required, including its strict duplicate/EOF/string handling.
The format does not accept network-user or other subject records.

Origins are not trimmed, normalized or repaired. The lexical check accepts
only lowercase `https://`, a nonempty ASCII host (optionally bracketed IPv6)
and an optional decimal port, with no path, query, userinfo, fragment,
backslash, whitespace or controls. This is not DNS resolution or full URL/IP
validation. Load requires an exact origin match, including spelling and port.
Tokens use ASCII letters/digits and `-._~+/`, optionally followed by `=` padding
only at the end; padding alone is invalid. UUID validation checks syntax, not
UUID version or account existence. UTF-8 limits count bytes, not characters;
no normalization is performed.

## Descriptor ownership and safe replacement

All load/save/forget operations require the caller's existing live `gin_root`
with its root writer lock held throughout. They never reacquire, reopen or
close the caller's root/lock descriptors. Handles must not be copied, edited,
used across fork, or used after release. See [LOCKING.md](LOCKING.md), especially
POSIX process-associated locks: closing any alias of the lock inode releases
ownership. The store inspects targets before opening them and refuses aliases
of the held lock inode without opening/closing that inode.

The root must remain effective-user-owned and exactly `0700`. An existing
credential target must be a regular, effective-user-owned, single-link file,
exactly `0600` including special bits. Symlinks, hardlinks, FIFO, directory,
foreign ownership and unsafe modes fail closed without repair, unlinking or
truncation. Opens are descriptor-relative, no-follow, nonblocking and
close-on-exec; opened files and path identities are checked again.

Save validates the value and old target before making a bounded-attempt,
exclusive `0600` temporary file in the same directory. It performs bounded
writes, fsyncs the complete temporary file, rechecks the old target's identity,
then atomically renames and fsyncs the directory. Readers never receive a
partially published record. Pre-publication failures preserve the old record;
cleanup removes only this save's created temporary file, never an unknown stale
temporary name. Existing stale temporary files are not recovered or deleted.
A directory fsync failure after rename returns an I/O error, **even though the
new record has already been published**. No rollback or crash-durability claim
is made for that failure. Forget also reports directory fsync failure after
unlink; the removed local record is not restored.

The existing lock serializes cooperating processes. Use trusted ancestors and
a local filesystem with working POSIX locks. Malicious same-UID processes,
root, concurrent owner namespace changes, hostile filesystem semantics and
distributed/NFS locking are outside this boundary. There is no thread-safety
promise, process-wide handle registry, or mechanism for proving that a caller
has not independently released its own POSIX lock.

## Secret handling and limits

This is private **plaintext**, not encryption. Tokens are not incorporated into
paths, error text or logs. Load clears the entire output on every error; callers
must only use it after success. `gin_credential_clear()` uses volatile writes
for best-effort clearing of the supplied structure, and the module similarly
clears its own byte buffers before release. This does not promise erasure of
Jansson/internal allocator copies, swap, core dumps, filesystem snapshots,
backups or disk blocks. Forget removes local storage only: it is neither secure
erasure nor remote revocation. A caller needing revocation must invoke the
separate server API and handle its outcome explicitly.

## Tests

`tests/test_credential.c` exercises lexical validation, bounds, clearing and
error handling. `tests/credential_driver.c` acquires a real held `gin_root`;
`tests/test_credentials.py DRIVER` controls it through bounded pipes and private
synthetic fixture files. No token is passed in argv or printed on failure.
Fixtures canonicalize TMPDIR so a symlink in the scratch path is not mistaken
for an application root traversal attack. Tests include atomic old-record
preservation under a real file-size write limit, exclusive temporary-name
exhaustion, strict malformed/deep JSON rejection, unsafe objects, exact origin
binding, and an independent child confirming lock retention after alias refusal.
Foreign-owner checks skip without isolated privilege. Platform and integrated
build qualification remain the main build/test gate's responsibility.
