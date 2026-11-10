#include "gin_protocol.h"
#include <string.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

enum gin_protocol_code gin_protocol_fetch(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, struct gin_protocol_info *out)
{
    char url[GIN_CREDENTIAL_ORIGIN_MAX + sizeof("/api/config")];
    unsigned char body[GIN_PROTOCOL_BODY_MAX];
    struct gin_http_settings transport;
    struct gin_http_result result;
    enum gin_protocol_code code;
    if (!out) return GIN_PROTOCOL_INVALID;
    memset(out, 0, sizeof(*out));
    if (!s || !policy || policy->client_protocol <= 0 ||
        policy->required_server_protocol <= 0 || !gin_http_origin_valid(s->origin) ||
        s->connect_ms <= 0 || s->total_ms <= 0 ||
        s->connect_ms > s->total_ms || s->total_ms > GIN_HTTP_TIMEOUT_MAX)
        return GIN_PROTOCOL_INVALID;
    strcpy(url, s->origin);
    strcat(url, "/api/config");
    transport.url = url;
    transport.ca_file = s->ca_file;
    transport.connect_ms = s->connect_ms;
    transport.total_ms = s->total_ms;
    transport.header_limit = GIN_HTTP_HEADER_MAX;
    transport.allow_loopback_http = 0;
    /* The unauthenticated API has no bearer or request-body fields. */
    result = gin_http_get(&transport, body, sizeof(body));
    switch (result.code) {
    case GIN_HTTP_OK:
        code = result.http_status == 200 ?
            gin_protocol_parse(policy, body, result.body_length, out) : GIN_PROTOCOL_REMOTE;
        break;
    case GIN_HTTP_STATUS: code = GIN_PROTOCOL_REMOTE; break;
    case GIN_HTTP_INVALID: code = GIN_PROTOCOL_INVALID; break;
    case GIN_HTTP_TLS: code = GIN_PROTOCOL_TLS; break;
    case GIN_HTTP_TIMEOUT: code = GIN_PROTOCOL_TIMEOUT; break;
    case GIN_HTTP_LIMIT: code = GIN_PROTOCOL_LIMIT; break;
    default: code = GIN_PROTOCOL_TRANSPORT; break;
    }
    wipe(body, sizeof(body));
    wipe(url, sizeof(url));
    if (code != GIN_PROTOCOL_OK) memset(out, 0, sizeof(*out));
    return code;
}

static struct gin_protocol_auth_result preflight(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, struct gin_credential *out)
{
    struct gin_protocol_auth_result r;
    memset(&r, 0, sizeof(r));
    r.protocol = GIN_PROTOCOL_INVALID;
    r.auth = GIN_AUTH_INVALID;
    if (out) {
        gin_credential_clear(out);
        r.protocol = gin_protocol_fetch(s, policy, &r.server);
    }
    return r;
}

struct gin_protocol_auth_result gin_protocol_login(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, const char *identifier,
    const char *password, struct gin_credential *out)
{
    struct gin_protocol_auth_result r = preflight(s, policy, out);
    if (r.protocol == GIN_PROTOCOL_OK) {
        r.auth_attempted = 1;
        r.auth = gin_auth_login(s, identifier, password, out);
    }
    return r;
}

struct gin_protocol_auth_result gin_protocol_resume(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, const char *bearer,
    struct gin_credential *out)
{
    struct gin_protocol_auth_result r = preflight(s, policy, out);
    if (r.protocol == GIN_PROTOCOL_OK) {
        r.auth_attempted = 1;
        r.auth = gin_auth_resume(s, bearer, out);
    }
    return r;
}
