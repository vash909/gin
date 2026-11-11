#ifndef GIN_RENDER_H
#define GIN_RENDER_H
#include <stddef.h>
#include <stdint.h>
#include "gin_phoenix.h"

#define GIN_RENDER_BODY_MAX 16384u
#define GIN_RENDER_SENDER_MAX 256u
#define GIN_RENDER_DETAIL_MAX 1024u
#define GIN_RENDER_RECORD_MAX 69632u
#define GIN_RENDER_WRITE_OPERATIONS_MAX 64u

enum gin_render_code {
    GIN_RENDER_OK, GIN_RENDER_IGNORE, GIN_RENDER_INVALID,
    GIN_RENDER_MALFORMED, GIN_RENDER_LIMIT, GIN_RENDER_NOMEM,
    GIN_RENDER_RANGE, GIN_RENDER_ROUTE_MISMATCH,
    GIN_RENDER_UNSUPPORTED_ENCRYPTION, GIN_RENDER_IO
};
enum gin_render_role {
    GIN_RENDER_CHANNEL, GIN_RENDER_QUERY, GIN_RENDER_STATUS
};
enum gin_render_encryption {
    GIN_RENDER_MODE_UNKNOWN, GIN_RENDER_MODE_PLAIN, GIN_RENDER_MODE_ENCRYPTED
};
struct gin_render_context {
    const char *subject; /* Authenticated topic label, never a token/UUID guess. */
    const char *network; /* Exact authenticated network slug. */
    const char *target; /* Explicit known local destination's original target. */
    const char *source_target; /* NULL => user-root topic, else transport target. */
    const char *chantypes;
    const char *join_ref; /* Current joined source's nonempty reference. */
    uint64_t socket_epoch; /* Current source attempt, nonzero. */
    enum gin_render_role role; /* Explicit, not inferred DM from a filename. */
    enum gin_render_encryption encryption; /* Owner policy, not wire attestation. */
};
struct gin_render_record;

/* Pure bounded projection of a decoded borrowed frame. No I/O, discovery,
 * clock/TZ/locale reads, registration or session reentry. Context is supplied
 * by the trusted single owner from a CURRENT JOINED source and known topology;
 * it is not a replacement for authentication. observed_epoch comes from the
 * real callback. Validate exact constructed topic and epoch, and any non-NULL
 * frame join_ref; a null broadcast ref remains valid for that joined source.
 * Source topic target and destination target are distinct: inbound DM channel
 * can be our old nick. Require matching network and source-channel identity;
 * QUERY additionally requires explicit dm_with matching the destination peer.
 * Never infer a peer from sender or open a payload-named path. Root lifecycle
 * uses network/destination agreement. Unknown/unmapped routes fail visibly.
 * out must be fresh NULL; failure/IGNORE never publishes a record or replaces
 * an occupied output. Owned records survive frame/context destruction.
 *
 * Only Phoenix event="event": outer kind="message" with nested
 * privmsg/notice/action, plus joined/window_pending/window_invited/
 * window_invite_declined/join_failed/kicked lifecycle. Unknown kinds IGNORE;
 * recognized kinds with bad consumed fields MALFORMED. Ignore unused id/meta
 * and additive fields; do not add wire constraints for unconsumed data.
 * Body key is required; null renders empty. Integer server_time is epoch ms;
 * lifecycle uses explicit receive_ms and the recv label. Presentation range
 * is Gregorian UTC years 0001..9999, not the upstream integer domain.
 *
 * Complete chat line: [TIME] KIND sender="ESCAPED" body="ESCAPED" LF.
 * Complete lifecycle: [TIME recv] KIND target="ESCAPED" then state="LITERAL"
 * when applicable, followed in order by inviter, by, reason, numeric if
 * present/non-null and consumed for that kind. Text details are quoted;
 * numeric is positive decimal, unquoted. State must match its literal kind;
 * invite_declined has no consumed state. Optional absent/null details omit.
 * Only action kind unframes exact CTCP ACTION opener plus optional last SOH,
 * following pinned CTCP.action_text; otherwise body is escaped literally.
 *
 * Keep useful strict UTF-8, quote/backslash escapes, visible C0/C1/DEL,
 * selected bidi and Unicode separator escapes per render_values.h. Exactly
 * one physical LF, no silent truncation, no raw JSON/error/meta dumps.
 * PLAIN must be explicit for message records; encrypted/unknown refuse.
 * Reserved e2e_* outer kinds and CTCP E2E bodies (including malformed frames)
 * refuse before ordinary body output. No decryption, ACK, ciphertext logging,
 * plaintext fallback or claim of complete encryption-mode detection.
 *
 * Retained bytes <= RECORD_MAX + object/NUL; temporary DOM and caller frame
 * excluded. No allocation escapes on failure. Caller copies/owns completed
 * records in a count/byte-bounded queue, and surfaces overflow/errors after
 * service returns. Disk writes MUST occur outside the connection callback.
 */
enum gin_render_code gin_render_event(const struct gin_phoenix_frame *frame,
    const struct gin_render_context *context, uint64_t observed_epoch,
    int64_t receive_ms, struct gin_render_record **out);
struct gin_phoenix_view gin_render_record_bytes(const struct gin_render_record *record);
void gin_render_record_free(struct gin_render_record *record);

/* Outside callbacks: append one complete owned record to a caller-owned fd
 * opened by gin_root_output_open under the same live held root. Recheck file
 * type/owner/exact0600/single-link and O_APPEND/write access before writes.
 * Non-NULL appended is set to zero before I/O and reports positive progress
 * on failure; record ownership remains with caller. At most WRITE_OPERATIONS_MAX
 * write attempts, including EINTR; advance only positive short-write offsets.
 * Zero write => EIO, any exhausted/interrupted/error path => IO with errno.
 * NEVER restart a prefix or report success unless all bytes were written.
 * On partial failure owner must stop that sink, not replay the whole record.
 * No fd open/close, root/lock manipulation, rollback, record atomicity, fsync
 * or power-loss guarantee. A regular-file syscall is not a realtime bound.
 */
enum gin_render_code gin_render_append(int fd,
    const struct gin_render_record *record, size_t *appended);
const char *gin_render_error(enum gin_render_code code);
#endif
