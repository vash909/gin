#ifndef GIN_ACCOUNT_REGISTRY_H
#define GIN_ACCOUNT_REGISTRY_H
#include "account_plan.h"
#include <stdint.h>
/* Pure owner-asserted scope binding, NOT authentication or plaintext authority.
 * Typed topology must have complete queries. No credentials, Native handles,
 * I/O, clock, JOIN, filesystem, history or readiness operations.
 * All inputs disjoint, valid NUL-terminated strings, borrowed for the call.
 * Origin/UUID/topic subject are distinct exact identities; epoch must be nonzero.
 * Constructor requires fresh *out == NULL. All views are owned until free.
 * Fixed retained storage: <=63 networks/windows/nonroot sources each, <=64
 * aggregate sources including Root. No realloc, threads or global state.
 */
struct gin_account_registry;
struct gin_account_registry_scope {
    const char *origin, *subject_uuid, *topic_subject, *chantypes;
    uint64_t epoch;
};
enum gin_account_registry_code {
    GIN_ACCOUNT_REGISTRY_OK, GIN_ACCOUNT_REGISTRY_INVALID,
    GIN_ACCOUNT_REGISTRY_UNKNOWN, GIN_ACCOUNT_REGISTRY_LIMIT,
    GIN_ACCOUNT_REGISTRY_NOMEM, GIN_ACCOUNT_REGISTRY_SCOPE,
    GIN_ACCOUNT_REGISTRY_UNSUPPORTED
};
struct gin_account_registry_delta {
    size_t windows_added, sources_added;
    uint64_t generation;
};
enum gin_account_registry_code gin_account_registry_new(const struct gin_topology *,
    const struct gin_account_registry_scope *, struct gin_account_registry **);
void gin_account_registry_free(struct gin_account_registry *);
const struct gin_account_registry_scope *gin_account_registry_scope(const struct gin_account_registry *);
size_t gin_account_registry_network_count(const struct gin_account_registry *);
size_t gin_account_registry_window_count(const struct gin_account_registry *);
size_t gin_account_registry_source_count(const struct gin_account_registry *);
uint64_t gin_account_registry_generation(const struct gin_account_registry *);
const struct gin_topology_network *gin_account_registry_network_at(const struct gin_account_registry *, size_t);
const struct gin_topology_window *gin_account_registry_window_at(const struct gin_account_registry *, size_t);
/* Source index 0 is implicit Root (network/target NULL); initial ordering equals
 * cold plan ordering, then append only. Existing slot addresses/bytes/string
 * addresses never change, including across canonical case/opened_at refreshes.
 */
const struct gin_connect_subscription *gin_account_registry_source_at(const struct gin_account_registry *, size_t);
/* Full replacement, not a delta. Omitted registered queries, changed joined
 * channels, network IDs/exact slugs/nicks are UNSUPPORTED. Only explicit QUERY
 * additions pass the existing closed /query peer grammar. All additions/caps
 * staged before publication. Failure leaves registry and *out byte-identical.
 * Success advances generation only for additions (overflow rejected). This is
 * NOT a fresh metadata mirror: original spellings and timestamps stay retained.
 */
enum gin_account_registry_code gin_account_registry_apply(struct gin_account_registry *,
    const struct gin_topology *, const struct gin_account_registry_scope *,
    struct gin_account_registry_delta *out);
#endif
