# Opt-in live plaintext smoke and candidate evidence

This is a development workflow, not a runtime Python dependency, a release
announcement, or permission to contact a server. Default tests and CI use local
fixtures only. Never put live credentials, account identities or private server
logs in the repository, CI, issues or pull requests.

## Operator prerequisites

Obtain explicit owner approval for the HTTPS origin, declared server revision,
dedicated account, exact network and already joined plaintext test channel.
Enroll the ordinary installed CLI through [AUTH.md](AUTH.md); do not put a bearer
or password in arguments, URLs, environment variables or the smoke configuration.
The script reuses an existing private credential root and never logs in.

The operator must separately confirm that plaintext is allowed. There is no
E2EE detection/negotiation guarantee or encrypted-to-plaintext fallback. The
narrow script accepts only lowercase ASCII network slugs and `#channel` targets;
these are smoke-tool restrictions, not a redefinition of Gin's naming contract.

Use an owner-only credential root and an existing owner-only 0700 work directory
with trusted, non-symlink ancestors. Example placeholders below are not a working
account and must be replaced with the explicitly approved scope:

```sh
python3 scripts/live_smoke.py --authorize-plaintext-send \
  --gin /absolute/path/to/installed/gin \
  --root /absolute/path/to/enrolled-private-root \
  --origin https://approved.example.invalid \
  --network approved-network --target '#approved-smoke' \
  --work-dir /absolute/path/to/private-work
```

Each invocation writes exactly one unique test message to the selected FIFO.
It waits for actual READY, exact phased persisted SEND receipts, and the marker
in the selected `out`; it then stops the owner and checks unchanged credentials,
log prefix and inode. It never JOINs, PARTs, opens queries, retries a message,
connects directly to IRC, disables TLS verification, or creates an account.
Transport lifetime retries are zero. The explicit positive-test settings are
30-second connect, 60-second per-request and 180-second startup allowances;
independent parent deadlines and bounded teardown remain active. This does not
change application defaults or prove hard real-time network bounds.

A failed/uncertain result must be investigated, not automatically rerun. Effects
can already exist. Timeout alone does not identify a Gin or server defect.
The JSON result omits message content, credentials and raw child diagnostics.
`ACCEPTED_PERSISTED` plus observed receive is not remote-recipient delivery,
exactly-once output, complete history or durable storage.

Offline guards run without authorization or live traffic as part of `make check`:

```sh
python3 tests/test_live_smoke.py
```

## Owner-authorized candidate observations

The private qualification deployment reported **Grappa 1.6.4**, protocol **63**,
server minimum **1**. The owner confirms the installed revision is release
`v1.6.4`, whose annotated upstream tag resolves to commit
`874d804dde0a95e7f1be3ddc7c954989c35311da`. The tag-to-commit mapping was
verified independently; the deployment linkage is owner-attested, not a read of
the server Git checkout. Do not substitute the documentation snapshot commit.
Gin declares client
36 and requires server floor 36 for this plaintext subset. The endpoint,
credential identity, test-channel names and raw evidence are intentionally private.

The candidate is based on Gin `86c493a41f492c2f4927867ca1f38e763559da56`
with a narrow auxiliary-lifecycle projection correction and its regression tests.
The tested installed ordinary ELF SHA-256 is:

```text
8036a8a36be7e8ee93fff7187443bce172a496e418f8efc30f6a3316ab2d9fa6
```

Observed real CLI behavior:

- One authentication/enrollment, fresh bound startup, WSS subscriptions and READY.
- FIFO SEND with exact persisted receipt and one server-published log marker.
- Auxiliary JOIN/PART with receipts and independent server-inventory checks.
- QUERY accepted ACK, not a claim of peer membership or recipient delivery.
- Independently injected REST-origin message received once by the installed CLI.
- Real closure of the owned client's established WSS socket, one finite reconnect,
  a new socket in the same process, and recovery of a row persisted during the gap.
- Graceful process stop and a new process recovering a row inserted while stopped.
  Checkpoint anchors matched the independently persisted rows; log inode/prefix
  and stored credentials remained unchanged. UNKNOWN-baseline history remained
  explicit; no durable exactly-once or gap-completeness guarantee is implied.

The Linux-local core snapshot passed GCC, Clang, genuine musl, BSD make, NDEBUG,
ASan/UBSan and strict Valgrind through the unmodified canonical check script.
There were 2,857 discovered Python test invocations across repeated configurations,
64 privileged skips and 2,793 executed invocations, plus C units. These are not
unique-test totals. The installed CLI separately passed 45 tests, zero skips.
The later opt-in development driver and its offline guards have separate evidence;
these additions must not be attributed retroactively to that earlier full matrix.

This is **candidate evidence**, not a completed milestone or release record.
Current-candidate native FreeBSD/required CI and final publication remain separate
acceptance items. Cleanup received accepted channel removals, then its first
readback timed out; a fresh read-only observation verified zero scenario-owned
channels still joined and unchanged unrelated membership. No removal was replayed.
An empty test query window was retained because closing it is outside the minimal
CLI subset; no chat message was sent to that peer. Logs and credentials remain
private and intact.
Local BSD make is not FreeBSD execution. Live DM reception, encrypted content,
remote-recipient delivery and exhaustive network-failure schedules are unqualified.
