#ifndef GIN_HISTORY_H
#define GIN_HISTORY_H
#include <stddef.h>
#include <stdint.h>
#include "gin_protocol.h"
#include "gin_render.h"
#include "gin_topology.h"

/* Local limits, not server guarantees. The HTTP adapter independently caps its
 * raw response at 1 MiB. Retained budgets exclude raw input, JSON DOM, transport
 * and a simultaneously retained old snapshot during atomic replacement. */
#define GIN_HISTORY_PAGE_ROWS_MAX 200u
#define GIN_HISTORY_PAGE_DEFAULT 50u
#define GIN_HISTORY_RESPONSE_MAX ((size_t)1048576)
#define GIN_HISTORY_DEPTH_MAX 32u
#define GIN_HISTORY_ROWS_MAX 4096u
#define GIN_HISTORY_OWNED_MAX ((size_t)8388608)
#define GIN_HISTORY_LIVE_ROWS_MAX 512u
#define GIN_HISTORY_PAGES_MAX 16u
#define GIN_HISTORY_ATTEMPT_MS_MAX UINT64_C(60000)
#define GIN_HISTORY_PATH_MAX 2048u
#define GIN_HISTORY_BODY_MAX 16384u
#define GIN_HISTORY_SENDER_MAX 256u
#define GIN_HISTORY_KIND_MAX 64u

/* Deliberately ID-only. No around/time cursor, hidden server count probe,
 * read-cursor mutation, E2EE inbox, send or implicit retry operation. */
enum gin_history_cursor { GIN_HISTORY_LATEST, GIN_HISTORY_BEFORE, GIN_HISTORY_AFTER };
struct gin_history_request {
    enum gin_history_cursor cursor;
    int64_t anchor; /* LATEST:0; BEFORE:positive; AFTER:>=0 (0=explicit beginning). */
    unsigned limit; /* 1..200 inclusive, always emitted explicitly. */
};
enum gin_history_code {
    GIN_HISTORY_OK, GIN_HISTORY_IGNORE, GIN_HISTORY_INVALID,
    GIN_HISTORY_MALFORMED, GIN_HISTORY_LIMIT, GIN_HISTORY_NOMEM,
    GIN_HISTORY_ROUTE, GIN_HISTORY_UNSUPPORTED, GIN_HISTORY_CONFLICT,
    GIN_HISTORY_STALE, GIN_HISTORY_DEADLINE, GIN_HISTORY_PREFLIGHT,
    GIN_HISTORY_AUTH, GIN_HISTORY_TLS, GIN_HISTORY_TIMEOUT,
    GIN_HISTORY_TRANSPORT, GIN_HISTORY_REMOTE
};
enum gin_history_source { GIN_HISTORY_REST, GIN_HISTORY_LIVE, GIN_HISTORY_MERGED };
/* Trusted single-owner binding, not independently an authentication proof.
 * Constructors copy every string. The owner supplies a current known topology
 * and explicit plaintext policy, not a role inferred from a local filename.
 * UUID is canonical 36-byte lower-case hex/hyphens; origin is exact HTTPS
 * authority. Source epoch is nonzero, immutable per recovery attempt.
 * CHANNEL requires a channel target; STATUS exactly $server; QUERY a peer.
 * Numeric network ID is positive; its slug is never converted from that ID.
 * own_nick is explicit identity context, not a way to invent a DM destination. */
struct gin_history_scope {
    const char *origin;
    const char *subject_id;
    const char *network_slug;
    const char *target;
    const char *chantypes;
    const char *own_nick;
    int64_t network_id;
    uint64_t socket_epoch;
    enum gin_render_role role;
    enum gin_render_encryption encryption;
};
/* Borrowed immutable decoded row. Body NULL is distinct from empty string.
 * dm_present=0 means absent; present NULL and string remain distinct. Every
 * successful decode preserves consumed original values even under tested
 * Jansson scratch-allocation faults; ignored metadata is not so attested.
 * These are server-scoped persisted IDs, not IRC message IDs or delivery ACKs. */
struct gin_history_row {
    int64_t id;
    int64_t server_time;
    const char *network;
    const char *channel;
    const char *kind;
    const char *sender;
    const char *body;
    const char *dm_with;
    int dm_present;
};
struct gin_history_page;
/* Fresh NULL output required; never replace occupied output. Strict bounded
 * whole-document JSON, duplicate/NUL/UTF-8/depth checks; flat array <=limit.
 * Latest/before IDs strictly DESC, after strictly ASC, strict anchor relation.
 * CHANNEL rows match target and absent/null dm_with; QUERY requires dm_with
 * present (feature floor36), matching peer when nonnull; a null peer is only
 * valid for non-chat window events whose channel matches the peer. STATUS
 * exactly matches $server with absent/null dm_with. Unknown additive metadata
 * is ignored after full validity checks. Reserved e2e_ kinds/CTCP E2E refuse
 * before ordinary output or any recovery-cursor advancement, for all roles.
 * No source I/O/authentication, inferred query inventory or display occurs. */
enum gin_history_code gin_history_page_decode(const struct gin_history_scope *scope,
    const struct gin_history_request *request, const unsigned char *body,
    size_t length, struct gin_history_page **out);
/* Pure borrowed real callback conversion. Context must describe a CURRENT
 * JOINED source supplied by the trusted connection owner; check actual topic,
 * nullable join-ref, scope/observed epoch and destination independently.
 * Only event/event kind message is a row; other plaintext events IGNORE.
 * Inbound query channel may be our OLD nick: context.source_target is transport
 * identity while dm_with establishes the known destination peer. No fake
 * joined frame is constructed for REST. No callback HTTP, disk or reentry. */
