# Protocol compatibility and release records

The [verified-WSS native connection API](CONNECT.md) provides root-first
subscriptions through libcurl >=8.23.0. The [production session owner](SESSION.md)
composes verified startup and live selected-window plaintext reception in the
CLI, including FIFO send/control, native liveness and bounded history/restart
recovery. An owner-authorized point-19 candidate has narrow live plaintext
evidence against a deployment reporting Grappa 1.6.4/protocol 63; this is not
a completed release qualification. See [opt-in smoke and evidence](LIVE_TESTS.md).

## Current status

Gin implements verified connected startup and selected-window plaintext
reception, not a released full chat client.

- Active documentation baseline: Grappa `87daa4cca9b41fbd6c5f93cb40d6848193f425f9`.
- Captured upstream protocol: 63; captured upstream server minimum: 1. These
  describe the pinned upstream revision, not the gin declaration.
- Connected gin and credential-import declaration: `C=36`; required server
  floor: `R=36` for the [receive subset](PROTOCOL.md), including `dm_with`.
  This is not a promise of every feature through 36; native APIs retain explicit
  caller policy.
- Release-qualified runtime Grappa commits: **none**. Candidate live observations
  cover a deployment reporting 1.6.4/protocol 63/minimum 1. The owner confirms
  upstream release `v1.6.4`, resolved to commit
  `874d804dde0a95e7f1be3ddc7c954989c35311da`; deployment linkage is owner-attested.
  Required CI/native FreeBSD for this candidate is pending.
- Isolated REST/TLS and account authentication APIs: local HTTP/HTTPS fixtures
  only, including bounded JSON, private credentials and [HTTP discovery with
  gated authentication](DISCOVERY.md) and [owned topology snapshots](TOPOLOGY.md);
  REST query inventory remains unknown without a user-topic snapshot, and no
  general live-version guarantee is claimed; composed CLI evidence is recorded
  separately in [LIVE_TESTS.md](LIVE_TESTS.md). Isolated WebSocket acquisition is in
  [CONNECT.md](CONNECT.md). See [authentication scope](AUTH.md).
- The [connected startup owner](SESSION.md) composes verified HTTPS discovery,
  stored-origin and account-identity checks, `/boot`, and root-first WSS
  subscriptions and selected-window plaintext projection. The 18.4 development
  candidate also composes FIFO sending/control, liveness and bounded recovery;
  candidate integration alone is not supported-platform or release qualification.
  The separately recorded live evidence is limited to its tested plaintext scope.
- Isolated [Phoenix v2 TEXT codec](PHOENIX.md): source-derived golden frames,
  explicit-message fragment collection and reply/reference fixtures only;
  stricter local reference/object-payload policies are documented there.
  The codec itself performs no RFC6455 handshake, subscriptions or liveness;
  the separate native connection API owns those transport operations.
- Published beta/stable releases: **none**.

The implemented connected floor `R=36` in [PROTOCOL.md](PROTOCOL.md) is a policy
choice, not a live compatibility guarantee. Preserved documentation and passing
local fixtures do not establish live Grappa interoperability or chat delivery.

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
