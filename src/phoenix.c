#include "gin_phoenix.h"
#include "json_evidence.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#if JANSSON_VERSION_HEX < 0x020e00
#error "gin requires Jansson >= 2.14"
#endif

struct gin_phoenix_frame {
    struct gin_phoenix_fields fields;
    struct gin_phoenix_reply reply;
    size_t owned;
    int has_reply;
};

const char *gin_phoenix_error(enum gin_phoenix_code code)
{
    switch (code) {
    case GIN_PHOENIX_OK: return "ok";
    case GIN_PHOENIX_NEED_MORE: return "need-more";
    case GIN_PHOENIX_INVALID: return "invalid-settings";
    case GIN_PHOENIX_MALFORMED: return "malformed-phoenix";
    case GIN_PHOENIX_LIMIT: return "limit";
    case GIN_PHOENIX_NOMEM: return "out-of-memory";
    default: return "unknown-phoenix-error";
    }
}

static void wipe(void *data, size_t length)
{
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}
void gin_phoenix_bytes_free(struct gin_phoenix_bytes *bytes)
{
    if (!bytes) return;
    if (bytes->data) { wipe(bytes->data, bytes->length); free(bytes->data); }
    bytes->data = NULL; bytes->length = 0;
}
static void string_free(const char *s)
{
    if (s) { wipe((void *)s, strlen(s)); free((void *)s); }
}
void gin_phoenix_frame_free(struct gin_phoenix_frame *frame)
{
    if (!frame) return;
    string_free(frame->fields.join_ref); string_free(frame->fields.ref);
    string_free(frame->fields.topic); string_free(frame->fields.event);
    string_free(frame->reply.status_text);
    if (frame->fields.payload.data) {
        wipe((void *)frame->fields.payload.data, frame->fields.payload.length);
        free((void *)frame->fields.payload.data);
    }
    if (frame->reply.response.data) {
        wipe((void *)frame->reply.response.data, frame->reply.response.length);
        free((void *)frame->reply.response.data);
    }
    wipe(frame, sizeof(*frame)); free(frame);
}
const struct gin_phoenix_fields *gin_phoenix_frame_fields(const struct gin_phoenix_frame *frame)
{ return frame ? &frame->fields : NULL; }

/* Called only on strings whose UTF-8 has already been checked by Jansson. */
static enum gin_phoenix_code metadata(json_t *value, size_t max, int nullable, int empty)
{
    const unsigned char *p;
    size_t n, i;
    if (nullable && json_is_null(value)) return GIN_PHOENIX_OK;
    if (!json_is_string(value)) return GIN_PHOENIX_MALFORMED;
    n = json_string_length(value);
    if (n > max) return GIN_PHOENIX_LIMIT;
    if (!empty && !n) return GIN_PHOENIX_MALFORMED;
    p = (const unsigned char *)json_string_value(value);
    for (i = 0; i < n; i++) {
        if (p[i] < 32 || p[i] == 127 ||
            (p[i] == 0xc2 && i + 1 < n && p[i+1] >= 0x80 && p[i+1] <= 0x9f))
            return GIN_PHOENIX_MALFORMED;
    }
    return GIN_PHOENIX_OK;
}

