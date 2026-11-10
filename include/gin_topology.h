#ifndef GIN_TOPOLOGY_H
#define GIN_TOPOLOGY_H

#include <stddef.h>
#include <stdint.h>
#include "gin_naming.h"
#include "gin_protocol.h"

/* Local resource policies, not upstream maxima or a gin wire declaration. */
#define GIN_TOPOLOGY_SERVER_MIN 4
#define GIN_TOPOLOGY_BODY_MAX 1048576
#define GIN_TOPOLOGY_DEPTH_MAX 32
#define GIN_TOPOLOGY_NETWORK_MAX 128
#define GIN_TOPOLOGY_WINDOW_MAX 4096
#define GIN_TOPOLOGY_OWNED_MAX 8388608
#define GIN_TOPOLOGY_OPENED_AT_MAX 64

struct gin_topology;
enum gin_remote_window_role { GIN_REMOTE_CHANNEL, GIN_REMOTE_QUERY };
enum gin_channel_source { GIN_CHANNEL_NONE, GIN_CHANNEL_AUTOJOIN, GIN_CHANNEL_JOINED };

/* Borrowed immutable views, valid until the owning snapshot is freed. */
struct gin_topology_network {
    int64_t id;
    const char *slug;
    const char *own_nick;
    const char *component;
};
struct gin_topology_window {
    int64_t network_id;
    enum gin_remote_window_role role;
    int joined;
    enum gin_channel_source source;
    const char *opened_at; /* NULL for channels; opaque timestamp for queries. */
    struct gin_name name;
};

enum gin_topology_code {
    GIN_TOPOLOGY_OK, GIN_TOPOLOGY_INVALID, GIN_TOPOLOGY_MALFORMED,
    GIN_TOPOLOGY_CONFLICT, GIN_TOPOLOGY_LIMIT, GIN_TOPOLOGY_NOMEM,
    GIN_TOPOLOGY_WRONG_SERVER, GIN_TOPOLOGY_CLIENT_TOO_OLD,
    GIN_TOPOLOGY_SERVER_TOO_OLD, GIN_TOPOLOGY_UNAUTHORIZED, GIN_TOPOLOGY_SCOPE,
    GIN_TOPOLOGY_FORBIDDEN, GIN_TOPOLOGY_THROTTLED, GIN_TOPOLOGY_SUBJECT,
    GIN_TOPOLOGY_TLS, GIN_TOPOLOGY_TIMEOUT, GIN_TOPOLOGY_TRANSPORT,
    GIN_TOPOLOGY_REMOTE
};

/* Constructors require out != NULL and *out == NULL; never overwrite ownership.
 * Failure leaves an empty output NULL. Inputs/output must not alias.
 * Boot consumes networks/channels/heads outer shapes, not message semantics.
 * Own nick and local NONCHANNEL classification never establish a DM peer.
 * JSON is bounded, duplicate-free, UTF-8, NUL-free and fully consumed. */
enum gin_topology_code gin_topology_parse_boot(const unsigned char *body,
    size_t length, const char *chantypes, struct gin_topology **out);
/* Consumes a decoded query_windows_list payload, NOT a Phoenix frame or REST
 * response. Returns a new snapshot, replacing ALL old queries and retaining
 * networks/channels. Invalid input leaves base and its borrowed views intact.
 * Omitted/empty groups remove queries; {} establishes a known-empty inventory.
 * Both snapshots remain independently owned. No transport or filesystem I/O. */
enum gin_topology_code gin_topology_apply_queries(const struct gin_topology *base,
    const unsigned char *payload, size_t length, struct gin_topology **out);
/* Fresh config -> bound identity /me -> one authenticated /boot; no fan-out,
 * cache, retries or persistence. Effective required server floor >= 4; caller
 * C/R must both be positive. Existing HTTP global owner and trust policy apply.
 * Synchronous, outside runtime callbacks; deadlines are per HTTP request.
 * Caller credentials stay unchanged. Does not establish known query inventory.
 * No CLI/runtime networking, subscriptions, log projection or messaging. */
enum gin_topology_code gin_topology_fetch(const struct gin_auth_settings *settings,
    const struct gin_protocol_policy *policy, const struct gin_credential *credential,
    const char *chantypes, struct gin_topology **out);

void gin_topology_free(struct gin_topology *topology);
size_t gin_topology_network_count(const struct gin_topology *topology);
size_t gin_topology_window_count(const struct gin_topology *topology);
int gin_topology_queries_known(const struct gin_topology *topology);
const struct gin_topology_network *gin_topology_network_at(
    const struct gin_topology *topology, size_t index);
const struct gin_topology_window *gin_topology_window_at(
    const struct gin_topology *topology, size_t index);
/* Exact positive int64 ID, case-sensitive slug or already-canonical target.
 * NULL/missing/invalid lookup returns NULL; target length is 1..256 bytes.
 * No implicit folding, allocation, I/O or role inference. */
const struct gin_topology_network *gin_topology_find_id(
    const struct gin_topology *topology, int64_t id);
const struct gin_topology_network *gin_topology_find_slug(
    const struct gin_topology *topology, const char *slug);
const struct gin_topology_window *gin_topology_find_window(
    const struct gin_topology *topology, int64_t network_id, const char *canonical_target);
const char *gin_topology_error(enum gin_topology_code code);

#endif
