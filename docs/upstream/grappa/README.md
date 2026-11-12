# Grappa protocol documentation snapshots

This directory preserves upstream documentation for offline reference. It is
**not a Grappa fork, runtime dependency or claim of client compatibility**.
Gin follows upstream through deliberate reviewed updates, not a rolling sync.

## Active documentation baseline

`current.json` selects revision `87daa4cca9b41fbd6c5f93cb40d6848193f425f9`.

- [Unmodified client protocol](87daa4cca9b41fbd6c5f93cb40d6848193f425f9/CLIENT_PROTOCOL.md)
- [Upstream MIT license](87daa4cca9b41fbd6c5f93cb40d6848193f425f9/LICENSE)
- [Provenance and integrity manifest](87daa4cca9b41fbd6c5f93cb40d6848193f425f9/snapshot.json)
- Original repository: https://github.com/vjt/grappa-irc
- Original document: https://github.com/vjt/grappa-irc/blob/87daa4cca9b41fbd6c5f93cb40d6848193f425f9/docs/CLIENT_PROTOCOL.md

The snapshot records upstream protocol **63**, server minimum **1**. These are
facts captured from `lib/grappa/protocol.ex` at that revision, **not versions
gin implements or has qualified against a running server**. Runtime source is
not copied. Cross-references from the preserved document to upstream source
remain online references; this is not a complete offline Grappa source tree.

Each revision directory contains the original `CLIENT_PROTOCOL.md` bytes, the
original `LICENSE` bytes, a gin-owned `snapshot.json`, and `SHA256SUMS` for manual
integrity checks. The manifest records
source paths, repository and full commit, commit/capture UTC timestamps, protocol
metadata, SHA-256 and Git blob SHA-1 identities. The initial capture checked the
bytes against GitHub's blob identities at the pinned commit. Git blob SHA-1 is
recorded for upstream identity; SHA-256 is the local integrity check. Neither is
a signature or independent proof that upstream is trustworthy.

The upstream document is intentionally unedited. Its wording, example values,
formatting and relative references belong to upstream; gin's interpretation,
feature floors and implementation status live in [PROTOCOL.md](../../PROTOCOL.md)
and [COMPATIBILITY.md](../../COMPATIBILITY.md).

## Offline verification

On Linux, from the repository root:

```sh
(cd docs/upstream/grappa/87daa4cca9b41fbd6c5f93cb40d6848193f425f9 && sha256sum -c SHA256SUMS)
```

These checks run offline and do not execute gin or require an application test
suite. They check the two copied files, not the truth of the manifest metadata
or runtime compatibility. Review provenance, the active pointer, protocol values
and documentation consistency in the PR. Re-derive protocol values from the
upstream source when deliberately updating the baseline. No dedicated checker,
test suite or additional CI workflow is introduced for this documentation change.

## Updating the baseline

1. Choose an explicit upstream commit. Read its client contract, protocol
   module and relevant source changes; do not silently substitute upstream main.
2. Create a feature branch. Add a **new full-SHA directory** containing the
   document and license retrieved from that exact commit. Confirm original bytes
   against upstream Git blob identities and compute SHA-256 locally.
3. Record provenance and the source-reviewed protocol values in `snapshot.json`.
   Keep existing snapshot directories unchanged, even after changing the active
   baseline. Do not edit the upstream document to fix gin-specific issues.
4. Review the semantic diff against the previous snapshot: authentication,
   routes, frame shapes, topics, event ordering, removal/deprecation, encryption
   boundaries and new mandatory feature floors. Record the result in the PR.
5. Update `current.json`, gin's [protocol guide](../../PROTOCOL.md),
   [architecture](../../ARCHITECTURE.md), [compatibility record](../../COMPATIBILITY.md)
   and relevant fixture/feature gates together. The active reference must agree
   across these files; changing a checksum alone is not an upstream update.
6. Verify checksums, provenance, license and documentation links. For a
   documentation-only update, application tests are not needed locally. If
   implementation behavior changes too, run its applicable tests. Pass the
   existing required CI and merge through a PR. There is no
   scheduled or automatic import and no deployment triggered by an upstream push.

A release tag freezes the docs and manifest included in that gin revision.
For each release, record supported protocol declarations/feature floors and
actual tested server revisions using the [release policy](../../COMPATIBILITY.md).
Retain older snapshots when a later development baseline moves forward.
