#include "gin_auth.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#if JANSSON_VERSION_HEX < 0x020e00
#error Jansson 2.14 or newer is required
#endif

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

/* Only a nesting/string-state preflight, not a JSON parser. Bound depth before
 * Jansson recursion; its decoder owns syntax, UTF-8 and duplicate validation. */
static int bounded_json(const unsigned char *p, size_t n)
{
    size_t i, depth = 0;
    int quoted = 0, escape = 0;
    for (i = 0; i < n; i++) {
        unsigned char c = p[i];
        if (!c) return 0;
        if (quoted) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') quoted = 0;
        } else if (c == '"') quoted = 1;
        else if (c == '{' || c == '[') {
            if (++depth > GIN_AUTH_DEPTH_MAX) return 0;
        } else if (c == '}' || c == ']') {
            if (!depth) return 0;
            depth--;
        }
    }
    return !depth && !quoted;
}

static const char *text(json_t *object, const char *key, size_t max)
{
    json_t *v = json_object_get(object, key);
    const char *s;
    size_t n;
    if (!json_is_string(v)) return NULL;
    s = json_string_value(v); n = json_string_length(v);
    if (!n || n > max || memchr(s, 0, n)) return NULL;
    return s;
}

/* Recursively reject decoded embedded NUL even in ignored additions. */
static int clean_strings(json_t *v)
{
    size_t i;
    const char *key;
    json_t *child;
    if (json_is_string(v))
        return memchr(json_string_value(v), 0, json_string_length(v)) == NULL;
    if (json_is_array(v)) {
        json_array_foreach(v, i, child) if (!clean_strings(child)) return 0;
    } else if (json_is_object(v)) {
        json_object_foreach(v, key, child) {
            (void)key;
            if (!clean_strings(child)) return 0;
        }
    }
    return 1;
}

static enum gin_auth_code exchange(const struct gin_auth_settings *s,
    const char *path, enum gin_http_method method, const char *token,
    const char *data, struct gin_credential *out)
{
    char url[GIN_CREDENTIAL_ORIGIN_MAX + 32];
    unsigned char body[GIN_AUTH_RESPONSE_MAX];
    struct gin_http_settings transport;
    struct gin_http_request q;
    struct gin_http_result r;
    enum gin_auth_code code = GIN_AUTH_MALFORMED;
    json_t *root = NULL, *identity;
    const char *kind, *id, *name, *bearer;
    if (!s || !gin_http_origin_valid(s->origin)) return GIN_AUTH_INVALID;
    strcpy(url, s->origin); strcat(url, path);
    transport.url = url; transport.ca_file = s->ca_file;
    transport.connect_ms = s->connect_ms; transport.total_ms = s->total_ms;
    transport.header_limit = GIN_HTTP_HEADER_MAX; transport.allow_loopback_http = 0;
    q.settings = &transport; q.method = method;
    q.bearer = token; q.bearer_origin = token ? s->origin : NULL;
    q.json_body = (const unsigned char *)data; q.json_length = data ? strlen(data) : 0;
    r = gin_http_request(&q, body, sizeof(body));
    if (r.code != GIN_HTTP_OK && r.code != GIN_HTTP_STATUS) {
        switch (r.code) {
        case GIN_HTTP_TLS: code = GIN_AUTH_TLS; break;
        case GIN_HTTP_INVALID: code = GIN_AUTH_INVALID; break;
        case GIN_HTTP_TIMEOUT: code = GIN_AUTH_TIMEOUT; break;
        case GIN_HTTP_LIMIT: code = GIN_AUTH_LIMIT; break;
        default: code = GIN_AUTH_TRANSPORT; break;
        }
        goto done;
    }
    if (r.http_status == 401) { code = GIN_AUTH_UNAUTHORIZED; goto done; }
    if (r.http_status == 429) { code = GIN_AUTH_THROTTLED; goto done; }
    if (r.http_status >= 500 || (r.http_status >= 300 && r.http_status != 403)) {
        code = GIN_AUTH_REMOTE; goto done;
    }
    if (method == GIN_HTTP_DELETE && r.http_status != 403) {
        code = r.http_status == 204 && !r.body_length ? GIN_AUTH_OK : GIN_AUTH_REMOTE;
        goto done;
    }
    if (!bounded_json(body, r.body_length)) goto done;
    root = json_loadb((const char *)body, r.body_length, JSON_REJECT_DUPLICATES, NULL);
    if (!json_is_object(root) || !clean_strings(root)) goto done;
    if (r.http_status == 403) {
        const char *error = text(root, "error", 255);
        code = error && !strcmp(error, "client_token_scope") ? GIN_AUTH_SCOPE : GIN_AUTH_FORBIDDEN;
        goto done;
    }
    if (r.http_status == 202 && method == GIN_HTTP_POST_JSON) {
        if (json_is_true(json_object_get(root, "two_factor_required"))) code = GIN_AUTH_INTERACTIVE;
        goto done;
    }
    if (r.http_status != 200) goto done;
    identity = method == GIN_HTTP_POST_JSON ? json_object_get(root, "subject") : root;
    kind = text(identity, "kind", 32);
    if (!kind) goto done;
    if (strcmp(kind, "user")) { code = GIN_AUTH_SUBJECT; goto done; }
    id = text(identity, "id", 36); name = text(identity, "name", GIN_CREDENTIAL_NAME_MAX);
    bearer = method == GIN_HTTP_POST_JSON ? text(root, "token", GIN_CREDENTIAL_TOKEN_MAX) : token;
    if (!id || !name || !bearer) goto done;
    strcpy(out->origin, s->origin); strcpy(out->token, bearer);
    strcpy(out->subject_id, id); strcpy(out->subject_name, name);
    if (gin_credential_validate(out) != GIN_CREDENTIAL_OK) goto done;
    code = GIN_AUTH_OK;
done:
    json_decref(root);
    wipe(body, sizeof(body));
    if (out && code != GIN_AUTH_OK) gin_credential_clear(out);
    return code;
}

