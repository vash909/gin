# Safe filesystem names

## Identity and API

`include/gin_naming.h` and `src/naming.c` define the shared lexical codec.
`gin_name_prepare()` owns separate copies of `input_network_slug`,
`original_wire_target`, ASCII-folded `canonical_target`, `network_component`,
`window_component` and `kind`. Inputs are never folded in place. Treat the
prepared value as immutable. `GIN_WINDOW_NONCHANNEL` includes queries and
synthetic windows: it does not prove that a target is a DM. No numeric network
ID is inferred from a slug or filename; IDs await actual server discovery.
No database, metadata sidecar or placeholder discovery is introduced.

Grappa canonical target identity folds ASCII A–Z unconditionally for channels
**and** nick/window targets. This follows the reviewed
`Grappa.PubSub.Topic.channel/3` reference described in [PROTOCOL.md](PROTOCOL.md)
([pinned source](https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/lib/grappa/pubsub/topic.ex),
canonical-target explanation at lines 81–93). It is independent of an IRC
network's CASEMAPPING. Do not substitute RFC1459 folding: brackets, braces,
backslash, pipe, caret and tilde remain distinct. Do not Unicode-case-fold.
Resource lookup and future discovery must still use explicit upstream metadata.

The encode/decode APIs take NUL-terminated, non-NULL inputs and explicit output
capacities **including the trailing NUL**; input/output buffers must not overlap.
Kind/result pointers must be non-NULL. Return is 0 on success or -1 with errno
(`EINVAL` for invalid syntax/identity, `ENAMETOOLONG` for expanded/buffer limits).
Outputs are usable only on success. Decode requires the same configured
CHANTYPES used for encoding. It strictly re-encodes and compares the exact
component: malformed/lowercase escapes, unnecessary escapes, raw uppercase,
namespace aliases and invalid identities are rejected. It never guesses a
legacy layout. Network decoding returns the **original slug**; window decoding
returns **canonical Grappa identity**, not original wire letter casing. Retain
`original_wire_target` separately when wire spelling is needed. The filename
cannot recover casing that was folded, or invalid arbitrary bytes previously
accepted by the scaffold.

## Channel classification and window namespaces

`-T CHANTYPES` or file `chantypes` is a distinct subset of the ASCII characters
`#&+!` (0–4 bytes), default `#`. Empty explicitly disables channels. Order is
irrelevant. Duplicate, foreign, whitespace/control or non-ASCII prefixes are
invalid. CLI overrides the file; duplicate flags/keys are invalid even if the
first value is empty. File values receive the existing surrounding space/tab
trim; CLI values do not. Syntax/raw limits are checked even in overridden file
values; semantic naming validation applies to effective settings.

A target is a channel only when its first byte is configured. Strip exactly
that prefix for the body. Reject an empty body or a body exactly `.` or `..`.
Nonchannels retain the entire target as their body and use a reserved `@`.
Encode the canonical body with readable `a-z`, `0-9`, `_`, `-`, `.`; every other
byte becomes `%HH` with uppercase hexadecimal, including literal `%`, `+`, `@`
and all non-ASCII bytes. A `#` channel has no namespace marker. Other configured
prefixes use the reserved **literal** `+` plus their uppercase ASCII hex code:

| Wire target | CHANTYPES | Component | Decoded canonical identity |
| --- | --- | --- | --- |
| `#Grappa` | `#&+!` | `grappa` | `#grappa` |
| `Grappa` | `#&+!` | `@grappa` | `grappa` |
| `&Foo` | `#&+!` | `+26foo` | `&foo` |
| `+Foo` | `#&+!` | `+2Bfoo` | `+foo` |
| `!Foo` | `#&+!` | `+21foo` | `!foo` |
| `#+26foo` | `#&+!` | `%2B26foo` | `#+26foo` |
| `&Foo` | `#` | `@%26foo` | `&foo` |
| `#Foo` | empty | `@%23foo` | `#foo` |
| `#@foo` | `#` | `%40foo` | `#@foo` |
| `$server` | `#` | `@%24server` | `$server` |

Changing CHANTYPES changes classification and potentially paths. Keep it stable
for an existing root; review paths explicitly before any manual migration.

## Unicode and hostile input

Targets must be strict UTF-8 scalar sequences: reject invalid continuation,
overlong, truncated, surrogate or above-U+10FFFF encodings. Reject C0 and C1
controls, DEL, ASCII whitespace, slash/traversal and exact dot/dotdot bodies.
A backslash is encoded data, not a POSIX path separator. Unicode whitespace
outside C0/C1 is retained; no Unicode normalization or locale rules apply.
Valid Unicode bytes are preserved then percent-encoded, so NFC/NFD and non-ASCII
case stay distinct even on normalization/case-insensitive filesystems:
`#é` -> `%C3%A9`, `#é` -> `e%CC%81`, `#É` -> `%C3%89`.
Filename spelling uses ASCII only, with one canonical escape form. ASCII-case
aliases intentionally share a canonical target; distinct canonical identities
remain distinct under filesystem ASCII case folding. This is a lexical codec,
not a guarantee against hostile filesystem semantics.

## Network components and bounds

Slugs retain the previous input restriction: 1–255 ASCII letters, digits, `_`
or `-`. Preserve exact case. Only lowercase `a-z`, digits, `_`, `-` remain
literal; uppercase ASCII is escaped (`Net` -> `%4Eet`, `net` -> `net`,
`NET` -> `%4E%45%54`). Never use the raw slug as a filesystem component.
The encoded component must fit 255 bytes: 85 uppercase bytes fit, 86 do not.
No truncation, hash fallback or expansion of accepted network syntax.

Raw targets remain at most 256 bytes; **each** encoded network/window component
at most 255 bytes excluding NUL. Namespace markers count toward this limit.
UTF-8 limits count bytes, not characters. Encoding may reject a short raw name
whose escapes expand too far. A filesystem's smaller limits may still produce
an operational error. Root lexical rules are unchanged: 1–4095 bytes, no ASCII
controls/DEL or `..` path components; dot components/repeated slashes accepted.
Root is an explicit path, not encoded or Unicode-normalized by this codec.
Config comments/overridden values retain their existing raw-byte grammar; strict
UTF-8 applies to the effective target, not every byte of the config or root.

## Pre-beta layout change and security

**Uppercase network slugs now have different paths. Use a new root or perform
an explicit, reviewed manual migration before reuse.** Common lowercase
network/default `#channel` and query layouts stay compatible. Previously
accepted invalid UTF-8 and C1 controls now fail; newly configured prefixes
change only their classified targets. Old arbitrary-byte names do not provide
a promise of recoverable original casing or valid Unicode.

Gin never automatically migrates, renames, deletes or merges old logs. It has
no legacy-path fallback and cannot detect every legacy/case-insensitive alias.
Back up logs, stop all writers, review original slug/target identity and select
a fresh root or manually copy the correct history under the new canonical
layout with safe ownership/modes; do not blindly rename escaped-looking names.

`check` and `init` use this same codec and CHANTYPES. All lexical validation
finishes before `init` mutates the output tree. `check` is read-only and never
opens/probes it. Encoded names do not weaken descriptor-relative O_NOFOLLOW,
owner/mode/type checks, single-link endpoints or append-only opening. Reusing
safe `in`/`out` preserves inodes and log bytes. Unsafe objects are rejected,
never repaired. See [ARCHITECTURE.md](ARCHITECTURE.md) and [CLI.md](CLI.md) for
the local-user security boundary and operational partial-initialization limits.
