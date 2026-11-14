#ifndef GIN_SESSION_H
#define GIN_SESSION_H

#include "gin_config.h"

/* Production startup subset declaration, not the pinned server's current
 * version or a claim of all later chat/history features. */
#define GIN_SESSION_CLIENT_PROTOCOL 36
#define GIN_SESSION_SERVER_MIN 36
#define GIN_SESSION_ORIGIN_MAX 1024u
#define GIN_SESSION_PATH_MAX 4095u
#define GIN_SESSION_MODE_MAX 15u
#define GIN_SESSION_CONNECT_MS 1500L
#define GIN_SESSION_TOTAL_MS 3000L
#define GIN_SESSION_JOIN_MS 3000L
#define GIN_SESSION_STARTUP_MS 30000L
#define GIN_SESSION_REQUEST_MAX 60000L
#define GIN_SESSION_STARTUP_MAX 600000L
#define GIN_SESSION_SERVICE_MS 50u
#define GIN_SESSION_METADATA_ROWS_MAX 8u
#define GIN_SESSION_METADATA_BYTES_MAX 1048576u

struct gin_session_settings {
    struct gin_settings local;
    char origin[GIN_SESSION_ORIGIN_MAX + 1u];
    char mode[GIN_SESSION_MODE_MAX + 1u];
    char ca_file[GIN_SESSION_PATH_MAX + 1u];
    long connect_ms, total_ms, join_ms, startup_ms;
};

enum gin_session_code {
    GIN_SESSION_OK, GIN_SESSION_INVALID, GIN_SESSION_IO,
    GIN_SESSION_CREDENTIAL, GIN_SESSION_PROTOCOL, GIN_SESSION_AUTH,
    GIN_SESSION_TLS, GIN_SESSION_TOPOLOGY, GIN_SESSION_ROUTE,
    GIN_SESSION_CONNECTION, GIN_SESSION_TIMEOUT, GIN_SESSION_STATUS,
    GIN_SESSION_INPUT, GIN_SESSION_LIMIT, GIN_SESSION_UNSUPPORTED,
    GIN_SESSION_NOMEM, GIN_SESSION_CLEANUP, GIN_SESSION_BUSY,
    GIN_SESSION_OPERATION, GIN_SESSION_SEND_NOT_ATTEMPTED,
    GIN_SESSION_SEND_REMOTE_FAILURE, GIN_SESSION_SEND_UNCERTAIN,
    GIN_SESSION_CONTROL_NOT_ATTEMPTED, GIN_SESSION_CONTROL_UNCERTAIN,
    GIN_SESSION_QUERY_NOT_ATTEMPTED, GIN_SESSION_QUERY_NOT_SENT, GIN_SESSION_QUERY_UNCERTAIN
};

/* No I/O. Default timers and chantypes '#'; all required settings empty.
 * read replaces the whole value with defaults plus FILE, clearing all bytes on
 * any error. FILE uses legacy 0600/single-link/owner/no-final-symlink, 16384
 * byte/8192 physical-line bounds. Return 0, 1 unsafe/I/O, 2 malformed/bounds.
 * Separate keys: root/network/target/chantypes/origin/mode/ca_file and
 * connect_ms/total_ms/join_ms/startup_ms. No secrets, duplicate or unknown keys.
 * Validate raw bounds, text, positive canonical decimal timers, origin and
 * explicit plain enum even if CLI would override them. Naming is effective.
 * valid performs lexical/effective validation only, never root/CA probing.
 * total_ms >= connect_ms; request/join <=60000, startup <=600000. */
void gin_session_settings_init(struct gin_session_settings *settings);
int gin_session_config_read(const char *path, struct gin_session_settings *settings);
int gin_session_settings_valid(const struct gin_session_settings *settings);

/* Single foreground owner; never call from callbacks/handlers or concurrently
 * with other Gin global owners. Exactly one held root, existing fixed private
 * store; fresh topology then fresh connection start, no credential mutation.
 * status_fd -1 disables reporting; otherwise >=3, already nonblocking pipe
 * write end. Borrow input and duplicate it, preserve original flags/ownership.
 * READY\n is an atomic bounded pipe write after correlated joins and validated
 * selected route/plain policy and held-root FIFO reader acquisition;
 * backpressure/broken sink terminates cleanly. This pipe is READY-only.
 * stop_on_eof 0 ignores stdin; 1 requires an already nonblocking stdin pipe read
 * end, and only observed zero read stops; any input bytes fail, never commands.
 * Selected-window log reception, literal FIFO SEND and typed JOIN/PART controls.
 * Command targets never replace the selected owner destination. Acceptance is
 * pending membership, not an observed join/part. Selected PART terminates after
 * its outcome is reported. QUERY uses the existing correlated asynchronous
 * root operation; its terminal receipt is consumed before more FIFO input.
 * Query ACK is acceptance only, never observed membership or delivery.
 * Sends use the selected original wire target and retained bound credential,
 * outside callbacks, without local echo or automatic retry. Failures terminate
 * with static diagnostics after cleanup; accepted sends are silent. FIFO EOF
 * retains the reader with a monotonic 100ms cooldown and does not stop reception.
 * No reconnect or history. One non-reused owner operation ID per SEND.
 * Explicit plain is an operator assertion, not server encryption attestation.
 * SIGINT/TERM defer through existing synchronous startup calls, which can still
 * finish later internal requests. Checks between aggregate phases and bounded
 * service, not a hard absolute latency/aggregate deadline guarantee. Preserve
 * caller signal actions/mask/SIGPIPE and descriptors after cleanup. API emits no
 * stdio, including errors; caller may use static diagnostics after return. */
enum gin_session_code gin_session_run(const struct gin_session_settings *settings,
    int status_fd, int stop_on_eof);

/* Same owner as gin_session_run (which passes operation_fd=-1). Optional
 * operation_fd is >=3, a nonblocking FIFO with write capability and observed
 * PIPE_BUF>=192; borrowed/duplicated without changing caller flags. Must differ
 * numerically from status_fd; caller supplies independent underlying streams
 * (aliases are not detected). OP1 records contain only owner id, kind, state,
 * and unsigned native reason; no payload or recipient-delivery assertion.
 * Nonempty records get unique nonzero monotonic ids. Invalid input reports only
 * INPUT REJECTED. SEND PENDING must publish before preflight/mutation; sink
 * failure is fatal, never queued/retried. Native completion publishes one terminal
 * outcome even after a deferred signal; terminal sink failure may follow effects.
 * NOT_ATTEMPTED with GIN_SEND_INVALID also covers local owner rejection,
 * including a stop observed after PENDING but before aggregate HTTP begins.
 * No automatic replay. -1 leaves accepted operations silent. */
enum gin_session_code gin_session_run_with_operations(
    const struct gin_session_settings *settings, int status_fd,
    int stop_on_eof, int operation_fd);

/* Explicit raw-bearer enrollment: consume safe owned0600/single-link regular
 * input before root acquisition. Exact 1..4096 bearer bytes, no trimming/NUL/LF.
 * Fresh protocol resume validates identity then atomically saves to that root.
 * Defaults 10000ms connect/20000ms request; deferred signal cancellation as above.
 * No implicit resume/rebinding/retry/logout. Save may report post-rename fsync
 * ambiguity; no rollback or secure-erasure promise. NULL ca uses compiled trust.
 * No argv/environment/config token, payload diagnostics, stdout or stderr. */
enum gin_session_code gin_session_import(const char *root, const char *origin,
    const char *ca_file, const char *bearer_file);
const char *gin_session_error(enum gin_session_code code);

#endif
