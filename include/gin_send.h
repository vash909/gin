#ifndef GIN_SEND_H
#define GIN_SEND_H

#include <stddef.h>
#include <stdint.h>
#include "gin_protocol.h"

#define GIN_SEND_BODY_MAX ((size_t)4096)
#define GIN_SEND_JSON_MAX GIN_HTTP_REQUEST_MAX
#define GIN_SEND_PATH_MAX ((size_t)2048)
#define GIN_SEND_URL_MAX ((size_t)4096)
#define GIN_SEND_RESPONSE_MAX ((size_t)65536)
#define GIN_SEND_DEPTH_MAX 32u

enum gin_send_role {
    GIN_SEND_ROLE_UNKNOWN, GIN_SEND_ROLE_CHANNEL, GIN_SEND_ROLE_QUERY
};
enum gin_send_mode {
    GIN_SEND_MODE_UNKNOWN, GIN_SEND_MODE_PLAIN, GIN_SEND_MODE_ENCRYPTED
};
/* Trusted owner context, not inferred from FIFO text or filesystem names.
 * A role/mode is an explicit owner assertion, not server attestation.
 * Use the destination peer for queries, never an inbound DM's source topic. */
struct gin_send_route {
    const char *network_slug;
    const char *wire_target;
    const char *chantypes;
    enum gin_send_role role;
    enum gin_send_mode mode;
};
struct gin_send_settings {
    struct gin_auth_settings auth;
    struct gin_protocol_policy protocol;
};
enum gin_send_code {
    GIN_SEND_OK, GIN_SEND_INVALID, GIN_SEND_UNSUPPORTED, GIN_SEND_LIMIT,
    GIN_SEND_NOMEM, GIN_SEND_PROTOCOL, GIN_SEND_AUTH, GIN_SEND_REMOTE,
    GIN_SEND_MALFORMED, GIN_SEND_TLS, GIN_SEND_TIMEOUT, GIN_SEND_TRANSPORT
};
enum gin_send_outcome {
    GIN_SEND_NOT_ATTEMPTED,
    GIN_SEND_ACCEPTED_PERSISTED,
    GIN_SEND_ACCEPTED_NO_PERSIST,
    GIN_SEND_REMOTE_FAILURE,
    GIN_SEND_UNCERTAIN
};
struct gin_send_result {
    enum gin_send_code code;
    enum gin_send_outcome outcome;
    uint64_t request_id;
    int64_t message_id; /* Last persisted fragment only; zero when absent. */
    long http_status; /* POST status only, diagnostic even on incomplete I/O. */
    enum gin_protocol_code protocol; /* INVALID until discovery is invoked. */
    enum gin_auth_code auth; /* INVALID unless auth_attempted is true. */
    int auth_attempted;
    int post_attempted;
};
/* Synchronous bounded native adapter, NEVER call in runtime/network callbacks.
 * The owner retains exactly one live root, all input storage and the existing
 * global HTTP lifecycle for the whole call. No descriptors are consumed.
 * Validate local inputs, origin/UUID binding and explicit PLAIN policy before
 * network I/O; fetch fresh config, verify bound /me, then issue one plain POST.
 * Timeouts bound EACH request, not the complete three-request sequence.
 * request_id is a nonzero owner-issued operation identity; the owner must never
 * reuse it. It is returned unchanged, not sent as an invented server nonce.
 * A response belongs only to this synchronous HTTP operation, not a Phoenix ref.
 * No queue, retry, local echo, persistence or credential import is performed.
 * NOT_ATTEMPTED means the message POST API was not invoked. Attempt flags prove
 * API invocation, not actual wire traffic. Any POST failure may have partial
 * side effects: REMOTE_FAILURE is a server error, NOT proof of no prior send.
 * No outcome, including a 201 row or 202 acceptance, proves peer delivery.
 * Unknown/malformed success or incomplete POST is UNCERTAIN. Never auto-replay.
 * Caller pointers must be valid and nonaliasing. See docs/SEND.md for scope. */
struct gin_send_result gin_send_plain(const struct gin_root *root,
    const struct gin_send_settings *settings,
    const struct gin_credential *credential, const struct gin_send_route *route,
    uint64_t request_id, const unsigned char *body, size_t length);
/* Static diagnostics only; never peer data, bodies, URLs or credentials. */
const char *gin_send_error(enum gin_send_code code);
const char *gin_send_outcome_name(enum gin_send_outcome outcome);

#endif
