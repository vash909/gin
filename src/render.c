#include "gin_render.h"
#include "json_evidence.h"
#include "gin_naming.h"
#include "render_values.h"
#include "connect_values.h"
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

struct gin_render_record { size_t length; unsigned char data[]; };
struct builder { char data[GIN_RENDER_RECORD_MAX + 1u]; size_t length; };

static int equal_fold(const char *a, const char *b)
{
    while (*a && *b) {
        unsigned char x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x = (unsigned char)(x + ('a' - 'A'));
        if (y >= 'A' && y <= 'Z') y = (unsigned char)(y + ('a' - 'A'));
        if (x != y) return 0;
    }
    return *a == *b;
}
static enum gin_render_code literal(struct builder *b, const char *s)
{
    size_t n = strlen(s);
    if (n > GIN_RENDER_RECORD_MAX - b->length) return GIN_RENDER_LIMIT;
    memcpy(b->data + b->length, s, n); b->length += n;
    b->data[b->length] = 0;
    return GIN_RENDER_OK;
}
static enum gin_render_code quoted(struct builder *b, const char *key,
    const char *s, size_t n, size_t max)
{
    size_t wrote;
    enum gin_render_value_code v;
    enum gin_render_code c;
    if (n > max) return GIN_RENDER_LIMIT;
    c = literal(b, key); if (c != GIN_RENDER_OK) return c;
    c = literal(b, "=\""); if (c != GIN_RENDER_OK) return c;
    v = gin_render_escape((const unsigned char *)s, n, b->data + b->length,
        sizeof(b->data) - b->length, &wrote);
    if (v != GIN_RENDER_VALUE_OK)
        return v == GIN_RENDER_VALUE_LIMIT ? GIN_RENDER_LIMIT : GIN_RENDER_MALFORMED;
    b->length += wrote;
    return literal(b, "\"");
}
static const char *text(json_t *o, const char *k)
{ return json_string_value(json_object_get(o, k)); }
static int e2e_body(const char *s, size_t n)
{
    size_t i;
    /* Refuse reserved verb prefixes even if the CTCP delimiter/arguments
     * are malformed. Do not require the strict E2E payload parser to accept. */
    for (i = 0; i + 3u < n; i++)
        if ((unsigned char)s[i] == 1 && !memcmp(s + i + 1u, "E2E", 3)) return 1;
    return 0;
}
static enum gin_render_code route(json_t *o, const struct gin_render_context *c,
    int message)
{
    const char *network = text(o, "network"), *channel = text(o, "channel");
    json_t *dm = json_object_get(o, "dm_with");
    if (!network || !channel) return GIN_RENDER_MALFORMED;
    if (strcmp(network, c->network)) return GIN_RENDER_ROUTE_MISMATCH;
    if (!equal_fold(channel, c->source_target ? c->source_target : c->target))
        return GIN_RENDER_ROUTE_MISMATCH;
    if (!message) {
        return equal_fold(channel, c->target) ? GIN_RENDER_OK : GIN_RENDER_ROUTE_MISMATCH;
    }
    if (!c->source_target) return GIN_RENDER_ROUTE_MISMATCH;
    if (c->role == GIN_RENDER_QUERY) {
        if (dm && !json_is_null(dm) && !json_is_string(dm)) return GIN_RENDER_MALFORMED;
        return json_is_string(dm) && equal_fold(json_string_value(dm), c->target)
            ? GIN_RENDER_OK : GIN_RENDER_ROUTE_MISMATCH;
    }
    if (dm && !json_is_null(dm))
        return json_is_string(dm) ? GIN_RENDER_ROUTE_MISMATCH : GIN_RENDER_MALFORMED;
    return equal_fold(channel, c->target) ? GIN_RENDER_OK : GIN_RENDER_ROUTE_MISMATCH;
}
static enum gin_render_code detail(struct builder *b, json_t *o,
    const char *key, const char *label)
{
    json_t *v = json_object_get(o, key);
    if (!v || json_is_null(v)) return GIN_RENDER_OK;
    if (!json_is_string(v)) return GIN_RENDER_MALFORMED;
    return quoted(b, label, json_string_value(v), json_string_length(v), GIN_RENDER_DETAIL_MAX);
}
static enum gin_render_code project(json_t *o, const struct gin_render_context *c,
    int64_t receive_ms, struct builder *b)
{
    const char *kind = text(o, "kind"), *label, *state = NULL;
    const char *body, *sender;
    json_t *row, *v;
    int64_t ms = receive_ms;
    int message;
    size_t n;
    char time[GIN_RENDER_TIME_CAP], numeric[32];
    enum gin_render_code code;
    enum gin_render_value_code vc;
    if (!kind) return GIN_RENDER_MALFORMED;
    if (!strncmp(kind, "e2e_", 4)) return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
    message = !strcmp(kind, "message");
    row = o; label = kind;
    if (message) {
        row = json_object_get(o, "message");
        if (!json_is_object(row) || !(label = text(row, "kind"))) return GIN_RENDER_MALFORMED;
        if (!strncmp(label, "e2e_", 4)) return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
        if (strcmp(label, "privmsg") && strcmp(label, "notice") && strcmp(label, "action"))
            return GIN_RENDER_IGNORE;
    } else if (!strcmp(kind, "joined")) state = "joined";
    else if (!strcmp(kind, "window_pending")) state = "pending";
    else if (!strcmp(kind, "window_invited")) state = "invited";
    else if (!strcmp(kind, "join_failed")) state = "failed";
    else if (!strcmp(kind, "kicked")) state = "kicked";
    else if (strcmp(kind, "window_invite_declined")) return GIN_RENDER_IGNORE;
    code = route(row, c, message); if (code != GIN_RENDER_OK) return code;
    if (message) {
        if (c->encryption != GIN_RENDER_MODE_PLAIN) return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
        v = json_object_get(row, "server_time");
        if (!json_is_integer(v)) return GIN_RENDER_MALFORMED;
        ms = (int64_t)json_integer_value(v);
    } else if (state && (!text(row, "state") || strcmp(text(row, "state"), state)))
        return GIN_RENDER_MALFORMED;
    vc = gin_render_time(ms, time);
    if (vc != GIN_RENDER_VALUE_OK) return GIN_RENDER_RANGE;
#define ADD(expr) do { code = (expr); if (code != GIN_RENDER_OK) return code; } while (0)
    ADD(literal(b, "[")); ADD(literal(b, time));
    ADD(literal(b, message ? "] " : " recv] ")); ADD(literal(b, label));
    if (message) {
        sender = text(row, "sender"); v = json_object_get(row, "body");
        if (!sender || !v || (!json_is_string(v) && !json_is_null(v))) return GIN_RENDER_MALFORMED;
        body = json_is_null(v) ? "" : json_string_value(v);
        n = json_is_null(v) ? 0 : json_string_length(v);
        if (n > GIN_RENDER_BODY_MAX) return GIN_RENDER_LIMIT;
        if (e2e_body(body, n)) return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
        if (!strcmp(label, "action") && n >= 8u && !memcmp(body, "\001ACTION ", 8)) {
            body += 8; n -= 8;
            if (n && body[n - 1u] == '\001') n--;
        }
        ADD(quoted(b, " sender", sender, json_string_length(json_object_get(row, "sender")), GIN_RENDER_SENDER_MAX));
        ADD(quoted(b, " body", body, n, GIN_RENDER_BODY_MAX));
    } else {
        ADD(quoted(b, " target", c->target, strlen(c->target), 256u));
        if (state) ADD(quoted(b, " state", state, strlen(state), 32u));
        if (!strcmp(kind, "window_invited")) ADD(detail(b, row, "inviter", " inviter"));
        if (!strcmp(kind, "kicked")) ADD(detail(b, row, "by", " by"));
        if (!strcmp(kind, "kicked") || !strcmp(kind, "join_failed"))
            ADD(detail(b, row, "reason", " reason"));
        if (!strcmp(kind, "join_failed")) {
            v = json_object_get(row, "numeric");
            if (v && !json_is_null(v)) {
                if (!json_is_integer(v) || json_integer_value(v) <= 0) return GIN_RENDER_MALFORMED;
                (void)snprintf(numeric, sizeof(numeric), "%" JSON_INTEGER_FORMAT, json_integer_value(v));
                ADD(literal(b, " numeric=")); ADD(literal(b, numeric));
            }
        }
    }
    ADD(literal(b, "\n"));
#undef ADD
    return GIN_RENDER_OK;
}
enum gin_render_code gin_render_event(const struct gin_phoenix_frame *frame,
    const struct gin_render_context *c, uint64_t observed_epoch,
    int64_t receive_ms, struct gin_render_record **out)
{
    const struct gin_phoenix_fields *f = gin_phoenix_frame_fields(frame);
    struct gin_name name;
    struct gin_render_record *record;
    struct builder b;
    char topic[GIN_PHOENIX_TOPIC_MAX + 1u]; size_t length;
    json_t *o; json_error_t error = {0};
    enum gin_render_code code;
    if (!f || !c || !out || *out || !c->subject || !c->network || !c->target ||
        !c->chantypes || !c->join_ref || !*c->join_ref || !c->socket_epoch ||
        c->role < GIN_RENDER_CHANNEL || c->role > GIN_RENDER_STATUS ||
        c->encryption < GIN_RENDER_MODE_UNKNOWN || c->encryption > GIN_RENDER_MODE_ENCRYPTED)
        return GIN_RENDER_INVALID;
    if (gin_name_prepare(c->network, c->target, c->chantypes, &name) < 0 ||
        (c->role == GIN_RENDER_CHANNEL && name.kind != GIN_WINDOW_CHANNEL) ||
        (c->role == GIN_RENDER_QUERY && (name.kind != GIN_WINDOW_NONCHANNEL || !strcmp(c->target, "$server"))) ||
        (c->role == GIN_RENDER_STATUS && strcmp(c->target, "$server"))) return GIN_RENDER_INVALID;
    if (gin_ws_topic(c->subject, c->source_target ? c->network : NULL,
        c->source_target, topic, sizeof(topic), &length) != GIN_WS_VALUE_OK) return GIN_RENDER_INVALID;
    if (observed_epoch != c->socket_epoch || strcmp(f->topic, topic) ||
        (f->join_ref && strcmp(f->join_ref, c->join_ref))) return GIN_RENDER_ROUTE_MISMATCH;
    if (strcmp(f->event, "event")) return GIN_RENDER_IGNORE;
    o = json_loadb((const char *)f->payload.data, f->payload.length, JSON_REJECT_DUPLICATES, &error);
    if (!o) return GIN_RENDER_MALFORMED;
    if (!gin_json_evidence(f->payload.data, f->payload.length, o)) {
        json_decref(o); return GIN_RENDER_MALFORMED;
    }
    memset(&b, 0, sizeof(b)); code = project(o, c, receive_ms, &b); json_decref(o);
    if (code != GIN_RENDER_OK) return code;
    record = malloc(sizeof(*record) + b.length + 1u);
    if (!record) return GIN_RENDER_NOMEM;
    record->length = b.length; memcpy(record->data, b.data, b.length + 1u);
    *out = record; return GIN_RENDER_OK;
}
enum gin_render_code gin_render_plain_message(const char *kind,
    const unsigned char *sender, size_t sender_length,
    const unsigned char *body, size_t body_length, int64_t server_ms,
    enum gin_render_encryption mode, struct gin_render_record **out)
{
    struct builder b;
    struct gin_render_record *record;
    char time[GIN_RENDER_TIME_CAP];
    enum gin_render_code code;
    if (!kind || !out || *out || (!sender && sender_length) ||
        (!body && body_length) || mode < GIN_RENDER_MODE_UNKNOWN ||
        mode > GIN_RENDER_MODE_ENCRYPTED) return GIN_RENDER_INVALID;
    if (mode != GIN_RENDER_MODE_PLAIN || !strncmp(kind, "e2e_", 4))
        return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
    if (sender_length > GIN_RENDER_SENDER_MAX || body_length > GIN_RENDER_BODY_MAX)
        return GIN_RENDER_LIMIT;
    if (e2e_body((const char *)body, body_length)) return GIN_RENDER_UNSUPPORTED_ENCRYPTION;
    if (strcmp(kind, "privmsg") && strcmp(kind, "notice") && strcmp(kind, "action"))
        return GIN_RENDER_IGNORE;
    if (gin_render_time(server_ms, time) != GIN_RENDER_VALUE_OK) return GIN_RENDER_RANGE;
    if (!strcmp(kind, "action") && body_length >= 8u && !memcmp(body, "\001ACTION ", 8)) {
        body += 8; body_length -= 8;
        if (body_length && body[body_length - 1u] == '\001') body_length--;
    }
    memset(&b, 0, sizeof(b));
#define ADD_PLAIN(expr) do { code = (expr); if (code != GIN_RENDER_OK) return code; } while (0)
    ADD_PLAIN(literal(&b, "[")); ADD_PLAIN(literal(&b, time));
    ADD_PLAIN(literal(&b, "] ")); ADD_PLAIN(literal(&b, kind));
    ADD_PLAIN(quoted(&b, " sender", (const char *)sender, sender_length, GIN_RENDER_SENDER_MAX));
    ADD_PLAIN(quoted(&b, " body", (const char *)body, body_length, GIN_RENDER_BODY_MAX));
    ADD_PLAIN(literal(&b, "\n"));
#undef ADD_PLAIN
    record = malloc(sizeof(*record) + b.length + 1u);
    if (!record) return GIN_RENDER_NOMEM;
    record->length = b.length;
    memcpy(record->data, b.data, b.length + 1u);
    *out = record;
    return GIN_RENDER_OK;
}
struct gin_phoenix_view gin_render_record_bytes(const struct gin_render_record *record)
{
    struct gin_phoenix_view b = {NULL, 0};
    if (record) { b.data = record->data; b.length = record->length; } return b;
}
void gin_render_record_free(struct gin_render_record *record) { free(record); }
enum gin_render_code gin_render_append(int fd, const struct gin_render_record *record,
    size_t *appended)
{
    struct stat st; int flags; size_t done = 0; unsigned attempts;
    if (appended) *appended = 0;
    if (!appended || !record) { errno = EINVAL; return GIN_RENDER_INVALID; }
    if (fstat(fd, &st) < 0 || (flags = fcntl(fd, F_GETFL)) < 0) return GIN_RENDER_IO;
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 07777) != 0600 || !(flags & O_APPEND) ||
        (flags & O_ACCMODE) == O_RDONLY) { errno = EINVAL; return GIN_RENDER_IO; }
    for (attempts = 0; done < record->length && attempts < GIN_RENDER_WRITE_OPERATIONS_MAX; attempts++) {
        ssize_t n = write(fd, record->data + done, record->length - done);
        if (n > 0) { done += (size_t)n; *appended = done; }
        else if (n == 0) { errno = EIO; return GIN_RENDER_IO; }
        else if (errno != EINTR) return GIN_RENDER_IO;
    }
    if (done != record->length) { errno = EIO; return GIN_RENDER_IO; }
    return GIN_RENDER_OK;
}
const char *gin_render_error(enum gin_render_code code)
{
    switch (code) {
    case GIN_RENDER_OK: return "ok";
    case GIN_RENDER_IGNORE: return "ignored event";
    case GIN_RENDER_INVALID: return "invalid renderer arguments";
    case GIN_RENDER_MALFORMED: return "malformed event";
    case GIN_RENDER_LIMIT: return "renderer limit exceeded";
    case GIN_RENDER_NOMEM: return "renderer allocation failed";
    case GIN_RENDER_RANGE: return "timestamp outside presentation range";
    case GIN_RENDER_ROUTE_MISMATCH: return "event route mismatch";
    case GIN_RENDER_UNSUPPORTED_ENCRYPTION: return "unsupported encryption";
    case GIN_RENDER_IO: return "output append failed";
    }
    return "unknown renderer error";
}
