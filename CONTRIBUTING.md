# Contributing

All repository content and GitHub communication must be in English: code,
comments, help/errors, documentation, issues, commits and pull requests.

1. Start from an up-to-date `main`; create a short-lived feature branch.
2. State the behavior and acceptance criteria. Add a failing test first, then
   the smallest implementation; keep the POSIX core independent of adapters.
3. Run `sh scripts/check.sh` locally for implementation changes. For
   documentation-only changes, review links, provenance, attribution and checksums
   as applicable; no application test run is needed. Existing required CI still
   applies. A local Linux pass is not a FreeBSD or macOS pass.
4. Review the staged diff for correctness, secrets, portability and scope.
5. Open a focused PR with test evidence, limitations and protocol references.
6. Merge only when the required `gate` check passes on the current PR revision.
   Squash merge; no direct main pushes, force-pushes or admin bypass.
7. Update local main after merge. Source artifacts are produced by successful
   main CI; releases and deployment remain explicit actions.

The first repository license commit bootstraps `main`; subsequent work follows
the PR gate, including the scaffold itself. No self-approval is required for a
single-maintainer repository; independent review is encouraged. GitHub's required
checks and PR requirement are enforced, including for administrators.

Use conventional commit titles, for example `feat: add bounded FIFO reads`.
Never merge failing checks, disable a gate to land a change, vendor credentials,
or claim an unimplemented network path works. See [development](docs/DEVELOPMENT.md)
and [roadmap](ROADMAP.md). Do not use public production channels as test fixtures.
