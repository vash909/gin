#ifndef GIN_SESSION_LIVE_QUEUE_H
#define GIN_SESSION_LIVE_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#include "gin_phoenix.h"

#define GIN_LIVE_ROWS_MAX 64u
#define GIN_LIVE_BYTES_MAX 2097152u
#define GIN_LIVE_SOURCES_MAX 4u

enum gin_live_code {
    GIN_LIVE_OK, GIN_LIVE_INVALID, GIN_LIVE_LIMIT, GIN_LIVE_NOMEM,
    GIN_LIVE_EMPTY
};
enum gin_live_kind {
    GIN_LIVE_NONE, GIN_LIVE_QUERY_SNAPSHOT, GIN_LIVE_MESSAGE,
    GIN_LIVE_LIFECYCLE, GIN_LIVE_OWN_NICK,
    GIN_LIVE_UNSUPPORTED, GIN_LIVE_MALFORMED
};
/* Input strings/views are borrowed for push only. Source index addresses the
 * owner's immutable <=4-topic plan, not a payload-derived destination. Source
 * join_ref is the actual current JOINED getter snapshot (nonempty); frame
 * join_ref/ref preserve NULL versus empty. Epoch is nonzero; receive_ms is an
 * explicit owner batch wall-clock value sampled OUTSIDE the callback. */
struct gin_live_view {
    struct gin_phoenix_fields fields;
    const char *source_join_ref;
    uint64_t observed_epoch;
    int64_t receive_ms;
    size_t source_index;
    enum gin_live_kind kind;
};
struct gin_live_queue;

/* No I/O, clocks, JSON parsing, renderer, global allocator changes or session
 * calls. new requires fresh NULL; rows1..64, bytes1..2097152. On any failure
 * leave out unchanged. Constructor allocation is fixed queue/pointer storage,
 * excluded from retained bytes. No preallocated payload-sized buffers.
 *
 * Push validates local shape BEFORE mutation: non-NULL q/view, kind in1..6,
 * source_index<4, epoch!=0, source_join_ref length1..REF_MAX; frame topic/event
 * length1..TOPIC/EVENT_MAX, nullable frame refs length0..REF_MAX, payload data
 * non-NULL with length1..BODY_MAX. Metadata UTF-8/JSON validity is a caller
 * precondition from the existing decoded frame, not revalidated here.
 * Copy every non-NULL string including terminator and exact payload bytes into
 * ONE owned allocation per entry with its fixed header. Account that complete
 * allocation (header, copied strings and payload) before allocating. Exclude
 * only the fixed queue object/pointer table. Check count and bytes independently
 * with overflow-safe arithmetic and subtraction. Allocation failure or count/
 * byte exhaustion latches NOMEM/LIMIT; queued earlier rows remain intact and
 * subsequent pushes return the same error. Invalid local arguments do not
 * latch or change state. No overwrite, drop-oldest or silent success.
 *
 * peek returns a borrowed immutable head valid until drop/free; drop wipes and
 * frees only that head and updates count/bytes. Empty drop returns EMPTY.
 * A latched failure is never reset by dropping: the single owner terminates.
 * Metadata8row/1MiB subcaps and admitted-source validation belong to the owner.
 * It must NOT service transport again until processing/dropping the queue.
 * free is NULL-safe and wipes owned payloads/strings/entries before freeing.
 * All accessors are NULL-safe: count/bytes0, peekNULL, failureINVALID.
 */
enum gin_live_code gin_live_queue_new(size_t rows, size_t bytes,
    struct gin_live_queue **out);
enum gin_live_code gin_live_queue_push(struct gin_live_queue *queue,
    const struct gin_live_view *view);
const struct gin_live_view *gin_live_queue_peek(const struct gin_live_queue *queue);
enum gin_live_code gin_live_queue_drop(struct gin_live_queue *queue);
size_t gin_live_queue_count(const struct gin_live_queue *queue);
size_t gin_live_queue_bytes(const struct gin_live_queue *queue);
enum gin_live_code gin_live_queue_failure(const struct gin_live_queue *queue);
void gin_live_queue_free(struct gin_live_queue *queue);
#endif
