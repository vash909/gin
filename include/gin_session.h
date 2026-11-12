#ifndef GIN_SESSION_H
#define GIN_SESSION_H

#include "gin_config.h"

/* Production startup subset declaration, not the pinned server's current
 * version or a claim of all later chat/history features. */
#define GIN_SESSION_CLIENT_PROTOCOL 4
#define GIN_SESSION_SERVER_MIN 4
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
    GIN_SESSION_NOMEM, GIN_SESSION_CLEANUP, GIN_SESSION_BUSY
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
 * selected route/plain policy; backpressure/broken sink terminates cleanly.
 * stop_on_eof 0 ignores stdin; 1 requires an already nonblocking stdin pipe read
 * end, and only observed zero read stops; any input bytes fail, never commands.
 * No chat FIFO/log I/O, sends, reconnect, history or heartbeat integration.
 * SIGINT/TERM defer through existing synchronous startup calls, which can still
 * finish later internal requests. Checks between aggregate phases and bounded
 * service, not a hard absolute latency/aggregate deadline guarantee. Preserve
 * caller signal actions/mask/SIGPIPE and descriptors after cleanup. API emits no
 * stdio, including errors; caller may use static diagnostics after return. */
enum gin_session_code gin_session_run(const struct gin_session_settings *settings,
    int status_fd, int stop_on_eof);

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
