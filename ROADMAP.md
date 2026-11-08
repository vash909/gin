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
18. **Enforce the E2EE boundary.** Detect encrypted queries/history and report
    unsupported encryption without outputting ciphertext as plaintext, sending
    plaintext fallback, or accepting encrypted content as ordinary chat.
    Acceptance: negative fixtures and explicit user-visible diagnostics.
19. **Qualify portability and interoperability.** Exercise Linux glibc/musl,
    macOS and FreeBSD; add hostile filesystem, sanitizers, memory/leak checks,
    local REST/Phoenix integration fixtures and opt-in live Grappa smoke tests.
    Acceptance: recorded versioned evidence; simulations are not live-server
    compatibility claims. No public Grappa credentials in CI.
20. **Cut `0.1.0-beta.1`.** Freeze the supported subset, complete examples and
    upgrade/troubleshooting notes, audit dependencies/licenses, and publish
    reproducible source archives, checksums and qualification results through
    the PR/CI gate. Acceptance: fresh-machine build/install/uninstall and
    end-to-end shell exchange against a declared Grappa revision.

## After the first beta: Grappa E2EE

Implement the documented encrypted-query protocol only after a dedicated design
and security review: device identity/registration, pairing and verification,
key persistence/protection, session and prekey lifecycle, ciphertext handling,
offline queues, migration and cross-client interoperability tests. Reuse audited
cryptographic libraries; do not invent crypto. Review Grappa's reference bot and
current client protocol at implementation time. TLS remains mandatory for remote
credentials regardless of whether E2EE is available.