struct dump_context { unsigned char *data; size_t length, capacity; int limit; };
static int dump_chunk(const char *data, size_t length, void *arg)
{
    struct dump_context *ctx = arg;
    if (length > ctx->capacity - ctx->length) { ctx->limit = 1; return -1; }
    if (ctx->data) memcpy(ctx->data + ctx->length, data, length);
    ctx->length += length;
    return 0;
}
/* Count with a bounded callback before allocating, never an unbounded dump. */
static enum gin_phoenix_code dump_json(json_t *value, struct gin_phoenix_bytes *out)
{
    struct dump_context ctx = { NULL, 0, GIN_PHOENIX_BODY_MAX, 0 };
    if (json_dump_callback(value, dump_chunk, &ctx, JSON_COMPACT))
        return ctx.limit ? GIN_PHOENIX_LIMIT : GIN_PHOENIX_NOMEM;
    ctx.data = malloc(ctx.length);
    if (!ctx.data) return GIN_PHOENIX_NOMEM;
    ctx.capacity = ctx.length; ctx.length = 0;
    if (json_dump_callback(value, dump_chunk, &ctx, JSON_COMPACT)) {
        wipe(ctx.data, ctx.capacity); free(ctx.data);
        return ctx.limit ? GIN_PHOENIX_LIMIT : GIN_PHOENIX_NOMEM;
    }
    out->data = ctx.data; out->length = ctx.length;
    return GIN_PHOENIX_OK;
}
static enum gin_phoenix_code copy_string(struct gin_phoenix_frame *f, json_t *value, const char **out)
{
    size_t n;
    char *p;
    if (json_is_null(value)) return GIN_PHOENIX_OK;
    n = json_string_length(value) + 1;
    if (n > GIN_PHOENIX_OWNED_MAX - f->owned) return GIN_PHOENIX_LIMIT;
    p = malloc(n);
    if (!p) return GIN_PHOENIX_NOMEM;
    memcpy(p, json_string_value(value), n);
    f->owned += n; *out = p;
    return GIN_PHOENIX_OK;
}
static enum gin_phoenix_code copy_view(struct gin_phoenix_frame *f, json_t *value,
    struct gin_phoenix_view *out)
{
    struct gin_phoenix_bytes bytes = { NULL, 0 };
    enum gin_phoenix_code code = dump_json(value, &bytes);
    if (code != GIN_PHOENIX_OK) return code;
    if (bytes.length > GIN_PHOENIX_OWNED_MAX - f->owned) {
        gin_phoenix_bytes_free(&bytes); return GIN_PHOENIX_LIMIT;
    }
    f->owned += bytes.length;
    out->data = bytes.data; out->length = bytes.length;
    return GIN_PHOENIX_OK;
}
static enum gin_phoenix_code load_json(const unsigned char *data, size_t length,
    size_t max_depth, json_t **out)
{
    json_error_t error;
    size_t i, depth = 0;
    int quoted = 0, escaped = 0;
    if (length > GIN_PHOENIX_BODY_MAX) return GIN_PHOENIX_LIMIT;
    if (!length) return GIN_PHOENIX_MALFORMED;
    /* Bound recursive work before decoding, including ignored additions.
     * Jansson subsequently checks delimiter pairing, escapes and UTF-8. */
    for (i = 0; i < length; i++) {
        unsigned char c = data[i];
        if (!c) return GIN_PHOENIX_MALFORMED;
        if (quoted) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
        } else if (c == '"') quoted = 1;
        else if (c == '{' || c == '[') {
            if (++depth > max_depth) return GIN_PHOENIX_LIMIT;
        } else if (c == '}' || c == ']') {
            if (!depth) return GIN_PHOENIX_MALFORMED;
            depth--;
        }
    }
    memset(&error, 0, sizeof(error));
    *out = json_loadb((const char *)data, length, JSON_REJECT_DUPLICATES, &error);
    if (!*out) return json_error_code(&error) == json_error_out_of_memory || !error.text[0] ?
        GIN_PHOENIX_NOMEM : GIN_PHOENIX_MALFORMED;
    if (!gin_json_evidence(data, length, *out)) {
        json_decref(*out); *out = NULL; return GIN_PHOENIX_MALFORMED;
    }
    return GIN_PHOENIX_OK;
}

