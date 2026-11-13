#include "topology_internal.h"
#include "json_evidence.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

#if JANSSON_VERSION_HEX < 0x020e00
#error "gin requires Jansson >= 2.14"
#endif

struct gin_topology {
    struct gin_topology_network *networks;
    struct gin_topology_window *windows;
    size_t network_count, window_count, owned;
    char chantypes[5];
    int known;
};

const struct gin_topology_network *gin_topology_network_at(const struct gin_topology *t, size_t i)
{ return t && i < t->network_count ? &t->networks[i] : NULL; }
const struct gin_topology_window *gin_topology_window_at(const struct gin_topology *t, size_t i)
{ return t && i < t->window_count ? &t->windows[i] : NULL; }
size_t gin_topology_network_count(const struct gin_topology *t) { return t ? t->network_count : 0; }
size_t gin_topology_window_count(const struct gin_topology *t) { return t ? t->window_count : 0; }
int gin_topology_queries_known(const struct gin_topology *t) { return t ? t->known : 0; }

void gin_topology_free(struct gin_topology *t)
{
    size_t i;
    if (!t) return;
    for (i = 0; i < t->network_count; i++) {
        free((void *)t->networks[i].slug);
        free((void *)t->networks[i].own_nick);
        free((void *)t->networks[i].component);
    }
    for (i = 0; i < t->window_count; i++) free((void *)t->windows[i].opened_at);
    free(t->networks);
    free(t->windows);
    free(t);
}

static enum gin_topology_code owned_alloc(struct gin_topology *t, size_t count,
    size_t size, void **out)
{
    size_t bytes;
    if (!count) { *out = NULL; return GIN_TOPOLOGY_OK; }
    if (size > (GIN_TOPOLOGY_OWNED_MAX - t->owned) / count) return GIN_TOPOLOGY_LIMIT;
    bytes = count * size;
    *out = calloc(count, size);
    if (!*out) return GIN_TOPOLOGY_NOMEM;
    t->owned += bytes;
    return GIN_TOPOLOGY_OK;
}

static enum gin_topology_code copy_string(struct gin_topology *t, const char *s, const char **out)
{
    void *p = NULL;
    enum gin_topology_code code = owned_alloc(t, strlen(s) + 1, 1, &p);
    if (code == GIN_TOPOLOGY_OK) { memcpy(p, s, strlen(s) + 1); *out = p; }
    return code;
}

static enum gin_topology_code create(const char *chantypes, size_t networks,
    size_t windows, struct gin_topology **out)
{
    struct gin_topology *t;
    void *p = NULL;
    enum gin_topology_code code;
    if (networks > GIN_TOPOLOGY_NETWORK_MAX || windows > GIN_TOPOLOGY_WINDOW_MAX)
        return GIN_TOPOLOGY_LIMIT;
    t = calloc(1, sizeof(*t));
    if (!t) return GIN_TOPOLOGY_NOMEM;
    t->owned = sizeof(*t);
    memcpy(t->chantypes, chantypes, strlen(chantypes) + 1);
    code = owned_alloc(t, networks, sizeof(*t->networks), &p);
    t->networks = p;
    if (code == GIN_TOPOLOGY_OK) {
        t->network_count = networks;
        code = owned_alloc(t, windows, sizeof(*t->windows), &p);
        if (code == GIN_TOPOLOGY_OK) { t->windows = p; t->window_count = windows; }
    }
    if (code != GIN_TOPOLOGY_OK) { gin_topology_free(t); return code; }
    *out = t;
    return GIN_TOPOLOGY_OK;
}

static int network_cmp(const void *a, const void *b)
{
    const struct gin_topology_network *x = a, *y = b;
    return (x->id > y->id) - (x->id < y->id);
}
static int window_cmp(const void *a, const void *b)
{
    const struct gin_topology_window *x = a, *y = b;
    int order;
    if (x->network_id != y->network_id) return (x->network_id > y->network_id) ? 1 : -1;
    order = strcmp(x->name.canonical_target, y->name.canonical_target);
    return order ? order : (int)x->role - (int)y->role;
}
static void sort_snapshot(struct gin_topology *t)
{
    if (t->network_count > 1) qsort(t->networks, t->network_count, sizeof(*t->networks), network_cmp);
    if (t->window_count > 1) qsort(t->windows, t->window_count, sizeof(*t->windows), window_cmp);
}
static enum gin_topology_code conflicts(const struct gin_topology *t)
{
    size_t i, j;
    for (i = 0; i < t->network_count; i++)
        for (j = 0; j < i; j++)
            if (t->networks[i].id == t->networks[j].id ||
                !strcmp(t->networks[i].slug, t->networks[j].slug)) return GIN_TOPOLOGY_CONFLICT;
    for (i = 0; i < t->window_count; i++)
        for (j = 0; j < i; j++)
            if (t->windows[i].network_id == t->windows[j].network_id &&
                (!strcmp(t->windows[i].name.canonical_target, t->windows[j].name.canonical_target) ||
                 !strcmp(t->windows[i].name.window_component, t->windows[j].name.window_component)))
                return GIN_TOPOLOGY_CONFLICT;
    return GIN_TOPOLOGY_OK;
}

