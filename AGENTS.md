# Agent guidance

- Use English exclusively for every repository artifact and GitHub message.
- This is gin, an independent C99/POSIX Grappa frontend, not a Grappa fork,
  direct IRC client, TUI or server. Keep the implementation small and Unix-like.
- Read README.md, ROADMAP.md, docs/ARCHITECTURE.md and docs/PROTOCOL.md before
  changing behavior. Distinguish shipped code from planned protocol adapters.
- Read docs/upstream/grappa/README.md and docs/COMPATIBILITY.md for protocol work.
  Preserve upstream snapshot bytes and attribution; add new pinned snapshots
  through reviewed PRs, never overwrite a retained snapshot or follow main
  automatically. Record real protocol qualification before a release tag.
- Write and run a failing regression/behavior test before implementation.
- POSIX.1-2008 and strict C99 are the baseline. No GNU C extensions, Linux-only
  APIs, C++, Rust, Elixir, Node or Python runtime dependency in the executable.
- Use a POSIX Makefile. Test GCC, Clang, musl and BSD make. Python 3 is acceptable
  for development tests only. Keep scripts POSIX sh unless explicitly justified.
- Treat server targets and filesystem components as untrusted data. Use bounded
  buffers and descriptor-relative safe filesystem operations. Do not follow
  symlinks, truncate logs or broaden permissions to recover an error.
- TLS verification is not deferred. E2EE is deferred and must fail closed until
  supported: never silently downgrade an encrypted query to plaintext.
- Never log credentials or put bearer tokens in URLs. No real credentials,
  private messages or live-server writes in default tests or GitHub Actions.
- Keep temporary builds and test trees under .build/ or the configured TMPDIR;
  do not modify unrelated services or global shell/Git configuration.
- Run sh scripts/check.sh for implementation changes, inspect git diff --check
  and obtain independent review before pushing. For documentation-only changes,
  review links, provenance, attribution and checksums as applicable; do not add
  or run application tests solely for documentation. Existing required PR CI
  still applies. Record actual checks and honest limitations in the PR.
- Minimize idle token use: never keep a model or subagent active solely to wait
  for CI, builds, downloads or other background work. Use one bounded,
  deterministic watcher with a completion notification, then release the turn.
  Do not repeatedly poll status or reread worker transcripts just to wait.
  Batch independent checks without weakening testing, review or safety gates.
- Work on a branch, open a PR, wait for required CI on its current revision,
  then squash merge. Never bypass branch protection or force-push main.
- Do not cut a beta/release until all relevant roadmap acceptance criteria have
  evidence. CI source artifacts are not a finished client or signed release.
