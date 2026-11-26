# Roadmap to the first beta

Each numbered item is a small delivery milestone, not a claim of completion.
Ship changes through a focused PR with tests, documentation and passing required
CI. Track dependencies and evidence in the PR. The existing filesystem/CLI
scaffold is a starting point, not completion of the operational milestones.

## First-beta contract

The target is `0.1.0-beta.1`: a headless C99/POSIX plaintext Grappa frontend,
not a TUI, direct IRC client or a completed E2EE client. TLS certificate and
hostname verification remain mandatory. Encrypted queries, unknown encryption
modes and reserved encrypted content must fail closed: no plaintext fallback,
ciphertext rendered as chat, pairing, device enrollment or invented crypto.

The account-mode interface is `gin <server> <user>`, with a no-echo password
prompt on the controlling terminal. Password and session bearer remain in
memory for that invocation; normal startup must not require persistent token
enrollment. Stable private account/checkpoint identity metadata is not a secret
store. Keep legacy explicit credential commands and migration consequences
documented until an independently qualified replacement permits retirement;
never delete existing credentials or user data automatically.

Project the account's already-open networks, channels and queries into an
ii-inspired tree: `irc/<network>/{in,out}` and nested window `{in,out}` pairs,
using the documented safe naming rules. The main terminal is for necessary
connection/error diagnostics, not chat input/output or route selection. Use
network/window FIFOs for the declared command and message subset. A newly opened
query must become usable without reconnecting or rebuilding old destinations.
Do not attach networks automatically, silently replay outgoing operations or
present local echo/transport acceptance as recipient delivery.

## Merged foundation and remaining delivery units

PR #31 was merged unchanged into `main` at
`65f180a6e3ec9f09c8610309e8ea7a966f2bdc7a` from qualified head
`7c519cc1dc8b5d5ac7b2100299e16a5d17a1a4ba`. It supplies the account front door
and fixed-plan owner, stable registry/live-subscription/dispatch foundations,
incremental resources and output/checkpoints, a pure output-binding getter,
and focused new-query history with quiet/held HTTP regressions. Its required
PR CI passed, including FreeBSD and strict history instrumentation. Main-only
delivery evidence is a separate gate, not implied by the PR result.

These foundations are not the complete dynamic account owner/FIFO workflow.
The remaining units below build on the merged APIs rather than reimplementing
them. All new acceptance units are **open** until their own evidence lands;
merging #31 does not declare milestone 18.4, interoperability or the beta done.
Keep historical failed runs, unresolved causes and missing evidence explicit.

1. **Lock the companion contract.** Record the Grappa reference revision,
   supported feature floor, terminology and non-goals; review protocol drift
   before every beta tag. Acceptance: reproducible compatibility notes and links.
2. **Stabilize the CLI and configuration.** Define foreground invocation,
   config precedence, validation, exit statuses and a man page. Acceptance:
   tests for invalid, missing and conflicting settings; no secrets in argv.
3. **Finish safe filesystem naming.** Support configured IRC channel types,
   queries, Unicode and collision-free reversible names; separate filesystem
   paths from network slugs, IDs and wire targets. Acceptance: hostile-name,
   symlink, ownership and case-mapping regression tests.
4. **Add instance locking and recovery.** Enforce one writer per root using
   POSIX locking; preserve logs and recover stale local state without deleting
   user data. Acceptance: concurrent startup and interrupted-run tests.
5. **Implement bounded FIFO input.** Handle partial reads, multiple writers,
   empty lines, NUL bytes, EOF and lines larger than the limit. Acceptance:
   no busy loop, truncation, injection or unbounded allocation.
6. **Implement the foreground event loop.** Use POSIX poll, explicit descriptor
   ownership and orderly SIGINT/SIGTERM shutdown. Acceptance: idle CPU and
   resource cleanup tests, no Linux-only event API requirement.
7. **Add the REST/TLS adapter.** Use libcurl with certificate/hostname checking,
   bounded responses, deadlines and structured errors. Acceptance: local HTTP
   fixtures plus trusted/untrusted TLS tests; no insecure default or redirect
   credential leakage.
8. **Add authentication and explicit credential-lifetime policy.** Implement
   login, session handling/revocation and safe handling of legacy explicit
   enrollment. The beta account front door uses the hidden TTY prompt and
   invocation-local credentials described above. Acceptance: wrong or expired
   sessions fail clearly; password/bearer never enter argv, URLs, ordinary
   settings, stdout, logs or automatically persisted files.
