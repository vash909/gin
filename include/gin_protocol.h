#ifndef GIN_PROTOCOL_H
#define GIN_PROTOCOL_H

#include <stddef.h>
#include "gin_auth.h"

#define GIN_PROTOCOL_BODY_MAX 65536
#define GIN_PROTOCOL_DEPTH_MAX 32
#define GIN_PROTOCOL_SOFTWARE_MAX 255

/* Explicit caller policy, not a claim of gin's implemented wire declaration.
 * C >= server minimum and server current >= R are independent comparisons. */
struct gin_protocol_policy {
    int client_protocol;
    int required_server_protocol;
};

struct gin_protocol_info {
    int server_protocol;
    int minimum_client_protocol;
    /* Optional untrusted diagnostic only; never used for compatibility.
     * Empty when absent, wrong-type, oversized or control-bearing. */
    char software_version[GIN_PROTOCOL_SOFTWARE_MAX + 1];
};

enum gin_protocol_code {
    GIN_PROTOCOL_OK, GIN_PROTOCOL_INVALID, GIN_PROTOCOL_MALFORMED,
    GIN_PROTOCOL_WRONG_SERVER, GIN_PROTOCOL_CLIENT_TOO_OLD,
    GIN_PROTOCOL_SERVER_TOO_OLD, GIN_PROTOCOL_TLS, GIN_PROTOCOL_TIMEOUT,
    GIN_PROTOCOL_LIMIT, GIN_PROTOCOL_TRANSPORT, GIN_PROTOCOL_REMOTE
};

/* Length-aware pure decoder/comparison. No HTTP, auth, persistence or logging.
 * Inputs and output must not alias. Valid output is cleared on every failure.
 * Server versions are positive JSON integers <= INT_MAX, minimum <= current.
 * Policy values must be positive. Unknown bounded fields are ignored. */
enum gin_protocol_code gin_protocol_parse(const struct gin_protocol_policy *policy,
    const unsigned char *body, size_t length, struct gin_protocol_info *out);
/* Server-side floor predicate only; does not claim a feature exists in gin. */
int gin_protocol_server_at_least(const struct gin_protocol_info *info, int introduced);
const char *gin_protocol_error(enum gin_protocol_code code);

/* Uses the existing HTTP global owner. Fresh unauthenticated verified-HTTPS
 * GET /api/config, exact origin, strict 200, bounded response; no cache/retry.
 * Synchronous and never suitable for poll callbacks. */
enum gin_protocol_code gin_protocol_fetch(const struct gin_auth_settings *settings,
    const struct gin_protocol_policy *policy, struct gin_protocol_info *out);

struct gin_protocol_auth_result {
    enum gin_protocol_code protocol;
    enum gin_auth_code auth; /* Meaningful only when auth_attempted is true. */
    int auth_attempted;
    struct gin_protocol_info server;
};

/* Each call performs a new same-origin preflight before the auth phase.
 * Failed preflight never attempts auth; successful preflight alone is not
 * authentication success. No implicit storage. Inputs/output must not alias.
 * Existing low-level auth functions retain their discovery precondition.
 * Resume imports an explicitly supplied bearer. For stored credentials use
 * fresh discovery then gin_auth_identity() to retain origin and UUID binding. */
struct gin_protocol_auth_result gin_protocol_login(
    const struct gin_auth_settings *settings, const struct gin_protocol_policy *policy,
    const char *identifier, const char *password, struct gin_credential *out);
struct gin_protocol_auth_result gin_protocol_resume(
    const struct gin_auth_settings *settings, const struct gin_protocol_policy *policy,
    const char *bearer, struct gin_credential *out);

#endif