static const char *text(json_t *obj, const char *key)
{ return json_string_value(json_object_get(obj, key)); }
static int positive_id(json_t *value, int64_t *id)
{
    json_int_t n;
    if (!json_is_integer(value)) return 0;
    n = json_integer_value(value);
    if (n <= 0 || (uintmax_t)n > INT64_MAX) return 0;
    *id = (int64_t)n;
    return 1;
}
static int valid_nick(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    if (!s || !*s || strlen(s) > 256) return 0;
    for (; *p; p++) {
        if (*p <= 32 || *p == 127 || *p == '/') return 0;
        if (*p == 0xc2 && p[1] >= 0x80 && p[1] <= 0x9f) return 0;
    }
    return 1; /* UTF-8 was already checked by the whole-document decoder. */
}
static enum gin_topology_code network_copy(struct gin_topology *t, size_t i,
    int64_t id, const char *slug, const char *nick)
{
    char component[256];
    enum gin_topology_code code;
    struct gin_topology_network *net = &t->networks[i];
    if (!slug || !valid_nick(nick) || gin_network_encode(slug, component, sizeof(component)))
        return GIN_TOPOLOGY_MALFORMED;
    net->id = id;
    code = copy_string(t, slug, &net->slug);
    if (code == GIN_TOPOLOGY_OK) code = copy_string(t, nick, &net->own_nick);
    if (code == GIN_TOPOLOGY_OK) code = copy_string(t, component, &net->component);
    return code;
}

/* Bound nesting before entering the recursive JSON decoder, even for ignored
 * additions and heads. Jansson then validates matching delimiters and UTF-8. */
static enum gin_topology_code load_json(const unsigned char *b, size_t n, json_t **out)
{
    size_t i, depth = 0;
    int quoted = 0, escaped = 0;
    json_error_t error;
    if (n > GIN_TOPOLOGY_BODY_MAX) return GIN_TOPOLOGY_LIMIT;
    if (n == 0) return GIN_TOPOLOGY_MALFORMED;
    for (i = 0; i < n; i++) {
        unsigned char c = b[i];
        if (!c) return GIN_TOPOLOGY_MALFORMED;
        if (quoted) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') quoted = 0;
        } else if (c == '"') quoted = 1;
        else if (c == '{' || c == '[') {
            if (++depth > GIN_TOPOLOGY_DEPTH_MAX) return GIN_TOPOLOGY_LIMIT;
        } else if (c == '}' || c == ']') {
            if (!depth) return GIN_TOPOLOGY_MALFORMED;
            depth--;
        }
    }
    memset(&error, 0, sizeof(error));
    *out = json_loadb((const char *)b, n, JSON_REJECT_DUPLICATES, &error);
    /* Some supported Jansson versions return NULL without setting an error
     * when the initial lexer allocation fails. Ordinary syntax failures have
     * an error message; preserve the library's explicit OOM category too. */
    if (!*out) return json_error_code(&error) == json_error_out_of_memory || !error.text[0] ?
        GIN_TOPOLOGY_NOMEM : GIN_TOPOLOGY_MALFORMED;
    if (!json_is_object(*out) || !gin_json_evidence(b, n, *out)) {
        json_decref(*out); *out = NULL;
        return GIN_TOPOLOGY_MALFORMED;
    }
    return GIN_TOPOLOGY_OK;
}

