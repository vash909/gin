#ifndef GIN_HTTP_CONTROL_H
#define GIN_HTTP_CONTROL_H
#include "gin_http.h"
#include "gin_protocol.h"
#include "gin_auth.h"
#include "gin_topology.h"
#include "gin_send.h"
#include "gin_control.h"
/* Explicit private opt-in; legacy public settings layouts remain unchanged.
 * Borrow for the complete synchronous aggregate. Initialize aborted to zero.
 * tick runs outside libcurl callbacks; it may reduce the proposed wait (ms),
 * and returns nonzero to latch abort. The caller owns the authoritative reason,
 * including graceful stop, which need not be a public transport error. */
struct gin_http_control {
    int (*tick)(void *opaque, unsigned *wait_ms);
    void *opaque;
    int aborted;
};
struct gin_http_result gin_http_get_controlled(const struct gin_http_settings *,
    unsigned char *, size_t, struct gin_http_control *);
struct gin_http_result gin_http_request_controlled(const struct gin_http_request *,
    unsigned char *, size_t, struct gin_http_control *);
enum gin_protocol_code gin_protocol_fetch_controlled(const struct gin_auth_settings *,
    const struct gin_protocol_policy *, struct gin_protocol_info *, struct gin_http_control *);
enum gin_auth_code gin_auth_identity_controlled(const struct gin_auth_settings *,
    const struct gin_credential *, struct gin_credential *, struct gin_http_control *);
enum gin_topology_code gin_topology_fetch_controlled(const struct gin_auth_settings *,
    const struct gin_protocol_policy *, const struct gin_credential *, const char *,
    struct gin_topology **, struct gin_http_control *);
struct gin_send_result gin_send_plain_controlled(const struct gin_root *,
    const struct gin_send_settings *, const struct gin_credential *,
    const struct gin_send_route *, uint64_t, const unsigned char *, size_t,
    struct gin_http_control *);
struct gin_control_result gin_control_join_controlled(struct gin_root *,
    const struct gin_send_settings *, const struct gin_credential *,
    const struct gin_send_route *, uint64_t, struct gin_http_control *);
struct gin_control_result gin_control_part_controlled(struct gin_root *,
    const struct gin_send_settings *, const struct gin_credential *,
    const struct gin_send_route *, uint64_t, struct gin_http_control *);
/* History types stay forward-declared: unrelated HTTP consumers do not acquire
 * the complete history/render/connect header graph. Caller owns abort reason. */
struct gin_history_fetch_result;
struct gin_history_scope;
struct gin_history_request;
struct gin_history_page;
struct gin_history_fetch_result gin_history_fetch_controlled(
    const struct gin_auth_settings *, const struct gin_protocol_policy *,
    const struct gin_credential *, const struct gin_topology *,
    const struct gin_history_scope *, const struct gin_history_request *,
    struct gin_history_page **, struct gin_http_control *);
#endif