enum gin_auth_code gin_auth_login(const struct gin_auth_settings *s,
    const char *identifier, const char *password, struct gin_credential *out)
{
    json_t *root;
    char *data;
    enum gin_auth_code code;
    if (!out) return GIN_AUTH_INVALID;
    gin_credential_clear(out);
    if (!identifier || !password || !*identifier || !*password ||
        strnlen(identifier, GIN_AUTH_IDENTIFIER_MAX + 1) > GIN_AUTH_IDENTIFIER_MAX ||
        strnlen(password, GIN_AUTH_PASSWORD_MAX + 1) > GIN_AUTH_PASSWORD_MAX)
        return GIN_AUTH_INVALID;
    root = json_pack("{s:s,s:s}", "identifier", identifier, "password", password);
    if (!root) return GIN_AUTH_INVALID;
    data = json_dumps(root, JSON_COMPACT);
    json_decref(root);
    if (!data) return GIN_AUTH_INVALID;
    code = exchange(s, "/auth/login", GIN_HTTP_POST_JSON, NULL, data, out);
    wipe(data, strlen(data)); free(data);
    return code;
}

enum gin_auth_code gin_auth_resume(const struct gin_auth_settings *s,
    const char *bearer, struct gin_credential *out)
{
    if (!out) return GIN_AUTH_INVALID;
    gin_credential_clear(out);
    if (!bearer) return GIN_AUTH_INVALID;
    return exchange(s, "/me", GIN_HTTP_GET, bearer, NULL, out);
}

static int bound(const struct gin_auth_settings *s, const struct gin_credential *c)
{
    return s && c && gin_http_origin_valid(s->origin) &&
        gin_credential_validate(c) == GIN_CREDENTIAL_OK && !strcmp(s->origin, c->origin);
}

enum gin_auth_code gin_auth_identity(const struct gin_auth_settings *s,
    const struct gin_credential *c, struct gin_credential *out)
{
    enum gin_auth_code code;
    if (!out || c == out) return GIN_AUTH_INVALID;
    gin_credential_clear(out);
    if (!bound(s, c)) return GIN_AUTH_INVALID;
    code = exchange(s, "/me", GIN_HTTP_GET, c->token, NULL, out);
    if (code == GIN_AUTH_OK && strcmp(c->subject_id, out->subject_id)) {
        gin_credential_clear(out); return GIN_AUTH_SUBJECT;
    }
    return code;
}

enum gin_auth_code gin_auth_logout(const struct gin_auth_settings *s,
    const struct gin_credential *c)
{
    if (!bound(s, c)) return GIN_AUTH_INVALID;
    return exchange(s, "/auth/logout", GIN_HTTP_DELETE, c->token, NULL, NULL);
}

const char *gin_auth_error(enum gin_auth_code code)
{
    switch (code) {
    case GIN_AUTH_OK: return "ok";
    case GIN_AUTH_INVALID: return "invalid-settings";
    case GIN_AUTH_UNAUTHORIZED: return "unauthorized";
    case GIN_AUTH_SCOPE: return "client-token-scope";
    case GIN_AUTH_FORBIDDEN: return "forbidden";
    case GIN_AUTH_THROTTLED: return "throttled";
    case GIN_AUTH_INTERACTIVE: return "unsupported-interactive-auth";
    case GIN_AUTH_SUBJECT: return "unsupported-or-changed-subject";
    case GIN_AUTH_MALFORMED: return "malformed-response";
    case GIN_AUTH_REMOTE: return "remote-status";
    case GIN_AUTH_TLS: return "tls";
    case GIN_AUTH_TIMEOUT: return "timeout";
    case GIN_AUTH_LIMIT: return "limit";
    default: return "transport";
    }
}
