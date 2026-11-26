#ifndef GIN_ACCOUNT_PLAN_H
#define GIN_ACCOUNT_PLAN_H
#include "gin_connect.h"
/* Private pure inventory projection, NOT runtime wiring or send authority.
 * Requires complete known queries and an authenticated public subject topic
 * label, never a UUID/bearer. No remote I/O, attachment, JOIN, FS or logging.
 * Constructor requires fresh *out == NULL; failure never changes *out.
 * Inputs disjoint, read-only; all successful views owned until plan_free.
 * Every network, joined channel and known query is retained. Own nick is a
 * transport-only subscription, never an inferred window/peer/plaintext mode.
 * Each known network also gets a transport-only $server status subscription;
 * this creates no inventory window, query, filesystem path or plaintext authority.
 * Native topics deduplicate exact gin_ws_topic values (ASCII target fold only).
 * Root, network parents, own nick, status and windows share the aggregate Native
 * limit of 64; LIMIT rejects the whole plan without partial output.
 * Immutable snapshots and plans may be freed in either order.
 */
struct gin_account_plan;
enum gin_account_plan_code {
    GIN_ACCOUNT_PLAN_OK, GIN_ACCOUNT_PLAN_INVALID, GIN_ACCOUNT_PLAN_QUERIES_UNKNOWN,
    GIN_ACCOUNT_PLAN_LIMIT, GIN_ACCOUNT_PLAN_NOMEM
};
enum gin_account_plan_code gin_account_plan_new(const struct gin_topology *,
    const char *chantypes, const char *subject_topic_name, struct gin_account_plan **);
void gin_account_plan_free(struct gin_account_plan *);
size_t gin_account_plan_network_count(const struct gin_account_plan *);
size_t gin_account_plan_window_count(const struct gin_account_plan *);
const struct gin_topology_network *gin_account_plan_network_at(const struct gin_account_plan *, size_t);
const struct gin_topology_window *gin_account_plan_window_at(const struct gin_account_plan *, size_t);
/* Counted borrowed list excludes implicit Root; all network entries precede
 * window targets. Status targets precede inventory windows; own nick follows.
 * Network/window counts describe inventory only; topic_count also includes Root
 * and status. List itself is deduplicated by actual Native topic, not path. */
const struct gin_connect_subscription *gin_account_plan_subscriptions(const struct gin_account_plan *, size_t *count);
size_t gin_account_plan_topic_count(const struct gin_account_plan *);
#endif
