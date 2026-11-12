# Roadmap to the first beta

Each numbered item is a small delivery milestone, not a claim of completion.
Ship changes through a focused PR with tests, documentation and passing required
CI. Track dependencies and evidence in the PR. The existing filesystem/CLI
scaffold is a starting point, not completion of the operational milestones.

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
8. **Add authentication and private credentials.** Implement documented login,
   session handling/revocation and secure token storage. Acceptance: wrong or
   expired sessions fail clearly; tokens never enter URLs, stdout or logs.
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
      and implement the connected CLI/configuration contract, endpoint and
      private credential-store selection using the existing authentication,
      protocol discovery, topology and connection APIs. Keep secrets out of
      argv, ordinary settings, URLs and diagnostics. Preserve read-only `check`
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

    - **18.4 — Integrated liveness, recovery and restart.** Drive existing
      heartbeat/backoff/session-expiry and history planners from the runtime.
      Schedule bounded REST work outside callbacks with an explicit WSS-service
      and deadline policy; do not silently starve heartbeats or pending replies.
      Resubscribe and reconcile current topology/source epochs after reconnect,
      merge history/live overlap by persisted identity and keep fetch cursors
      separate from successful output progress and user read cursors. Define
      restart-anchor ownership and explicit unknown-baseline behavior; retain
      partial-append/crash ambiguity rather than promising durable exactly-once
      output. Acceptance: actual executable/process restart and network-loss
      fixtures preserve logs/locking, reject stale/conflicting/encrypted input,
      bound CPU/queues/retries and never replay outgoing operations. Demonstrate
      the installed CLI's complete FIFO-send/log-receive shell workflow locally.

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
20. **Cut `0.1.0-beta.1`.** Only after 18 and 19 pass, freeze the supported
    subset, complete examples and upgrade/troubleshooting notes, audit
    dependencies/licenses, and publish reproducible source archives, checksums
    and qualification results through the PR/CI gate. Acceptance: fresh-machine
    build/install/uninstall and end-to-end shell exchange using the installed
    executable against the declared, explicitly authorized Grappa revision.

## After the first beta: Grappa E2EE

Implement the documented encrypted-query protocol only after a dedicated design
and security review: device identity/registration, pairing and verification,
key persistence/protection, session and prekey lifecycle, ciphertext handling,
offline queues, migration and cross-client interoperability tests. Reuse audited
cryptographic libraries; do not invent crypto. Review Grappa's reference bot and
current client protocol at implementation time. TLS remains mandatory for remote
credentials regardless of whether E2EE is available.
