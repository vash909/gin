#include "gin_protocol.h"
#include "json_evidence.h"

#include <jansson.h>
#include <limits.h>
#include <string.h>

static void clear_info(struct gin_protocol_info *out)
{
    if (out != NULL) {
        memset(out, 0, sizeof(*out));
    }
}

static int preflight(const unsigned char *body, size_t length)
{
    size_t i;
    int depth = 0;
    int in_string = 0;
    int escaped = 0;

    for (i = 0; i < length; i++) {
        unsigned char c = body[i];
        if (c == '\0') {
            return 0;
        }
        if (in_string) {
            if (escaped) {
                escaped = 0;
            } else if (c == '\\') {
                escaped = 1;
            } else if (c == '"') {
                in_string = 0;
            }
        } else if (c == '"') {
            in_string = 1;
        } else if (c == '{' || c == '[') {
            depth++;
            if (depth > GIN_PROTOCOL_DEPTH_MAX) {
                return 0;
            }
        } else if (c == '}' || c == ']') {
            if (depth == 0) {
                return 0;
            }
            depth--;
        }
    }
    return 1;
}

static int positive_int_field(json_t *root, const char *key, int *value)
{
    json_t *field = json_object_get(root, key);
    json_int_t v;

    if (!json_is_integer(field)) {
        return 0;
    }
    v = json_integer_value(field);
    if (v <= 0 || v > INT_MAX) {
        return 0;
    }
    *value = (int)v;
    return 1;
}

static int diagnostic_clean(const char *s, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || c == 0x7f) {
            return 0;
        }
        if (c == 0xc2 && i + 1 < length) {
            unsigned char next = (unsigned char)s[i + 1];
            if (next >= 0x80 && next <= 0x9f) {
                return 0;
            }
        }
    }
    return 1;
}

enum gin_protocol_code gin_protocol_parse(const struct gin_protocol_policy *policy,
    const unsigned char *body, size_t length, struct gin_protocol_info *out)
{
    json_error_t error = {0};
    json_t *root;
    json_t *server;
    json_t *version;
    int s = 0;
    int m = 0;

    clear_info(out);
    if (policy == NULL || body == NULL || out == NULL) {
        return GIN_PROTOCOL_INVALID;
    }
    if (policy->client_protocol <= 0 || policy->required_server_protocol <= 0) {
        return GIN_PROTOCOL_INVALID;
    }
    if (length > GIN_PROTOCOL_BODY_MAX) {
        return GIN_PROTOCOL_LIMIT;
    }
    if (length == 0) {
        return GIN_PROTOCOL_MALFORMED;
    }
    if (!preflight(body, length)) {
        return GIN_PROTOCOL_MALFORMED;
    }
    root = json_loadb((const char *)body, length, JSON_REJECT_DUPLICATES, &error);
    if (root == NULL) {
        return GIN_PROTOCOL_MALFORMED;
    }
    if (!json_is_object(root) || !gin_json_evidence(body, length, root)) {
        json_decref(root);
        return GIN_PROTOCOL_MALFORMED;
    }
    server = json_object_get(root, "server");
    if (!json_is_string(server)) {
        json_decref(root);
        return GIN_PROTOCOL_MALFORMED;
    }
    if (strcmp(json_string_value(server), "grappa") != 0) {
        json_decref(root);
        return GIN_PROTOCOL_WRONG_SERVER;
    }
    if (!positive_int_field(root, "protocol_version", &s) ||
        !positive_int_field(root, "min_protocol_version", &m) || m > s) {
        json_decref(root);
        return GIN_PROTOCOL_MALFORMED;
    }
    if (policy->client_protocol < m) {
        json_decref(root);
        return GIN_PROTOCOL_CLIENT_TOO_OLD;
    }
    if (s < policy->required_server_protocol) {
        json_decref(root);
        return GIN_PROTOCOL_SERVER_TOO_OLD;
    }
    out->server_protocol = s;
    out->minimum_client_protocol = m;
    version = json_object_get(root, "version");
    if (json_is_string(version)) {
        const char *v = json_string_value(version);
        if (v != NULL) {
            size_t vlen = strlen(v);
            if (vlen > 0 && vlen <= GIN_PROTOCOL_SOFTWARE_MAX &&
                diagnostic_clean(v, vlen)) {
                memcpy(out->software_version, v, vlen + 1);
            }
        }
    }
    json_decref(root);
    return GIN_PROTOCOL_OK;
}

int gin_protocol_server_at_least(const struct gin_protocol_info *info, int introduced)
{
    if (info == NULL || introduced <= 0) {
        return 0;
    }
    if (info->server_protocol <= 0 || info->minimum_client_protocol <= 0 ||
        info->minimum_client_protocol > info->server_protocol) {
        return 0;
    }
    return info->server_protocol >= introduced ? 1 : 0;
}

const char *gin_protocol_error(enum gin_protocol_code code)
{
    switch (code) {
    case GIN_PROTOCOL_OK:
        return "ok";
    case GIN_PROTOCOL_INVALID:
        return "invalid-settings";
    case GIN_PROTOCOL_MALFORMED:
        return "malformed-config";
    case GIN_PROTOCOL_WRONG_SERVER:
        return "unexpected-server";
    case GIN_PROTOCOL_CLIENT_TOO_OLD:
        return "client-protocol-too-old";
    case GIN_PROTOCOL_SERVER_TOO_OLD:
        return "server-protocol-too-old";
    case GIN_PROTOCOL_TLS:
        return "tls";
    case GIN_PROTOCOL_TIMEOUT:
        return "timeout";
    case GIN_PROTOCOL_LIMIT:
        return "limit";
    case GIN_PROTOCOL_TRANSPORT:
        return "transport";
    case GIN_PROTOCOL_REMOTE:
        return "remote-status";
    default:
        return "unknown-protocol-error";
    }
}
