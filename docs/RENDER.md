# Incoming event rendering (isolated native API)

`include/gin_render.h` defines the pure renderer and outside-callback append API.
Connected session startup does not yet project chat; [18.2](../ROADMAP.md)
will integrate reception. These APIs do not send, acknowledge, decrypt,
reconnect, fetch history or establish live Grappa compatibility.

## Trusted owner and routing

The owner supplies a known destination, authenticated subject and exact network
slug, current joined source reference and socket epoch. The renderer reconstructs
the actual source topic using the shared connection topic builder and validates
it and the observed callback epoch. A null broadcast reference is accepted only
with this explicit joined context; a supplied reference must match.

Source and destination targets are different facts. Inbound DM source channels
may be the operator's old nick. Query messages require explicit `dm_with` peer
agreement; sender is never a routing authority. Channel destinations must also
match the source channel. `$server` is an explicit status role, not an inferred
query. Root lifecycle notifications require matching network and destination.
No payload chooses a filesystem path. ASCII letters alone are identity-folded.

## Stable records

Supported Phoenix `event` payloads are `message` with nested `privmsg`, `notice`
or `action`; and `joined`, `window_pending`, `window_invited`,
`window_invite_declined`, `join_failed`, `kicked`. Unknown kinds are ignored,
without JSON dumps. Recognized malformed consumed fields fail without output;
unused IDs, metadata and additive fields are not mandatory renderer schema.

Examples (each ends in exactly one LF):

```text
[1970-01-01T00:00:00.000Z] privmsg sender="Nick" body="hello"
[1969-12-31T23:59:59.999Z recv] joined target="#Room" state="joined"
```

Message time is integer epoch milliseconds from `server_time`; lifecycle time
is explicit owner-supplied receive milliseconds, marked `recv`. Gregorian UTC
presentation accepts years 0001 through 9999, including negative milliseconds;
there are no clock, timezone or locale reads. Null message bodies render empty;
missing bodies fail. Only `action` strips the exact SOH + `ACTION ` opener and
one optional trailing SOH, following pinned upstream `CTCP.action_text/1`.

Quoted fields preserve strict useful UTF-8. Quote/backslash are escaped;
C0/DEL become uppercase `\xHH`, C1 becomes `\u00HH`, and selected bidi controls
and Unicode separators become `\uXXXX`. This is not universal visual-spoofing
protection. Invalid UTF-8 fails; the Phoenix codec separately rejects JSON NUL.
The public header fixes input and retained-record bounds, with no truncation.
Records are owned and survive frame/context destruction. Fresh NULL output is
required; failure never publishes a partial record or replaces occupied output.

Messages require explicit owner `PLAIN` policy, including channel messages.
Unknown/encrypted modes, reserved `e2e_*` kinds and CTCP E2E prefixes (including
malformed frames) refuse without ciphertext logging or plaintext fallback.
This caller policy is not backend attestation or complete encryption detection.

## Appending safely

Acquire one root, initialize known destinations, and obtain an existing output
with `gin_root_output_open`. Keep that same held root alive until child fds close.
Never open or close another descriptor for its lock inode: POSIX record locks
are process-associated. See [locking](LOCKING.md).

Render inside the connection callback, retain only bounded owned records, and
append **after** `gin_connect_service` returns. The fixture owner demonstrates
an eight-record/256-KiB queue, visible overflow and deferred error counts; it is
not a production runtime queue. No callback filesystem I/O or session reentry.

Append rechecks regular type, effective owner, exact 0600, single link,
O_APPEND and write access. At most 64 write attempts include interrupted and
short writes. Positive progress advances once; zero writes fail with EIO.
On error the reported byte count can be positive. Stop that sink: replaying the
whole record duplicates the prefix. There is no rollback, record atomicity,
fsync, power-loss or syscall realtime guarantee. Fixed error labels contain no
wire JSON, credential, authentication header or raw event content.

## Evidence and limits

`make check` includes exact native core golden records, append syscall fault
injection (private translation-unit substitution, no production hooks), helper
units, held-root filesystem tests and a real libcurl WSS loopback fixture.
The Python fixture asserts exact output-file bytes after borrowed frames have
been freed, malformed/encrypted error counts and queue overflow. It uses only a
public synthetic credential and the existing TLS trust machinery. Valgrind,
NDEBUG, sanitizer and platform lanes retain the existing gates. Root ownership
checks require the isolated privileged CI lane; unprivileged skips are explicit.
Jansson allocation errors are not perfectly distinguishable from parse errors;
explicit owned-record allocation failure is classified as NOMEM.

Behavior is derived from pinned upstream
`87daa4cca9b41fbd6c5f93cb40d6848193f425f9`: `scrollback/wire.ex`
61–85, 155–192; `session/wire.ex` 488–521, 655–671;
`session/persistor.ex` 110–129; and `irc/ctcp.ex`.
These are source-derived local fixtures, not live-server qualification.
