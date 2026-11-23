#ifndef GIN_ACCOUNT_DISCOVERY_H
#define GIN_ACCOUNT_DISCOVERY_H
#include "gin_connect.h"
#include "http_control.h"
#define GIN_ACCOUNT_DISCOVERY_SNAPSHOTS_MAX 8u
#define GIN_ACCOUNT_DISCOVERY_BYTES_MAX 1048576u
enum gin_account_discovery_code { GIN_DISCOVERY_OK, GIN_DISCOVERY_INVALID,
 GIN_DISCOVERY_TOPOLOGY, GIN_DISCOVERY_CONNECT, GIN_DISCOVERY_STOP,
 GIN_DISCOVERY_TIMEOUT, GIN_DISCOVERY_LIMIT, GIN_DISCOVERY_NOMEM,
 GIN_DISCOVERY_CLOCK, GIN_DISCOVERY_CLEANUP };
struct gin_account_discovery_result {
 enum gin_account_discovery_code code;
 enum gin_topology_code topology;
 enum gin_connect_code connect;
 int cleanup_failed, quarantined;
};
/* Private synchronous read-only bootstrap. Caller owns HTTP global lifetime.
 * Fresh NULL output only, unchanged on failure. Credential/control borrowed only
 * during call; stop remains sticky. startup_ms 1..60000 is aggregate monotonic
 * budget. Exactly one root-only connection, closed before owned publication.
 * Quarantine prohibits curl global teardown. No retry or remote mutation.
 * Snapshot is BOOT plus latest drained query inventory, not an atomic cut,
 * history barrier or plaintext authority. No callbacks may reenter Native I/O. */
struct gin_account_discovery_result gin_account_discover(
 const struct gin_connect_settings *, const struct gin_credential *,
 const char *chantypes, struct gin_http_control *, unsigned startup_ms,
 struct gin_topology **out);
#endif