9. **Negotiate the Grappa protocol.** Read `/api/config`; compare the server
   floor and the features gin needs, separately from the software release and
   Phoenix serializer versions. Acceptance: old/new/malformed fixture matrix.
10. **Discover networks and windows.** Fetch authenticated network and window
    state and map IDs, slugs, channels and DM peers explicitly. Acceptance:
    multi-network fixtures and stable local paths without direct IRC access.
11. **Implement Phoenix v2 framing.** Encode/decode five-element JSON arrays,
    references, join replies, heartbeat and limits. Acceptance: golden frames,
    fragmented transport input, invalid shapes and unknown-field tolerance.
12. **Connect and subscribe safely.** Authenticate using the bearer
    subprotocol; join user, network and channel topics with correlated replies.
    Acceptance: handshake failures, rejected joins and reconnect resubscription.
13. **Render incoming events to logs.** Project messages, notices, actions and
    lifecycle state to bounded, escaped, timestamped text without terminal
    escape/control injection. Acceptance: exact fixtures and write-failure tests.
14. **Send plain messages from FIFOs.** Translate lines into documented Grappa
    actions; correlate acknowledgements and expose failures. Acceptance: mock
    server observes the exact action; no local echo misreported as delivery.
15. **Add minimal window controls.** Define small explicit commands for joining,
    parting and opening queries; reject unsupported commands. Acceptance: input
    cannot become arbitrary server commands or shell execution.
16. **Handle reconnects and liveness.** Add heartbeat timeouts, bounded backoff,
    session expiry handling and reliable state transitions. Acceptance: network
    loss tests, bounded queues, no silent replay or duplicate-send promises.
17. **Implement history and gap recovery.** Use documented cursors and history
    windows, preserving ordering and de-duplicating only with server identity.
    Acceptance: restart/gap fixtures and clear limits on delivery guarantees.
