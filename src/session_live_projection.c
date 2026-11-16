#include "session_live_projection.h"
#include "history_evidence.h"
#include "history_values.h"
#include <jansson.h>
#include <string.h>

int gin_live_receive_ms(const struct timespec *wall, int64_t *out)
{
    uint64_t seconds;
    int64_t millis, remainder;
    if (!wall || !out || wall->tv_sec < 0 || wall->tv_nsec < 0 ||
        wall->tv_nsec >= 1000000000L) return 0;
    seconds = (uint64_t)wall->tv_sec;
    if (seconds > (uint64_t)INT64_MAX / 1000u) return 0;
    millis = (int64_t)seconds * 1000;
    remainder = wall->tv_nsec / 1000000;
    if (millis > INT64_MAX - remainder) return 0;
    *out = millis + remainder;
    return 1;
}

static const char *text(json_t *o, const char *key)
{
    json_t *v = json_object_get(o, key);
    const char *s = json_string_value(v);
    return s && strlen(s) == json_string_length(v) && gin_history_utf8(s) ? s : NULL;
}
static int same(const char *a, const char *b)
{
    unsigned char x, y;
    if (!a || !b) return 0;
    do {
        x = (unsigned char)*a++; y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return 0;
    } while (x);
    return 1;
}
static enum gin_session_code render_code(enum gin_render_code c)
{
    if (c == GIN_RENDER_OK || c == GIN_RENDER_IGNORE) return GIN_SESSION_OK;
    if (c == GIN_RENDER_NOMEM) return GIN_SESSION_NOMEM;
    if (c == GIN_RENDER_LIMIT || c == GIN_RENDER_RANGE) return GIN_SESSION_LIMIT;
    if (c == GIN_RENDER_UNSUPPORTED_ENCRYPTION) return GIN_SESSION_UNSUPPORTED;
    if (c == GIN_RENDER_IO) return GIN_SESSION_IO;
    return GIN_SESSION_ROUTE;
}
static enum gin_session_code history_code(enum gin_history_code c)
{
    if (c == GIN_HISTORY_OK) return GIN_SESSION_OK;
    if (c == GIN_HISTORY_NOMEM) return GIN_SESSION_NOMEM;
    if (c == GIN_HISTORY_LIMIT) return GIN_SESSION_LIMIT;
    if (c == GIN_HISTORY_UNSUPPORTED) return GIN_SESSION_UNSUPPORTED;
    return GIN_SESSION_ROUTE;
}
static enum gin_session_code project(const struct gin_live_view *v,
    const struct gin_live_projection *c, const struct gin_topology *topology,
    struct gin_topology **next, struct gin_render_record **record, int *terminal,
    const struct gin_history_scope *scope, struct gin_history_page **history)
{
    json_error_t error;
    json_t *o = NULL, *row;
    const char *kind, *network, *channel, *peer;
    struct gin_name name;
    const struct gin_topology_network *net;
    const struct gin_topology_window *window;
    struct gin_render_context rc;
    struct gin_phoenix_bytes encoded = {NULL, 0};
    struct gin_phoenix_frame *frame = NULL;
    enum gin_phoenix_code pc;
    enum gin_session_code code = GIN_SESSION_ROUTE;
    int message;
    if (!v || !c || !topology || !next || *next || !record || *record || !terminal ||
        !c->settings || !c->subject || !c->own_nick || !v->fields.payload.data ||
        !v->fields.payload.length) return GIN_SESSION_INVALID;
    if (scope || history) {
        enum gin_history_code hc;
        if (!scope || !history || *history) return GIN_SESSION_INVALID;
        hc = gin_history_scope_check(scope);
        if (hc != GIN_HISTORY_OK)
            return hc == GIN_HISTORY_INVALID ? GIN_SESSION_INVALID : history_code(hc);
        /* The topic subject is a display label, NOT the credential UUID.
         * The owner binds subject_id through the checkpoint and bound /me;
         * this borrowed projection validates its separate transport identity. */
        if (strcmp(scope->origin, c->settings->origin) ||
            strcmp(scope->own_nick, c->own_nick) ||
            strcmp(scope->network_slug, c->settings->local.network) ||
            strcmp(scope->target, c->settings->local.target) ||
            strcmp(scope->chantypes, c->settings->local.chantypes) ||
            scope->network_id != c->network_id ||
            scope->socket_epoch != v->observed_epoch ||
            scope->role != (c->query ? GIN_RENDER_QUERY : GIN_RENDER_CHANNEL))
            return GIN_SESSION_ROUTE;
    }
    memset(&error, 0, sizeof(error));
    *terminal = 0;
    o = json_loadb((const char *)v->fields.payload.data, v->fields.payload.length,
        JSON_REJECT_DUPLICATES, &error);
    if (!o) return GIN_SESSION_NOMEM;
    if (!gin_history_evidence(v->fields.payload.data, v->fields.payload.length, o) ||
        !(kind = text(o, "kind"))) goto done;
    if (!strncmp(kind, "e2e_", 4)) { code = GIN_SESSION_UNSUPPORTED; goto done; }
    if (!strcmp(kind, "query_windows_list")) {
        enum gin_topology_code tc;
        if (v->source_index != 0) goto done;
        tc = gin_topology_apply_queries(topology, v->fields.payload.data,
            v->fields.payload.length, next);
        code = tc == GIN_TOPOLOGY_OK ? GIN_SESSION_OK :
            tc == GIN_TOPOLOGY_NOMEM ? GIN_SESSION_NOMEM : GIN_SESSION_TOPOLOGY;
        if (code == GIN_SESSION_OK && c->query) {
            if (gin_name_prepare(c->settings->local.network, c->settings->local.target,
                c->settings->local.chantypes, &name) < 0) { code = GIN_SESSION_ROUTE; goto done; }
            const struct gin_topology_window *previous;
            previous = gin_topology_find_window(topology, c->network_id, name.canonical_target);
            window = gin_topology_find_window(*next, c->network_id, name.canonical_target);
            if (previous && previous->role == GIN_REMOTE_QUERY &&
                (!window || window->role != GIN_REMOTE_QUERY)) code = GIN_SESSION_ROUTE;
        }
        goto done;
    }
    if (!strcmp(kind, "own_nick_changed")) {
        json_t *id = json_object_get(o, "network_id");
        const char *nick = text(o, "nick");
        if (v->source_index != 0 || !json_is_integer(id) || json_integer_value(id) <= 0 ||
            !nick || !*nick || strlen(nick) > 256) goto done;
        net = gin_topology_find_id(topology, (int64_t)json_integer_value(id));
        if (!net) goto done;
        code = net->id == c->network_id &&
            (scope ? strcmp(nick, scope->own_nick) != 0 :
                c->query && !same(nick, c->own_nick)) ?
            GIN_SESSION_ROUTE : GIN_SESSION_OK;
        goto done;
    }
    message = !strcmp(kind, "message");
    row = message ? json_object_get(o, "message") : o;
    if (message) {
        const char *nested = text(row, "kind");
        if (!nested) goto done;
        if (!strncmp(nested, "e2e_", 4)) { code = GIN_SESSION_UNSUPPORTED; goto done; }
        if (strcmp(nested, "privmsg") && strcmp(nested, "notice") && strcmp(nested, "action")) {
            code = GIN_SESSION_OK; goto done;
        }
    } else if (strcmp(kind, "joined") && strcmp(kind, "window_pending") &&
        strcmp(kind, "window_invited") && strcmp(kind, "window_invite_declined") &&
        strcmp(kind, "join_failed") && strcmp(kind, "kicked")) { code = GIN_SESSION_OK; goto done; }
    network = text(row, "network"); channel = text(row, "channel");
    if (!network || !channel || !(net = gin_topology_find_slug(topology, network))) goto done;
    if (net->id != c->network_id) { code = GIN_SESSION_OK; goto done; }
    if (message && (!c->source_target || !same(channel, c->source_target))) goto done;
    peer = c->query && message ? text(row, "dm_with") : channel;
    if (!peer || gin_name_prepare(network, peer, c->settings->local.chantypes, &name) < 0) goto done;
    window = gin_topology_find_window(topology, net->id, name.canonical_target);
    if (!window) goto done;
    if (!same(peer, c->settings->local.target)) { code = GIN_SESSION_OK; goto done; }
    if (c->query ? window->role != GIN_REMOTE_QUERY :
        window->role != GIN_REMOTE_CHANNEL || !window->joined) goto done;
    memset(&rc, 0, sizeof(rc));
    rc.subject = c->subject; rc.network = c->settings->local.network;
    rc.target = c->settings->local.target; rc.source_target = c->source_target;
    rc.chantypes = c->settings->local.chantypes; rc.join_ref = v->source_join_ref;
    rc.socket_epoch = v->observed_epoch; rc.role = c->query ? GIN_RENDER_QUERY : GIN_RENDER_CHANNEL;
    rc.encryption = GIN_RENDER_MODE_PLAIN; /* Explicit operator assertion only. */
    pc = gin_phoenix_encode(&v->fields, &encoded);
    if (pc == GIN_PHOENIX_OK) pc = gin_phoenix_decode(encoded.data, encoded.length, &frame);
    if (pc != GIN_PHOENIX_OK) { code = pc == GIN_PHOENIX_NOMEM ? GIN_SESSION_NOMEM : GIN_SESSION_ROUTE; goto done; }
    code = render_code(gin_render_event(frame, &rc, v->observed_epoch, v->receive_ms, record));
    /* The DOM was attested above; presence alone selects strict decoding. Never
     * extract an ID here or infer persisted progress from an ordinary record. */
    if (code == GIN_SESSION_OK && scope && message && *record &&
        json_object_get(row, "id")) {
        code = history_code(gin_history_live_decode(frame, &rc, scope,
            v->observed_epoch, history));
        if (code != GIN_SESSION_OK) {
            gin_render_record_free(*record); *record = NULL;
            gin_history_page_free(*history); *history = NULL;
        }
    }
    if (code == GIN_SESSION_OK && !message && strcmp(kind, "joined") &&
        strcmp(kind, "window_invite_declined")) *terminal = 1;
done:
    gin_phoenix_frame_free(frame); gin_phoenix_bytes_free(&encoded); json_decref(o);
    return code;
}

enum gin_session_code gin_live_project(const struct gin_live_view *v,
    const struct gin_live_projection *c, const struct gin_topology *topology,
    struct gin_topology **next, struct gin_render_record **record, int *terminal)
{
    return project(v, c, topology, next, record, terminal, NULL, NULL);
}

enum gin_session_code gin_live_project_history(const struct gin_live_view *v,
    const struct gin_live_projection *c, const struct gin_topology *topology,
    struct gin_topology **next, struct gin_render_record **record, int *terminal,
    const struct gin_history_scope *scope, struct gin_history_page **history)
{
    if (!scope || !history || *history) return GIN_SESSION_INVALID;
    return project(v, c, topology, next, record, terminal, scope, history);
}
