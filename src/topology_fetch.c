#include "topology_internal.h"
#include <stdlib.h>
#include <string.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

static enum gin_topology_code protocol_code(enum gin_protocol_code code)
{
    switch (code) {
    case GIN_PROTOCOL_OK: return GIN_TOPOLOGY_OK;
    case GIN_PROTOCOL_INVALID: return GIN_TOPOLOGY_INVALID;
    case GIN_PROTOCOL_MALFORMED: return GIN_TOPOLOGY_MALFORMED;
    case GIN_PROTOCOL_WRONG_SERVER: return GIN_TOPOLOGY_WRONG_SERVER;
    case GIN_PROTOCOL_CLIENT_TOO_OLD: return GIN_TOPOLOGY_CLIENT_TOO_OLD;
    case GIN_PROTOCOL_SERVER_TOO_OLD: return GIN_TOPOLOGY_SERVER_TOO_OLD;
    case GIN_PROTOCOL_TLS: return GIN_TOPOLOGY_TLS;
    case GIN_PROTOCOL_TIMEOUT: return GIN_TOPOLOGY_TIMEOUT;
    case GIN_PROTOCOL_LIMIT: return GIN_TOPOLOGY_LIMIT;
    case GIN_PROTOCOL_TRANSPORT: return GIN_TOPOLOGY_TRANSPORT;
    case GIN_PROTOCOL_REMOTE: return GIN_TOPOLOGY_REMOTE;
    }
    return GIN_TOPOLOGY_REMOTE;
}

static enum gin_topology_code auth_code(enum gin_auth_code code)
{
    switch (code) {
    case GIN_AUTH_OK: return GIN_TOPOLOGY_OK;
    case GIN_AUTH_INVALID: return GIN_TOPOLOGY_INVALID;
    case GIN_AUTH_UNAUTHORIZED: return GIN_TOPOLOGY_UNAUTHORIZED;
    case GIN_AUTH_SCOPE: return GIN_TOPOLOGY_SCOPE;
    case GIN_AUTH_FORBIDDEN: return GIN_TOPOLOGY_FORBIDDEN;
    case GIN_AUTH_THROTTLED: return GIN_TOPOLOGY_THROTTLED;
    case GIN_AUTH_SUBJECT: return GIN_TOPOLOGY_SUBJECT;
    case GIN_AUTH_MALFORMED: return GIN_TOPOLOGY_MALFORMED;
    case GIN_AUTH_TLS: return GIN_TOPOLOGY_TLS;
    case GIN_AUTH_TIMEOUT: return GIN_TOPOLOGY_TIMEOUT;
    case GIN_AUTH_LIMIT: return GIN_TOPOLOGY_LIMIT;
    case GIN_AUTH_TRANSPORT: return GIN_TOPOLOGY_TRANSPORT;
    case GIN_AUTH_INTERACTIVE: case GIN_AUTH_REMOTE: return GIN_TOPOLOGY_REMOTE;
    }
    return GIN_TOPOLOGY_REMOTE;
}

enum gin_topology_code gin_topology_fetch(const struct gin_auth_settings *s,
    const struct gin_protocol_policy *policy, const struct gin_credential *credential,
    const char *chantypes, struct gin_topology **out)
{
    struct gin_protocol_policy effective;
    struct gin_protocol_info server;
    struct gin_credential validated;
    struct gin_http_settings transport;
    struct gin_http_request request;
    struct gin_http_result result;
    enum gin_topology_code code;
    unsigned char *body;
    char url[GIN_CREDENTIAL_ORIGIN_MAX + sizeof("/boot")];
    /* Finish every local check before even the credential-free preflight. */
    if (!out || *out || !s || !policy || policy->client_protocol <= 0 ||
        policy->required_server_protocol <= 0 || !gin_http_origin_valid(s->origin) ||
        s->connect_ms <= 0 || s->total_ms <= 0 || s->connect_ms > s->total_ms ||
        s->total_ms > GIN_HTTP_TIMEOUT_MAX || !chantypes ||
        gin_chantypes_validate(chantypes) != 0 ||
        gin_credential_validate(credential) != GIN_CREDENTIAL_OK ||
        strcmp(s->origin, credential->origin)) return GIN_TOPOLOGY_INVALID;
    effective = *policy;
    if (effective.required_server_protocol < GIN_TOPOLOGY_SERVER_MIN)
        effective.required_server_protocol = GIN_TOPOLOGY_SERVER_MIN;
    code = protocol_code(gin_protocol_fetch(s, &effective, &server));
    if (code != GIN_TOPOLOGY_OK) return code;
    memset(&validated, 0, sizeof(validated));
    code = auth_code(gin_auth_identity(s, credential, &validated));
    if (code != GIN_TOPOLOGY_OK) {
        gin_credential_clear(&validated);
        return code;
    }
    body = malloc(GIN_TOPOLOGY_BODY_MAX);
    if (!body) {
        gin_credential_clear(&validated);
        return GIN_TOPOLOGY_NOMEM;
    }
    strcpy(url, s->origin); strcat(url, "/boot");
    transport.url = url; transport.ca_file = s->ca_file;
    transport.connect_ms = s->connect_ms; transport.total_ms = s->total_ms;
    transport.header_limit = GIN_HTTP_HEADER_MAX; transport.allow_loopback_http = 0;
    request.settings = &transport; request.method = GIN_HTTP_GET;
    request.bearer = validated.token; request.bearer_origin = validated.origin;
    request.json_body = NULL; request.json_length = 0;
    result = gin_http_request(&request, body, GIN_TOPOLOGY_BODY_MAX);
    switch (result.code) {
    case GIN_HTTP_OK: case GIN_HTTP_STATUS:
        code = result.http_status == 200 ?
            gin_topology_parse_boot(body, result.body_length, chantypes, out) :
            gin_topology_http_status(result.http_status, body, result.body_length);
        break;
    case GIN_HTTP_INVALID: code = GIN_TOPOLOGY_INVALID; break;
    case GIN_HTTP_TLS: code = GIN_TOPOLOGY_TLS; break;
    case GIN_HTTP_TIMEOUT: code = GIN_TOPOLOGY_TIMEOUT; break;
    case GIN_HTTP_LIMIT: code = GIN_TOPOLOGY_LIMIT; break;
    default: code = GIN_TOPOLOGY_TRANSPORT; break;
    }
    /* Includes ignored private head content, not only retained topology bytes. */
    wipe(body, GIN_TOPOLOGY_BODY_MAX); free(body);
    wipe(url, sizeof(url));
    gin_credential_clear(&validated);
    return code;
}
