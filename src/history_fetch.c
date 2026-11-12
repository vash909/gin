#include "history_values.h"
#include "gin_naming.h"
#include <stdlib.h>
#include <string.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
static enum gin_history_code protocol_error(enum gin_protocol_code c)
{
    switch (c) {
    case GIN_PROTOCOL_TLS: return GIN_HISTORY_TLS;
    case GIN_PROTOCOL_TIMEOUT: return GIN_HISTORY_TIMEOUT;
    case GIN_PROTOCOL_LIMIT: return GIN_HISTORY_LIMIT;
    case GIN_PROTOCOL_TRANSPORT: return GIN_HISTORY_TRANSPORT;
    default: return GIN_HISTORY_PREFLIGHT;
    }
}
static enum gin_history_code auth_error(enum gin_auth_code c)
{
    switch (c) {
    case GIN_AUTH_TLS: return GIN_HISTORY_TLS;
    case GIN_AUTH_TIMEOUT: return GIN_HISTORY_TIMEOUT;
    case GIN_AUTH_LIMIT: return GIN_HISTORY_LIMIT;
    case GIN_AUTH_TRANSPORT: return GIN_HISTORY_TRANSPORT;
    default: return GIN_HISTORY_AUTH;
    }
}
/* Deliberately a known-topology subset: archived windows absent from the
 * supplied inventory cannot be authorized here. No path/name infers a peer. */
static int known_route(const struct gin_topology *t, const struct gin_history_scope *s)
{
    const struct gin_topology_network *n;
    const struct gin_topology_window *w;
    struct gin_name name;
    if (!t) return 0;
    n = gin_topology_find_id(t, s->network_id);
    if (!n || strcmp(n->slug, s->network_slug) || strcmp(n->own_nick, s->own_nick) ||
        gin_topology_find_slug(t, s->network_slug) != n) return 0;
    if (s->role == GIN_RENDER_STATUS) return !strcmp(s->target, "$server");
    if (gin_name_prepare(s->network_slug, s->target, s->chantypes, &name)) return 0;
    w = gin_topology_find_window(t, s->network_id, name.canonical_target);
    if (!w) return 0;
    if (s->role == GIN_RENDER_QUERY)
        return gin_topology_queries_known(t) && w->role == GIN_REMOTE_QUERY;
    return w->role == GIN_REMOTE_CHANNEL;
}
struct gin_history_fetch_result gin_history_fetch(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, const struct gin_credential *credential,
    const struct gin_topology *topology, const struct gin_history_scope *scope,
    const struct gin_history_request *request, struct gin_history_page **out)
{
    struct gin_history_fetch_result r = {GIN_HISTORY_INVALID, GIN_PROTOCOL_INVALID,
        GIN_AUTH_INVALID, 0, 0, 0};
    struct gin_protocol_policy effective;
    struct gin_protocol_info info;
    struct gin_credential verified = {0};
    struct gin_http_settings http;
    struct gin_http_request q;
    struct gin_http_result response;
    char path[GIN_HISTORY_PATH_MAX + 1];
    char url[GIN_CREDENTIAL_ORIGIN_MAX + GIN_HISTORY_PATH_MAX + 1];
    unsigned char *body = NULL;
    size_t written = 0, origin_length;
    /* No I/O, including anonymous discovery, until every local check passes. */
    if (!out || *out || !s || !policy || !topology ||
        policy->client_protocol <= 0 || policy->required_server_protocol <= 0 ||
        !gin_http_origin_valid(s->origin) || s->connect_ms <= 0 ||
        s->total_ms <= 0 || s->connect_ms > s->total_ms ||
        s->total_ms > GIN_HTTP_TIMEOUT_MAX ||
        gin_credential_validate(credential) != GIN_CREDENTIAL_OK) return r;
    if (gin_history_request_check(request) != GIN_HISTORY_OK) return r;
    r.code = gin_history_scope_check(scope);
    if (r.code != GIN_HISTORY_OK) return r;
    if (strcmp(s->origin, scope->origin) || strcmp(s->origin, credential->origin) ||
        strcmp(scope->subject_id, credential->subject_id)) {
        r.code = GIN_HISTORY_INVALID; return r;
    }
    if (!known_route(topology, scope)) { r.code = GIN_HISTORY_ROUTE; return r; }
    r.code = gin_history_path(scope, request, path, sizeof(path), &written);
    if (r.code != GIN_HISTORY_OK) return r;
    origin_length = strlen(s->origin);
    if (origin_length > GIN_CREDENTIAL_ORIGIN_MAX ||
        written > sizeof(url) - origin_length - 1) {
        r.code = GIN_HISTORY_LIMIT; return r;
    }
    memcpy(url, s->origin, origin_length);
    memcpy(url + origin_length, path, written + 1);
    effective = *policy;
    if (effective.required_server_protocol < (scope->role == GIN_RENDER_QUERY ? 36 : 4))
        effective.required_server_protocol = scope->role == GIN_RENDER_QUERY ? 36 : 4;
    r.protocol = gin_protocol_fetch(s, &effective, &info);
    if (r.protocol != GIN_PROTOCOL_OK) { r.code = protocol_error(r.protocol); goto done; }
    r.auth_attempted = 1;
    r.auth = gin_auth_identity(s, credential, &verified);
    if (r.auth != GIN_AUTH_OK) { r.code = auth_error(r.auth); goto done; }
    body = malloc(GIN_HISTORY_RESPONSE_MAX);
    if (!body) { r.code = GIN_HISTORY_NOMEM; goto done; }
    http.url = url; http.ca_file = s->ca_file;
    http.connect_ms = s->connect_ms; http.total_ms = s->total_ms;
    http.header_limit = GIN_HTTP_HEADER_MAX; http.allow_loopback_http = 0;
    q.settings = &http; q.method = GIN_HTTP_GET;
    q.bearer = verified.token; q.bearer_origin = verified.origin;
    q.json_body = NULL; q.json_length = 0;
    r.get_attempted = 1;
    response = gin_http_request(&q, body, GIN_HISTORY_RESPONSE_MAX);
    r.http_status = response.http_status;
    /* Status is diagnostic only until the transfer completed successfully. */
    switch (response.code) {
    case GIN_HTTP_OK: case GIN_HTTP_STATUS:
        if (response.http_status == 200)
            r.code = gin_history_page_decode(scope, request, body, response.body_length, out);
        else if (response.http_status == 401 || response.http_status == 403)
            r.code = GIN_HISTORY_AUTH;
        else r.code = GIN_HISTORY_REMOTE;
        break;
    case GIN_HTTP_TLS: r.code = GIN_HISTORY_TLS; break;
    case GIN_HTTP_TIMEOUT: r.code = GIN_HISTORY_TIMEOUT; break;
    case GIN_HTTP_LIMIT: r.code = GIN_HISTORY_LIMIT; break;
    case GIN_HTTP_INVALID: r.code = GIN_HISTORY_INVALID; break;
    default: r.code = GIN_HISTORY_TRANSPORT; break;
    }
done:
    gin_credential_clear(&verified);
    if (body) { wipe(body, GIN_HISTORY_RESPONSE_MAX); free(body); }
    wipe(url, sizeof(url));
    return r;
}