18. **Integrate the operational client and enforce the E2EE boundary.** After
    the component milestones through 17 land, wire their native APIs into the
    installed production CLI/runtime, not another test-only driver. Deliver the
    four sequential sub-milestones below through focused PRs. Each requires
    tests of the actual executable against local verified HTTPS/WSS fixtures,
    documentation and passing required CI; standalone API tests are not enough.
    Keep C99/POSIX, one foreground owner, bounded storage and one held root lock.
    Do not start live-server qualification until all four are complete.

    - **18.1 — Operational startup, credentials and session ownership.** Define
      and implement the connected CLI/configuration contract and server/account
      selection using the existing authentication,
      protocol discovery, topology and connection APIs. Keep secrets out of
      argv, ordinary settings, URLs and diagnostics; use the hidden TTY password
      prompt and keep password/bearer invocation-local for account-mode startup.
      Do not make legacy persistent enrollment mandatory. Preserve read-only `check`
      and safe local initialization; document any invocation migration. One
      owner coordinates subscriptions, descriptor lifetimes, deadlines and
      orderly shutdown. Establish TLS, origin/UUID binding and explicit route/
      plaintext-policy checks before enabling ordinary chat operations.
      Acceptance: launch the real executable against the local server, observe
      correlated READY, reject invalid credentials/TLS/protocol/mode, and stop
      cleanly without leaking descriptors, credentials or the root lock.

    - **18.2 — Live reception and filesystem projection.** Connect discovered
      networks and known channel/query windows to existing safe naming, output
      and rendering APIs. Copy borrowed live events into a count/byte-bounded
      owner queue; append only outside callbacks through the existing held root.
      Preserve prior logs, identity routing and escaped timestamps/text. Carry
      the E2EE boundary through incoming events and history: unknown/encrypted
      modes and reserved encrypted content produce explicit unsupported
      diagnostics, never ciphertext as ordinary chat or plaintext fallback.
      Acceptance: the real executable receives channel and DM events into the
      correct `out` files, with exact escaped bytes, unchanged existing prefixes,
      negative encryption/routing fixtures and independently tested queue caps.

    - **18.3 — FIFO sending and minimal window controls.** Wire the existing
      bounded FIFO reader, closed command grammar, plain-send and typed control
      APIs into that same connected process. Support the declared literal send,
      JOIN/PART and query-opening subset only; retain stable owner routing,
      correlated outcomes and explicit pending/failed/uncertain states. Apply
      encryption/mode checks before outgoing work, with no plaintext fallback.
      No raw IRC, shell passthrough, automatic resend or local echo presented as
      recipient delivery. Acceptance: write to real `in` FIFOs while the same
      process continues receiving; the local server observes exact authorized
      operations and the CLI exposes rejection, uncertainty and unsupported
      encryption without leaking payloads into diagnostics or replaying input.

    - **18.4 — Complete the dynamic account runtime on the merged foundation.**
      Deliver the following ordered units through focused PRs. Each states a
      concrete acceptance boundary; existing API successes remain prerequisites,
      not substitutes for production-executable acceptance.

      - **18.4.1 — Close resource-admission failure coverage.** Qualify the
        existing incremental filesystem/sink/output/checkpoint APIs under
        capacity, unsafe identity, partial preparation, failed publication and
        cleanup faults. Audit inherited nested-close accounting and document
        any unavoidable quarantine/retention instead of claiming reclamation.
        Acceptance: old descriptors, bytes, anchors and the held root lease
        survive failed admission; no recreate-all, repair, truncation or replay.

      - **18.4.2 — Finish focused history and its LIVE handoff.** Retain the
        merged quiet `after=0`, old-LIVE-during-held-HTTP and typed pre-drop
        rejection regressions. Add overlap/deduplication and conflict cases,
        missing/invalid IDs, old-query/STATUS traffic, pending same/changed
        generation metadata, stop/deadline/clock/pressure and output-fault cases.
        Define recovery-view ownership and the exact handoff to steady LIVE.
        Acceptance: only the new scope is fetched, no metadata is silently
        adopted, and no failed/incomplete recovery exposes a usable FIFO.
        Keep REST fetch cursor, LIVE IDs, processing anchor and user read cursor
        distinct; preserve UNKNOWN baselines and partial-append/crash ambiguity.

      - **18.4.3 — Wire the dynamic owner admission state machine.** Connect
        inventory proposal, append-only registry delta, subscription/ACK,
        resource admission, focused history and dispatch/input readiness in one
        serialized owner. Bound records and bytes, schedule HTTP/writes outside
        callbacks, and keep old windows/heartbeats/pending replies serviced.
        Acceptance: a newly observed or explicitly opened query gets stable
        routing and becomes usable only after the required stages succeed,
        without reconnecting, renumbering old slots or rebuilding old parsers.
        Cancellation and each failed stage leave explicit owned/pending state.

      - **18.4.4 — Connect dynamic FIFO routing and window lifecycle.** Admit a
        new window's real input parser at the owner's readiness boundary; route
        the declared SEND/JOIN/PART/QUERY subset through network/window FIFOs.
        Reconcile supported window lifecycle changes with actual receipts and
        inventory, preserving existing logs and unrelated account service.
        Acceptance: one real process handles new-query input and output while
        old windows continue working. Following buffered input cannot bypass a
        pending operation or revoked destination; unsupported topology/control
        changes fail explicitly, with no shell/raw IRC passthrough or resend.

      - **18.4.5 — Qualify account-wide reconnect, restart and shutdown.** Apply
        finite heartbeat/backoff/session-expiry policy to the composed owner,
        reconcile topology/source epochs and restore only validated processing
        state. A process restart requests credentials again, not a saved bearer.
        Acceptance: actual CLI restart, network loss, pending stop and EOF/signal
        fixtures preserve prefixes and locking, reject stale/conflicting or
        encrypted input, bound work and disclose uncertain outgoing outcomes.
        Never turn an incoming maximum ID into an unproven durable anchor or
        promise durable exactly-once output.

      - **18.4.6 — Close integrated fault and regression evidence.** Diagnose or
        adequately characterize the retained original output-fixture teardown
        timeout with phase-specific evidence; a later PASS is not its cause.
        Exercise root ownership/links, append/checkpoint ambiguity, descriptor
        and lease lifetime, bounded queues, cancellation and cleanup in the final
        composition. Acceptance: deterministic sensitivity/fault controls and
        current-source sanitizer/strict-Valgrind evidence, with unresolved
        limits and missing historical artifacts stated rather than rewritten.

      - **18.4.7 — Prove the actual installed account workflow.** Use the real
        production executable through PTY plus verified local HTTPS/WSS, then
        repeat the operational shell workflow with a byte-verified staged
        installation. Cover hidden-password restoration/secrecy, existing
        multi-network/window projection, new query without reconnect, FIFO send,
        log receive, supported controls and restart. Acceptance: no test-only
        adapter stands in for the CLI, no chat appears in its diagnostic terminal,
        and credentials never enter persisted files, argv or diagnostics.

      - **18.4.8 — Align documentation and freeze the local beta subset.** Audit
        README, manual, help, architecture, configuration/migration notes and
        examples against the actual executable, including its unsupported E2EE
        boundary and delivery guarantees. Acceptance: documented account/FIFO
        paths and commands work from a clean setup; existing user data is never
        silently migrated/deleted. Reconcile final whole-diff/component reviews
        and exact-head required CI before advancing to milestone 19.

    Completion of 18 means a basic connected plaintext client works against the
    local protocol fixtures. It does not yet mean real Grappa compatibility,
    implemented E2EE, recipient delivery or a release-ready beta.
