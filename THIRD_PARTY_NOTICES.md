# Third-party notices

## Grappa client protocol documentation

The files `CLIENT_PROTOCOL.md` and `LICENSE` inside the full-commit directories
under `docs/upstream/grappa/` are unmodified snapshots from
https://github.com/vjt/grappa-irc.

Copyright (c) 2026 Marcello Barnaba. Distributed under the upstream MIT License,
whose full original text is included as `LICENSE` beside each document.
The corresponding `snapshot.json` records the exact revision, source paths and
file identities. See [snapshot policy](docs/upstream/grappa/README.md).

Only documentation and its license are copied. Grappa runtime source is neither
vendored nor linked into gin. Preserving these documents does not imply upstream
endorsement, transfer of authorship, implemented functionality or qualification
against a running Grappa server.

## Linked transport dependencies

The REST/TLS adapter links the separately licensed libcurl dependency; no
libcurl implementation is vendored. See [curl's license](https://curl.se/docs/copyright.html)
and the selected TLS backend's own notices. The optional pinned musl developer
recipe downloads curl and mbedTLS sources outside the repository. Distributors
must retain the applicable dependency licenses/notices, including those of
static libraries; a source-only gin archive is not a qualified binary release.

Gin's own source and documentation remain covered by the repository's MIT
[license](LICENSE). Do not replace the upstream attribution with gin's notice.
