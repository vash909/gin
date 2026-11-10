# Protocol compatibility and release records

## Current status

Gin is a development scaffold, not a released or operational Grappa client.

- Active documentation baseline: Grappa `87daa4cca9b41fbd6c5f93cb40d6848193f425f9`.
- Captured upstream protocol: 63; captured upstream server minimum: 1.
- Implemented gin client declaration: **none**.
- Qualified runtime Grappa revisions: **none**.
- Isolated REST/TLS and account authentication APIs: local HTTP/HTTPS fixtures
  only, including bounded JSON and private credentials; no protocol negotiation
  or live Grappa qualification. See [authentication scope](AUTH.md).
- Published beta/stable releases: **none**.

The proposed server feature floor in [PROTOCOL.md](PROTOCOL.md) is a design
choice, not a compatibility guarantee. A preserved document or passing local
filesystem tests do not establish that gin can authenticate or exchange chat.

The full client document and upstream license are kept in the
[versioned snapshots](upstream/grappa/README.md), with provenance and checksums.
The active pointer describes the reference being developed against; it does not
claim support for every field or version in that document.

## Policy

- Pin documentation by full upstream commit, not a branch name or moving tag.
- Keep copied upstream files byte-for-byte unchanged and retain their license.
- Follow upstream deliberately through PRs that review wire changes, update
  fixtures and feature floors, and pass the required CI gate.
- Add a new snapshot rather than overwriting a retained one. Git history and
  release tags preserve earlier states; retained directories also allow offline
  comparison within a current checkout.
- Prefer compatible feature/version comparisons, not exact version equality.
  Test the declared minimum and newer revisions before making support claims.
- Do not raise a feature floor merely because a documentation baseline advanced,
  or claim old-server support solely because a socket accepted a declaration.
- Do not publish a beta until roadmap acceptance criteria have actual evidence.
  No fake release tag or runtime qualification is introduced by this snapshot.

## Required record for each future release

Add a release-specific entry in this file in the release PR, before tagging:

- Gin release tag and code revision being released.
- Active documentation snapshot revision and manifest path.
- Gin's implemented client declaration and minimum required server protocol.
- Feature-specific floors, implemented subset and intentional exclusions.
- Exact tested Grappa commit(s), corresponding protocol/minimum values, and
  relevant configuration, with secrets removed.
- Test commands, fixture revisions, platform results and CI run identifiers.
- Opt-in live REST/Phoenix interoperability evidence: authentication, user-topic
  joins, incoming/outgoing messages, reconnection/history and unsupported E2EE
  handling. Mark untested cases explicitly; mocks are not live-server evidence.
- Known limitations, upgrade implications and any compatibility regression.

Release source archives must include the snapshot, original upstream license,
manifest, current pointer and this compatibility record. The existing source
archive workflow uses tracked Git content; it does not fetch upstream during a
build. Checksums identify bytes, not a cryptographic endorsement of upstream.

## Release entries

No releases yet. Do not replace this statement with an inferred support matrix;
add entries only when a release has real qualification evidence.
