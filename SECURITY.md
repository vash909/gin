# Security policy

This project is an early development scaffold and is not production-ready.
No released version is currently supported. Do not use it for sensitive chat.

Report vulnerabilities privately through the repository's GitHub security
advisory reporting feature when available. If unavailable, contact the maintainer
through their GitHub profile and request a private reporting channel before
sharing details. Do not disclose credentials or private messages in issues.

The filesystem interface must be private to its owner. Existing unsafe objects
are rejected, not chmodded or deleted automatically. Never run gin as root to
work around ownership errors. Shared writable ancestors are outside the trust
boundary; do not place the root under a directory controlled by another user.

Remote transport must verify TLS certificates and hostnames, bound inputs and
queues, redact credentials and authenticate through Grappa's documented paths.
Do not implement insecure certificate bypasses as the default.

E2EE support is deferred. That does not permit plaintext fallback for encrypted
conversations. Dependency and interoperability reviews are required before beta.