19. **Qualify portability and interoperability.** After all of 18, exercise the
    operational executable on Linux glibc/musl and FreeBSD, including
    hostile filesystem, sanitizers and memory/leak checks. Retain local
    REST/Phoenix integration fixtures and add opt-in live Grappa smoke tests.
    Before live access, require explicit owner authorization of the endpoint,
    declared server revision, dedicated account and allowed read/write targets
    and operations; planning this milestone does not authorize server writes.
    Acceptance: recorded versioned evidence for real CLI authentication,
    subscription, FIFO send/log receive, controls and reconnect/history behavior
    within the supported plaintext subset. Simulations are not live-server
    compatibility claims. No real Grappa credentials or live-server traffic in CI.

    - **19.1 — Final executable/platform matrix.** Qualify the installed beta
      candidate on Linux glibc with GCC/Clang, genuine musl/static TLS and native
      FreeBSD. Include hostile filesystem/FD faults, no-procfs portable fixtures,
      path quoting, sanitizers and strict leak checks. Acceptance: current-source
      platform/runtime evidence, not reused component or previous-head labels.
    - **19.2 — Bounded real-Grappa account workflow.** Pin and record the actual
      server revision/protocol, dedicated account and owner-authorized targets
      and operations before traffic. Use the production CLI for login, discovery,
      FIFO send/log receive, supported controls and reconnect/history. Acceptance:
      observed remote outcomes and, where claimed, independent recipient
      confirmation; no automatic replay or broader live/CI access.
    - **19.3 — Compatibility and residual-risk sign-off.** Reconcile local and
      live results, declared plaintext support, negative encryption behavior,
      restart/output ambiguity and remaining limitations. Acceptance: no open
      blocker in the frozen beta subset; explicitly deferred work stays deferred.

20. **Cut `0.1.0-beta.1`.** Only after 18 and 19 pass, freeze the supported
    subset, complete examples and upgrade/troubleshooting notes, audit
    dependencies/licenses, and publish reproducible source archives, checksums
    and qualification results through the PR/CI gate. Acceptance: fresh-machine
    build/install/uninstall and end-to-end shell exchange using the installed
    executable against the declared, explicitly authorized Grappa revision.

    - **20.1 — Release-candidate and documentation freeze.** Record the exact
      supported plaintext feature set, dependency/license audit, compatibility
      floor, migration/recovery guidance and known limits. Acceptance: every
      beta claim maps to evidence from 18.4 and 19; E2EE is not implied.
    - **20.2 — Verify the actual delivery artifact.** Check main CI, archive
      membership/bytes/modes and checksums; build a fresh extraction and exercise
      install/uninstall plus the installed shell workflow on the declared target.
      Acceptance: reproducible source delivery and tested instructions, not only
      a successful worktree build or `--version` smoke check.
    - **20.3 — Explicit beta go/no-go and tag.** Review the complete acceptance
      ledger and obtain release authorization before publishing the beta tag,
      checksums and notes. A foundation merge or green CI alone does not cut a
      release. Start post-beta E2EE design only through its separate review.

## After the first beta: Grappa E2EE

Implement the documented encrypted-query protocol only after a dedicated design
and security review: device identity/registration, pairing and verification,
key persistence/protection, session and prekey lifecycle, ciphertext handling,
offline queues, migration and cross-client interoperability tests. Reuse audited
cryptographic libraries; do not invent crypto. Review Grappa's reference bot and
current client protocol at implementation time. TLS remains mandatory for remote
credentials regardless of whether E2EE is available.