enum gin_topology_code gin_topology_parse_boot(const unsigned char *b, size_t n,
    const char *c, struct gin_topology **out)
{
    json_t *root = NULL, *nets, *channels, *rows, *row;
    struct gin_topology *t = NULL;
    enum gin_topology_code code;
    size_t i, j, count = 0, used = 0;
    const char *key;
    if (!out || *out || !b || !c || gin_chantypes_validate(c)) return GIN_TOPOLOGY_INVALID;
    code = load_json(b, n, &root);
    if (code != GIN_TOPOLOGY_OK) return code;
    nets = json_object_get(root, "networks");
    channels = json_object_get(root, "channels");
    code = GIN_TOPOLOGY_MALFORMED;
    if (!json_is_array(nets) || !json_is_object(channels) ||
        !json_is_object(json_object_get(root, "heads"))) goto done;
    json_object_foreach(channels, key, rows) {
        (void)key;
        if (!json_is_array(rows)) goto done;
        if (json_array_size(rows) > GIN_TOPOLOGY_WINDOW_MAX - count) {
            code = GIN_TOPOLOGY_LIMIT; goto done;
        }
        count += json_array_size(rows);
    }
    code = create(c, json_array_size(nets), count, &t);
    if (code != GIN_TOPOLOGY_OK) goto done;
    json_array_foreach(nets, i, row) {
        int64_t id;
        size_t k;
        code = GIN_TOPOLOGY_MALFORMED;
        if (!positive_id(json_object_get(row, "id"), &id)) goto done;
        code = network_copy(t, i, id, text(row, "slug"), text(row, "nick"));
        if (code != GIN_TOPOLOGY_OK) goto done;
        for (k = 0; k < i; k++) {
            if (t->networks[k].id == id || !strcmp(t->networks[k].slug, t->networks[i].slug)) {
                code = GIN_TOPOLOGY_CONFLICT; goto done;
            }
        }
        rows = json_object_get(channels, t->networks[i].slug);
        code = GIN_TOPOLOGY_MALFORMED;
        if (!json_is_array(rows)) goto done;
        json_array_foreach(rows, j, row) {
            struct gin_topology_window *w;
            const char *name = text(row, "name"), *source = text(row, "source");
            json_t *joined = json_object_get(row, "joined");
            if (used >= count) goto done;
            w = &t->windows[used++];
            if (!name || !source || !json_is_boolean(joined) ||
                gin_name_prepare(t->networks[i].slug, name, c, &w->name)) goto done;
            if (!strcmp(source, "autojoin")) w->source = GIN_CHANNEL_AUTOJOIN;
            else if (!strcmp(source, "joined")) w->source = GIN_CHANNEL_JOINED;
            else goto done;
            w->network_id = id;
            w->role = GIN_REMOTE_CHANNEL;
            w->joined = json_is_true(joined);
        }
    }
    if (used != count || json_object_size(channels) != t->network_count) {
        code = GIN_TOPOLOGY_MALFORMED; goto done;
    }
    code = conflicts(t);
    if (code != GIN_TOPOLOGY_OK) goto done;
    sort_snapshot(t);
    *out = t; t = NULL;
    code = GIN_TOPOLOGY_OK;
done:
    gin_topology_free(t);
    json_decref(root);
    return code;
}

/* Numeric object keys are canonical positive decimal int64, not strtol's
 * permissive sign/whitespace grammar. Check multiplication before every step. */
