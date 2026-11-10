#ifndef GIN_PHOENIX_H
#define GIN_PHOENIX_H
#include <stddef.h>
#include <stdint.h>

/* Local policies for text messages, not upstream maxima or wire declaration. */
#define GIN_PHOENIX_BODY_MAX 1048576u
#define GIN_PHOENIX_DEPTH_MAX 32u
#define GIN_PHOENIX_OWNED_MAX 4194304u
#define GIN_PHOENIX_REF_MAX 64u
#define GIN_PHOENIX_TOPIC_MAX 4096u
#define GIN_PHOENIX_EVENT_MAX 128u
#define GIN_PHOENIX_STATUS_MAX 64u
#define GIN_PHOENIX_REF_CAP 21u

enum gin_phoenix_code {
    GIN_PHOENIX_OK, GIN_PHOENIX_NEED_MORE, GIN_PHOENIX_INVALID,
    GIN_PHOENIX_MALFORMED, GIN_PHOENIX_LIMIT, GIN_PHOENIX_NOMEM
};
struct gin_phoenix_view { const unsigned char *data; size_t length; };
struct gin_phoenix_bytes { unsigned char *data; size_t length; };
struct gin_phoenix_fields {
    const char *join_ref;
    const char *ref;
    const char *topic;
    const char *event;
    struct gin_phoenix_view payload;
};
enum gin_phoenix_reply_status {
    GIN_PHOENIX_REPLY_OK, GIN_PHOENIX_REPLY_ERROR, GIN_PHOENIX_REPLY_UNKNOWN
};
struct gin_phoenix_reply {
    enum gin_phoenix_reply_status status;
    const char *status_text;
    struct gin_phoenix_view response;
};
struct gin_phoenix_frame;
struct gin_phoenix_stream;
struct gin_phoenix_refs { uint64_t current; int exhausted; };
#define GIN_PHOENIX_REFS_INITIALIZER { 0, 0 }

/* Pure text codec: encode exactly five slots; decode first five of >=5,
 * validating all JSON including ignored additions. Refs are strings or null,
 * payload is an object. Metadata strings are bounded UTF-8 without C0/C1/DEL;
 * refs may be empty, topic/event may not. No implicit reference coercion.
 * Complete JSON is length-aware, duplicate/NUL-free, depth/body bounded.
 * decode requires out != NULL and *out == NULL; occupied output is untouched.
 * NULL bytes are allowed only at length zero; empty complete JSON is MALFORMED.
 * encode requires an empty {NULL,0} output and disjoint stable input. No partial
 * publication on failure. Owned frame cap includes frame, copied metadata and
 * compact JSON views, NOT temporary Jansson DOM, stream or encoded buffers. */
enum gin_phoenix_code gin_phoenix_decode(const unsigned char *data, size_t length,
    struct gin_phoenix_frame **out);
enum gin_phoenix_code gin_phoenix_encode(const struct gin_phoenix_fields *fields,
    struct gin_phoenix_bytes *out);
void gin_phoenix_frame_free(struct gin_phoenix_frame *frame);
/* Wipes owned bytes, frees, and clears data/length; NULL-safe. */
void gin_phoenix_bytes_free(struct gin_phoenix_bytes *bytes);
/* All returned strings/views are borrowed until frame_free. JSON views are
 * compact complete objects; use length, not a NUL-termination assumption.
 * NULL frame => NULL; frame_reply(nonreply) => NULL. Unknown nonempty reply
 * statuses are valid but NEVER success. Known phx_reply requires object response. */
const struct gin_phoenix_fields *gin_phoenix_frame_fields(const struct gin_phoenix_frame *frame);
const struct gin_phoenix_reply *gin_phoenix_frame_reply(const struct gin_phoenix_frame *frame);
/* Exact phx_reply/topic/join_ref/ref identity, NULL distinct from empty or
 * literal "null". May match error/unknown status: caller separately checks OK.
 * Does not establish joined state, authentication or current connection epoch. */
int gin_phoenix_reply_matches(const struct gin_phoenix_frame *frame,
    const char *topic, const char *join_ref, const char *ref);
const char *gin_phoenix_error(enum gin_phoenix_code code);

/* Collector of already opcode-validated WS TEXT message bytes, NOT an RFC6455
 * parser or TCP/NDJSON delimiter detector. final must be exactly 0/1 and denote
 * END OF MESSAGE, not end of a socket read. No decode before final, even if a
 * prefix already looks like complete JSON. Split UTF-8/escapes are allowed.
 * new/feed outputs require fresh NULL. Non-final returns NEED_MORE, no frame.
 * Limit/OOM wipes pending bytes and latches dropping through the observed final
 * boundary; tails return that error, never become a new message. Final decoder
 * errors consume the boundary; the next message may recover. Invalid local
 * arguments leave collector/output unchanged. Reset clears pending/drop state.
 * Heap pending capacity <= BODY_MAX. No I/O, timers, global state or logging. */
enum gin_phoenix_code gin_phoenix_stream_new(struct gin_phoenix_stream **out);
void gin_phoenix_stream_free(struct gin_phoenix_stream *stream);
void gin_phoenix_stream_reset(struct gin_phoenix_stream *stream);
enum gin_phoenix_code gin_phoenix_stream_feed(struct gin_phoenix_stream *stream,
    const unsigned char *data, size_t length, int final, struct gin_phoenix_frame **out);

/* Caller-exclusive local refs: reset then emit ASCII decimal 1..UINT64_MAX.
 * Capacity must be >= REF_CAP; INVALID/LIMIT leaves state and output unchanged.
 * Last value marks exhaustion; never wrap or reuse. Reset is an explicit new
 * connection epoch decision. Remote wire refs remain opaque strings, not ints. */
void gin_phoenix_refs_reset(struct gin_phoenix_refs *refs);
enum gin_phoenix_code gin_phoenix_refs_next(struct gin_phoenix_refs *refs,
    char *out, size_t capacity);
#endif
