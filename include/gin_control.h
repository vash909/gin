#ifndef GIN_CONTROL_H
#define GIN_CONTROL_H
#include "gin_send.h"

enum gin_control_code {
    GIN_CONTROL_OK, GIN_CONTROL_INVALID, GIN_CONTROL_UNSUPPORTED,
    GIN_CONTROL_NOMEM, GIN_CONTROL_LIMIT, GIN_CONTROL_PREFLIGHT,
    GIN_CONTROL_UNAUTHORIZED, GIN_CONTROL_SCOPE, GIN_CONTROL_FORBIDDEN,
    GIN_CONTROL_UPGRADE_REQUIRED, GIN_CONTROL_THROTTLED, GIN_CONTROL_TLS,
    GIN_CONTROL_TIMEOUT, GIN_CONTROL_TRANSPORT, GIN_CONTROL_REMOTE,
    GIN_CONTROL_MALFORMED
};
enum gin_control_outcome {
    GIN_CONTROL_NOT_ATTEMPTED, GIN_CONTROL_ACCEPTED,
    GIN_CONTROL_REMOTE_FAILURE, GIN_CONTROL_UNCERTAIN
};
struct gin_control_result {
    enum gin_control_code code;
    enum gin_control_outcome outcome;
    uint64_t operation_id;
    long http_status;
    enum gin_protocol_code protocol;
    enum gin_auth_code auth;
    int attempted;
};
/* Synchronous isolated native controls, never runtime/network callbacks.
 * Borrow one live held filesystem root, explicit CHANNEL/PLAIN route, original
 * target, exact slug, caller-unique nonzero operation ID and existing HTTP owner.
 * Validate all local arguments -> fresh anonymous config -> bound /me -> one
 * mutation. Reuse Send settings, route and origin/UUID credential binding.
 * JOIN: POST /networks/:slug/channels with only {"name": original_target}.
 * PART: DELETE /networks/:slug/channels/:original_target, no body or reason.
 * Only completed 202 {"ok":true} is ACCEPTED, never observed membership.
 * Errors may follow effects (PART can already remove autojoin). No automatic
 * retry, log/FIFO creation, local echo, plaintext fallback or inferred role.
 * Per-request deadlines, not aggregate sequence deadline. Protocol/auth INVALID
 * until their phase is invoked; attempted means mutation API invocation, not
 * proof of actual bytes transmitted. Inputs stay borrowed/unchanged. */
struct gin_control_result gin_control_join(struct gin_root *root,
    const struct gin_send_settings *settings,
    const struct gin_credential *credential, const struct gin_send_route *route,
    uint64_t operation_id);
struct gin_control_result gin_control_part(struct gin_root *root,
    const struct gin_send_settings *settings,
    const struct gin_credential *credential, const struct gin_send_route *route,
    uint64_t operation_id);
const char *gin_control_error(enum gin_control_code code);
#endif