static int group_id(const char *s, int64_t *out)
{
    int64_t id = 0;
    if (*s < '1' || *s > '9') return 0;
    for (; *s; s++) {
        int digit = *s - '0';
        if (digit < 0 || digit > 9 || id > (INT64_MAX - digit) / 10) return 0;
        id = id * 10 + digit;
    }
    *out = id;
    return 1;
}
static const struct gin_topology_network *network_by_id(const struct gin_topology *t, int64_t id)
{
    size_t i;
    for (i = 0; i < t->network_count; i++) if (t->networks[i].id == id) return &t->networks[i];
    return NULL;
}
enum gin_topology_code gin_topology_apply_queries(const struct gin_topology *base,
    const unsigned char *b, size_t n, struct gin_topology **out)
{
    struct gin_topology *t = NULL;
    json_t *root = NULL, *groups, *rows, *row;
    const char *key, *kind;
    size_t count = 0, used = 0, i;
    enum gin_topology_code code;
    if (!out || *out || !base || !b) return GIN_TOPOLOGY_INVALID;
    code = load_json(b, n, &root);
    if (code != GIN_TOPOLOGY_OK) return code;
    groups = json_object_get(root, "windows");
    kind = text(root, "kind");
    code = GIN_TOPOLOGY_MALFORMED;
    if (!kind || strcmp(kind, "query_windows_list") || !json_is_object(groups)) goto done;
    for (i = 0; i < base->window_count; i++) if (base->windows[i].role == GIN_REMOTE_CHANNEL) count++;
    json_object_foreach(groups, key, rows) {
        int64_t id;
        if (!group_id(key, &id) || !network_by_id(base, id) || !json_is_array(rows)) goto done;
        if (json_array_size(rows) > GIN_TOPOLOGY_WINDOW_MAX - count) {
            code = GIN_TOPOLOGY_LIMIT; goto done;
        }
        count += json_array_size(rows);
    }
    code = create(base->chantypes, base->network_count, count, &t);
    if (code != GIN_TOPOLOGY_OK) goto done;
    for (i = 0; i < base->network_count; i++) {
        const struct gin_topology_network *net = &base->networks[i];
        code = network_copy(t, i, net->id, net->slug, net->own_nick);
        if (code != GIN_TOPOLOGY_OK) goto done;
    }
    for (i = 0; i < base->window_count; i++)
        if (base->windows[i].role == GIN_REMOTE_CHANNEL) t->windows[used++] = base->windows[i];
    json_object_foreach(groups, key, rows) {
        int64_t id = 0;
        const struct gin_topology_network *net;
        (void)group_id(key, &id); /* checked above, root is immutable */
        net = network_by_id(base, id);
        json_array_foreach(rows, i, row) {
            int64_t row_id;
            const char *target = text(row, "target_nick"), *opened = text(row, "opened_at");
            struct gin_topology_window *w = &t->windows[used++];
            code = GIN_TOPOLOGY_MALFORMED;
            if (!positive_id(json_object_get(row, "network_id"), &row_id) || row_id != id ||
                !target || !opened || !*opened || strlen(opened) > GIN_TOPOLOGY_OPENED_AT_MAX ||
                gin_name_prepare(net->slug, target, t->chantypes, &w->name)) goto done;
            w->network_id = id;
            w->role = GIN_REMOTE_QUERY;
            code = copy_string(t, opened, &w->opened_at);
            if (code != GIN_TOPOLOGY_OK) goto done;
        }
    }
    code = conflicts(t);
    if (code != GIN_TOPOLOGY_OK) goto done;
    sort_snapshot(t);
    t->known = 1;
    *out = t; t = NULL;
    code = GIN_TOPOLOGY_OK;
done:
    gin_topology_free(t);
    json_decref(root);
    return code;
}

enum gin_topology_code gin_topology_http_status(long status, const unsigned char *b, size_t n)
{
    json_t *root = NULL;
    enum gin_topology_code result = GIN_TOPOLOGY_FORBIDDEN;
    const char *error;
    if (status == 401) return GIN_TOPOLOGY_UNAUTHORIZED;
    if (status == 429) return GIN_TOPOLOGY_THROTTLED;
    if (status != 403) return GIN_TOPOLOGY_REMOTE;
    if (!b || load_json(b, n, &root) != GIN_TOPOLOGY_OK) return result;
    error = text(root, "error");
    if (error && !strcmp(error, "client_token_scope")) result = GIN_TOPOLOGY_SCOPE;
    json_decref(root);
    return result;
}

const char *gin_topology_error(enum gin_topology_code code)
{
    switch (code) {
    case GIN_TOPOLOGY_OK: return "ok";
    case GIN_TOPOLOGY_INVALID: return "invalid-settings";
    case GIN_TOPOLOGY_MALFORMED: return "malformed-topology";
    case GIN_TOPOLOGY_CONFLICT: return "topology-conflict";
    case GIN_TOPOLOGY_LIMIT: return "limit";
    case GIN_TOPOLOGY_NOMEM: return "out-of-memory";
    case GIN_TOPOLOGY_WRONG_SERVER: return "unexpected-server";
    case GIN_TOPOLOGY_CLIENT_TOO_OLD: return "client-protocol-too-old";
    case GIN_TOPOLOGY_SERVER_TOO_OLD: return "server-protocol-too-old";
    case GIN_TOPOLOGY_UNAUTHORIZED: return "unauthorized";
    case GIN_TOPOLOGY_SCOPE: return "client-token-scope";
    case GIN_TOPOLOGY_FORBIDDEN: return "forbidden";
    case GIN_TOPOLOGY_THROTTLED: return "throttled";
    case GIN_TOPOLOGY_SUBJECT: return "unsupported-subject";
    case GIN_TOPOLOGY_TLS: return "tls";
    case GIN_TOPOLOGY_TIMEOUT: return "timeout";
    case GIN_TOPOLOGY_TRANSPORT: return "transport";
    case GIN_TOPOLOGY_REMOTE: return "remote-status";
    default: return "unknown-topology-error";
    }
}