enum gin_history_code gin_history_live_decode(const struct gin_phoenix_frame *frame,
    const struct gin_render_context *context, const struct gin_history_scope *scope,
    uint64_t observed_epoch, struct gin_history_page **out);
void gin_history_page_free(struct gin_history_page *page);
size_t gin_history_page_count(const struct gin_history_page *page);
size_t gin_history_page_owned_bytes(const struct gin_history_page *page);
const struct gin_history_row *gin_history_page_row(const struct gin_history_page *page, size_t index);
const struct gin_history_scope *gin_history_page_scope(const struct gin_history_page *page);
enum gin_history_source gin_history_page_source(const struct gin_history_page *page);
const struct gin_history_request *gin_history_page_request(const struct gin_history_page *page);
/* Project a validated owned row using the existing escaped presentation and
 * checked append format, not a fabricated authenticated Phoenix frame. Unknown
 * row kinds IGNORE; no output/cursor/ack side effect, out freshNULL. */
enum gin_render_code gin_history_render(const struct gin_history_page *page,
    size_t index, struct gin_render_record **out);
/* Pure path builder; no retained allocation, I/O, emitted origin or bearer.
 * Shared scope validation may allocate temporary libcurl URL-parser storage.
 * Validate entire scope/request first. Percent-encode each ORIGINAL slug/window UTF-8 byte
 * independently using RFC3986 unreserved bytes, uppercase hex. Format:
 * /networks/SEG/channels/SEG/messages?limit=N[&before=ID|&after=ID].
 * Capacity includes NUL; on any failure buffer and *written stay unchanged. */
enum gin_history_code gin_history_path(const struct gin_history_scope *scope,
    const struct gin_history_request *request, char *output, size_t capacity, size_t *written);
struct gin_history_fetch_result {
    enum gin_history_code code;
    enum gin_protocol_code protocol;
    enum gin_auth_code auth;
    long http_status;
    int auth_attempted;
    int get_attempted;
};
/* Synchronous outside all callbacks: validate stored binding/topology locally,
 * fresh config -> bound /me -> ONE authenticated focused GET. Effective server
 * floor4, QUERY36; no time cursors. No automatic fan-out/retries or persistence.
 * The existing caller-owned HTTP global lifecycle remains required.
 * Deadlines are per HTTP request; recovery owner must check whole-attempt
 * deadline AFTER this function before accepting the page. Inputs unchanged. */
struct gin_history_fetch_result gin_history_fetch(const struct gin_auth_settings *settings,
    const struct gin_protocol_policy *policy, const struct gin_credential *credential,
    const struct gin_topology *topology, const struct gin_history_scope *scope,
    const struct gin_history_request *request, struct gin_history_page **out);

enum gin_history_recovery_state {
    GIN_HISTORY_RECOVERING, GIN_HISTORY_VISIBLE_DRAINED,
    GIN_HISTORY_INCOMPLETE_LIMIT, GIN_HISTORY_INCOMPLETE_DEADLINE
};
struct gin_history_recovery;
struct gin_history_status {
    enum gin_history_recovery_state state;
    unsigned pages;
    unsigned live_rows;
    size_t rows;
    size_t owned_bytes;
    int64_t fetch_after; /* REST pagination ONLY, NEVER a delivery/checkpoint ACK. */
    int unknown_before; /* initial anchor0: no trusted previous processing anchor. */
};
/* Pure owned bounded attempt; trusted caller anchor>=0, page_limit1..200.
 * Deadline now_ms+60000, overflow/backward supplied clock rejected. No clocks,
 * HTTP, filesystem, output ACK or outgoing operation replay is hidden here.
 * Anchor is caller supplied, never derived from /me read cursor or boot heads.
 * Even a trusted nonzero anchor cannot attest retention/server generation. */
enum gin_history_code gin_history_recovery_new(const struct gin_history_scope *scope,
    int64_t anchor, unsigned page_limit, uint64_t now_ms, struct gin_history_recovery **out);
void gin_history_recovery_free(struct gin_history_recovery *recovery);
struct gin_history_status gin_history_recovery_status(const struct gin_history_recovery *recovery);
enum gin_history_code gin_history_recovery_next(struct gin_history_recovery *recovery,
    uint64_t now_ms, struct gin_history_request *out);
/* REST input must match next AFTER request exactly and current full scope.
 * Rest cursor advances ONLY by accepted REST rows, never by buffered live max.
 * Both feeds deep-copy atomically; failures preserve rows/cursor and old borrowed
 * views. Resource/deadline exhaustion latches INCOMPLETE, never silently evicts.
 * Duplicate persisted ID with equal consumed fields is one row; conflicting
 * same ID => CONFLICT. Ignore unrelated metadata, not body/null/routing changes.
 * Staged view is numeric ID ASC, not timestamp order. At most16 REST pages,
 * 512 DISTINCT admitted live rows,4096 combined rows,8MiB retained snapshot.
 * A short/empty AFTER page means visible drain AT THAT REQUEST, not an atomic
 * REST/WSS cut or proof of complete original history. After drain no feeds.
 * Borrowed view valid until next successful feed/free. During catch-up defer
 * ordinary appends; append-only files cannot insert old rows before old output.
 * Owner appends outside callbacks and advances its own trusted checkpoint only
 * after successful output. Crash/restart may duplicate or lose uncertain output;
 * no append/checkpoint atomicity, durability or exactly-once promise. */
enum gin_history_code gin_history_recovery_page(struct gin_history_recovery *recovery,
    const struct gin_history_page *page, uint64_t now_ms);
enum gin_history_code gin_history_recovery_live(struct gin_history_recovery *recovery,
    const struct gin_history_page *page, uint64_t now_ms);
const struct gin_history_page *gin_history_recovery_view(const struct gin_history_recovery *recovery);
const char *gin_history_error(enum gin_history_code code);
#endif