const struct gin_phoenix_reply *gin_phoenix_frame_reply(const struct gin_phoenix_frame *frame)
{ return frame && frame->has_reply ? &frame->reply : NULL; }
static int nullable_equal(const char *a, const char *b)
{ return a && b ? !strcmp(a, b) : a == b; }
int gin_phoenix_reply_matches(const struct gin_phoenix_frame *frame,
    const char *topic, const char *join_ref, const char *ref)
{
    return frame && frame->has_reply && topic && !strcmp(topic, frame->fields.topic) &&
        nullable_equal(join_ref, frame->fields.join_ref) && nullable_equal(ref, frame->fields.ref);
}
static enum gin_phoenix_code reply_schema(json_t *event, json_t *payload)
{
    enum gin_phoenix_code code;
    if (strcmp(json_string_value(event), "phx_reply")) return GIN_PHOENIX_OK;
    code = metadata(json_object_get(payload, "status"), GIN_PHOENIX_STATUS_MAX, 0, 0);
    if (code != GIN_PHOENIX_OK) return code;
    return json_is_object(json_object_get(payload, "response")) ? GIN_PHOENIX_OK : GIN_PHOENIX_MALFORMED;
}

static enum gin_phoenix_code local_string(const char *text, size_t max,
    int nullable, int empty, json_t **out)
{
    size_t n;
    enum gin_phoenix_code code;
    if (!text) {
        if (!nullable) return GIN_PHOENIX_INVALID;
        *out = json_null(); return GIN_PHOENIX_OK;
    }
    for (n = 0; n <= max && text[n]; n++) {}
    if (n > max) return GIN_PHOENIX_LIMIT;
    /* Validate UTF-8 before allocation so construction failure means OOM. */
    {
        size_t i = 0;
        while (i < n) {
            unsigned char c = (unsigned char)text[i++];
            unsigned long cp;
            size_t more, k;
            if (c < 0x80) continue;
            if (c >= 0xc2 && c <= 0xdf) { more = 1; cp = c & 31u; }
            else if (c >= 0xe0 && c <= 0xef) { more = 2; cp = c & 15u; }
            else if (c >= 0xf0 && c <= 0xf4) { more = 3; cp = c & 7u; }
            else return GIN_PHOENIX_INVALID;
            if (more > n - i) return GIN_PHOENIX_INVALID;
            for (k = 0; k < more; k++) {
                c = (unsigned char)text[i++];
                if ((c & 0xc0u) != 0x80u) return GIN_PHOENIX_INVALID;
                cp = (cp << 6) | (c & 63u);
            }
            if ((more == 1 && cp < 0x80) || (more == 2 && cp < 0x800) ||
                (more == 3 && cp < 0x10000) || (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff)
                return GIN_PHOENIX_INVALID;
        }
    }
    *out = json_stringn(text, n);
    if (!*out) return GIN_PHOENIX_NOMEM;
    code = metadata(*out, max, nullable, empty);
    if (code != GIN_PHOENIX_OK) { json_decref(*out); *out = NULL; }
    return code == GIN_PHOENIX_MALFORMED ? GIN_PHOENIX_INVALID : code;
}
enum gin_phoenix_code gin_phoenix_encode(const struct gin_phoenix_fields *fields,
    struct gin_phoenix_bytes *out)
{
    json_t *root = NULL, *values[5] = { NULL, NULL, NULL, NULL, NULL };
    enum gin_phoenix_code code;
    size_t i;
    if (!fields || !out || out->data || out->length ||
        (!fields->payload.data && fields->payload.length)) return GIN_PHOENIX_INVALID;
    code = local_string(fields->join_ref, GIN_PHOENIX_REF_MAX, 1, 1, &values[0]);
    if (code == GIN_PHOENIX_OK) code = local_string(fields->ref, GIN_PHOENIX_REF_MAX, 1, 1, &values[1]);
    if (code == GIN_PHOENIX_OK) code = local_string(fields->topic, GIN_PHOENIX_TOPIC_MAX, 0, 0, &values[2]);
    if (code == GIN_PHOENIX_OK) code = local_string(fields->event, GIN_PHOENIX_EVENT_MAX, 0, 0, &values[3]);
    if (code == GIN_PHOENIX_OK) code = load_json(fields->payload.data, fields->payload.length, GIN_PHOENIX_DEPTH_MAX - 1u, &values[4]);
    if (code == GIN_PHOENIX_MALFORMED) code = GIN_PHOENIX_INVALID;
    if (code != GIN_PHOENIX_OK) goto done;
    if (!json_is_object(values[4])) { code = GIN_PHOENIX_INVALID; goto done; }
    code = reply_schema(values[3], values[4]);
    if (code == GIN_PHOENIX_MALFORMED) code = GIN_PHOENIX_INVALID;
    if (code != GIN_PHOENIX_OK) goto done;
    root = json_array();
    if (!root) { code = GIN_PHOENIX_NOMEM; goto done; }
    for (i = 0; i < 5; i++) {
        if (json_array_append(root, values[i])) { code = GIN_PHOENIX_NOMEM; goto done; }
    }
    code = dump_json(root, out);
done:
    for (i = 0; i < 5; i++) json_decref(values[i]);
    json_decref(root);
    return code;
}

enum gin_phoenix_code gin_phoenix_decode(const unsigned char *data, size_t length,
    struct gin_phoenix_frame **out)
{
    struct gin_phoenix_frame *frame = NULL;
    json_t *root = NULL, *values[5];
    enum gin_phoenix_code code;
    size_t i;
    if (!out || *out || (!data && length)) return GIN_PHOENIX_INVALID;
    code = load_json(data, length, GIN_PHOENIX_DEPTH_MAX, &root);
    if (code != GIN_PHOENIX_OK) return code;
    code = GIN_PHOENIX_MALFORMED;
    if (!json_is_array(root) || json_array_size(root) < 5) goto done;
    for (i = 0; i < 5; i++) values[i] = json_array_get(root, i);
    code = metadata(values[0], GIN_PHOENIX_REF_MAX, 1, 1);
    if (code == GIN_PHOENIX_OK) code = metadata(values[1], GIN_PHOENIX_REF_MAX, 1, 1);
    if (code == GIN_PHOENIX_OK) code = metadata(values[2], GIN_PHOENIX_TOPIC_MAX, 0, 0);
    if (code == GIN_PHOENIX_OK) code = metadata(values[3], GIN_PHOENIX_EVENT_MAX, 0, 0);
    if (code != GIN_PHOENIX_OK) goto done;
    if (!json_is_object(values[4])) { code = GIN_PHOENIX_MALFORMED; goto done; }
    code = reply_schema(values[3], values[4]);
    if (code != GIN_PHOENIX_OK) goto done;
    frame = calloc(1, sizeof(*frame));
    if (!frame) { code = GIN_PHOENIX_NOMEM; goto done; }
    frame->owned = sizeof(*frame);
    code = copy_string(frame, values[0], &frame->fields.join_ref);
    if (code == GIN_PHOENIX_OK) code = copy_string(frame, values[1], &frame->fields.ref);
    if (code == GIN_PHOENIX_OK) code = copy_string(frame, values[2], &frame->fields.topic);
    if (code == GIN_PHOENIX_OK) code = copy_string(frame, values[3], &frame->fields.event);
    if (code == GIN_PHOENIX_OK) code = copy_view(frame, values[4], &frame->fields.payload);
    if (code != GIN_PHOENIX_OK) goto done;
    if (!strcmp(frame->fields.event, "phx_reply")) {
        json_t *status = json_object_get(values[4], "status");
        const char *text = json_string_value(status);
        frame->has_reply = 1;
        frame->reply.status = !strcmp(text, "ok") ? GIN_PHOENIX_REPLY_OK :
            !strcmp(text, "error") ? GIN_PHOENIX_REPLY_ERROR : GIN_PHOENIX_REPLY_UNKNOWN;
        code = copy_string(frame, status, &frame->reply.status_text);
        if (code == GIN_PHOENIX_OK)
            code = copy_view(frame, json_object_get(values[4], "response"), &frame->reply.response);
        if (code != GIN_PHOENIX_OK) goto done;
    }
    json_decref(root); root = NULL;
    *out = frame; frame = NULL;
done:
    gin_phoenix_frame_free(frame); json_decref(root);
    return code;
}
