#ifndef GIN_CONNECT_H
#define GIN_CONNECT_H
#include <stddef.h>
#include <stdint.h>
#include "gin_protocol.h"
#include "gin_phoenix.h"

#define GIN_CONNECT_TOPICS_MAX 64u
#define GIN_CONNECT_SERVICE_MAX_MS 1000u
#define GIN_CONNECT_IO_MAX 1048576u
#define GIN_CONNECT_OPERATIONS_MAX 64u

enum gin_connect_code {
    GIN_CONNECT_OK, GIN_CONNECT_INVALID, GIN_CONNECT_NOMEM, GIN_CONNECT_LIMIT,
    GIN_CONNECT_UNAVAILABLE, GIN_CONNECT_PREFLIGHT, GIN_CONNECT_UNAUTHORIZED,
    GIN_CONNECT_SCOPE, GIN_CONNECT_FORBIDDEN, GIN_CONNECT_UPGRADE_REQUIRED,
    GIN_CONNECT_TLS, GIN_CONNECT_TIMEOUT, GIN_CONNECT_HANDSHAKE,
    GIN_CONNECT_TRANSPORT, GIN_CONNECT_MALFORMED, GIN_CONNECT_JOIN_REJECTED,
    GIN_CONNECT_CLOSED
};
enum gin_connect_state {
    GIN_CONNECT_IDLE, GIN_CONNECT_CONNECTING, GIN_CONNECT_JOINING,
    GIN_CONNECT_READY, GIN_CONNECT_FAILED, GIN_CONNECT_STOPPED
};
enum gin_connect_topic_state {
    GIN_CONNECT_TOPIC_DESIRED, GIN_CONNECT_TOPIC_PENDING,
    GIN_CONNECT_TOPIC_JOINED, GIN_CONNECT_TOPIC_REJECTED
};
struct gin_connect_settings {
    struct gin_auth_settings http;
    struct gin_protocol_policy policy;
    long join_ms;
};
struct gin_connect_subscription {
    const char *network; /* Required exact slug, never a numeric ID. */
    const char *target; /* NULL => network only; otherwise original wire target. */
};
struct gin_connect_topic_status {
    const char *topic;
    const char *join_ref; /* NULL outside the active pending/joined generation. */
    enum gin_connect_topic_state state;
};
struct gin_connect_result {
    enum gin_connect_code code;
    enum gin_protocol_code protocol;
    enum gin_auth_code auth; /* Meaningful only if auth_attempted. */
    int auth_attempted;
    long http_status; /* Upgrade status only, 0 if none. */
    uint64_t socket_epoch; /* Explicit start attempt, not proof of connection. */
};
struct gin_connect;
typedef void (*gin_connect_frame_fn)(void *user,
    const struct gin_phoenix_frame *frame, uint64_t socket_epoch);

/* Single-owner C99/POSIX session, not CLI/runtime wiring. Existing HTTP global
 * owner must initialize before start and clean up only after all sessions free.
 * Requires libcurl >=8.23 with SSL, AsynchDNS and WS/WSS enabled; no raw WS mode,
 * direct TLS-backend API, global signal/log policy changes or hidden thread.
 * new validates/deep-copies settings, credential and subscriptions, no I/O.
 * out must be fresh NULL, occupied output untouched. Inputs/outputs disjoint.
 * Exact credential-origin binding; account-user identities only as existing API.
 * Topic plan is root first, each needed network before its windows; deduplicate
 * exact constructed topics, preserve slug/subject case, fold only target ASCII.
 * Includes root and implicit networks within TOPICS_MAX; never silently drop.
 * No arbitrary administrative topics, implicit environment/config or persistence.
 */
enum gin_connect_code gin_connect_new(const struct gin_connect_settings *settings,
    const struct gin_credential *credential,
    const struct gin_connect_subscription *subscriptions, size_t count,
    gin_connect_frame_fn on_frame, void *user, struct gin_connect **out);

/* Explicit synchronous start, only idle/stopped/failed. EVERY attempt performs
 * fresh anonymous config/policy -> origin+UUID-bound identity -> verified WSS
 * upgrade. Never call from poll callbacks or a signal handler. HTTP/upgrade use
 * per-request http deadlines, not one aggregate deadline or hard realtime bound.
 * HTTPS authority becomes same-authority WSS /socket/websocket?vsn=2.0.0&client_proto=C.
 * Token only encoded in offered subprotocols; require selected exactly phoenix.
 * No redirects, proxies, netrc, cookies, insecure TLS, raw-mode or auto reconnect.
 * TLS >=1.2, normal certificate/hostname verification, existing CA-file policy.
 * Successful return queues root subscription; it does not imply READY.
 * All joins use one pending request and one outbound frame, actual codec/refs.
 * Root reply additionally requires positive response.protocol_version meeting R.
 * ANY required join rejection/unknown status/error/close fails the whole session;
 * partial subscription success never produces READY. Unknown extras are tolerated.
 */
enum gin_connect_code gin_connect_start(struct gin_connect *session);

/* Drive only WebSocket I/O after start. max_wait_ms 0..SERVICE_MAX_MS; zero is
 * poll-only. Drain already buffered TLS/WS data before waiting; one bounded poll,
 * <=OPERATIONS_MAX operations and <=IO_MAX data bytes per call; no busy-wait.
 * Complete TEXT messages use the existing explicit-FIN collector; control frames
 * do not terminate a partial text message. Binary fails closed. Protocol CLOSE
 * and disconnect clear active joins. Only joined-topic application events reach
 * on_frame; phx_reply and unrelated/stale join references never reach it. Server
 * broadcasts with null join_ref are accepted for already joined topics.
 * Frames are borrowed only during callback. Callback must not block/reenter or
 * free/close session; read-only getters are allowed. No event queue or logging.
 * Join deadlines cover queued send and correlated reply, independently of this
 * call's wait budget. Short/AGAIN sends retain exact unsent offset, never replay
 * an already sent prefix or reset a frame. Uncertain failures terminate session.
 */
enum gin_connect_code gin_connect_service(struct gin_connect *session,
    unsigned max_wait_ms);

/* Explicit close is idempotent/abortive, not remote logout or IRC PART. Clear
 * active generations/pending/collector/outbound; no waiting or application replay.
 * Retain owned credential and desired plan for explicit close -> start reconnect.
 * Preserve monotonic request refs across reconnects within this session; advance
 * attempt epoch, never wrap either counter. Old wire replies cannot collide by
 * automatic ref reset. Identical broadcasts have no extra invented epoch bytes.
 * free closes transport first, then best-effort wipes/frees owned secrets; NULL
 * safe. No promise to erase TLS/library allocator copies. Not callback-reentrant.
 */
void gin_connect_close(struct gin_connect *session);
void gin_connect_free(struct gin_connect *session);
enum gin_connect_state gin_connect_get_state(const struct gin_connect *session);
struct gin_connect_result gin_connect_last_result(const struct gin_connect *session);
size_t gin_connect_topic_count(const struct gin_connect *session);
/* Borrowed stable views until session_free; state/join_ref may change per call.
 * NULL/missing => NULL, count(NULL)==0, get_state(NULL)==STOPPED. */
const struct gin_connect_topic_status *gin_connect_topic_at(
    const struct gin_connect *session, size_t index);
const char *gin_connect_error(enum gin_connect_code code);
#endif
