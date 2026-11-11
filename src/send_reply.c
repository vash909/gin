#include "send_values.h"
#include <jansson.h>
#include <string.h>

/* Cross-check only consumed top-level evidence against immutable wire bytes.
 * Jansson still owns whole-document validation. This is not an independent
 * JSON validator or a repair of dependency memory safety under allocation loss.
 * In tested Jansson 2.14 a scratch append failure can silently alter a token. */
typedef struct {
    const unsigned char *s;
    size_t len;
    size_t pos;
    const char *slug;
    size_t slug_len;
    int64_t raw_id;
    int network_ok;
    int ok_true;
} gin_raw_scan;

typedef struct {
    size_t len;
    int eq_id;
    int eq_network;
    int eq_ok;
    int eq_slug;
} gin_str_info;

static void gin_str_feed(const gin_raw_scan *sc, gin_str_info *info, unsigned int cp)
{
    if (cp > 127u) {
        info->eq_id = 0;
        info->eq_network = 0;
        info->eq_ok = 0;
        info->eq_slug = 0;
    } else {
        char c = (char)cp;
        if (info->len >= 2u || "id"[info->len] != c) info->eq_id = 0;
        if (info->len >= 7u || "network"[info->len] != c) info->eq_network = 0;
        if (info->len >= 2u || "ok"[info->len] != c) info->eq_ok = 0;
        if (info->len >= sc->slug_len || sc->slug[info->len] != c) info->eq_slug = 0;
    }
    info->len++;
}

static void gin_raw_skip_ws(gin_raw_scan *sc)
{
    while (sc->pos < sc->len && (sc->s[sc->pos] == ' ' || sc->s[sc->pos] == '\t' ||
           sc->s[sc->pos] == '\n' || sc->s[sc->pos] == '\r'))
        sc->pos++;
}

static int gin_raw_hex(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int gin_raw_scan_string(gin_raw_scan *sc, gin_str_info *info)
{
    if (sc->pos >= sc->len || sc->s[sc->pos] != '"') return 0;
    sc->pos++;
    info->len = 0;
    info->eq_id = 1;
    info->eq_network = 1;
    info->eq_ok = 1;
    info->eq_slug = 1;
    while (sc->pos < sc->len) {
        unsigned char c = sc->s[sc->pos++];
        if (c == '"') {
            info->eq_id = info->eq_id && info->len == 2u;
            info->eq_network = info->eq_network && info->len == 7u;
            info->eq_ok = info->eq_ok && info->len == 2u;
            info->eq_slug = info->eq_slug && info->len == sc->slug_len;
            return 1;
        }
        if (c == '\\') {
            unsigned char e;
            unsigned int cp;
            if (sc->pos >= sc->len) return 0;
            e = sc->s[sc->pos++];
            switch (e) {
            case '"': cp = '"'; break;
            case '\\': cp = '\\'; break;
            case '/': cp = '/'; break;
            case 'b': cp = 8u; break;
            case 'f': cp = 12u; break;
            case 'n': cp = 10u; break;
            case 'r': cp = 13u; break;
            case 't': cp = 9u; break;
            case 'u': {
                int i;
                cp = 0;
                for (i = 0; i < 4; i++) {
                    int h;
                    if (sc->pos >= sc->len) return 0;
                    h = gin_raw_hex(sc->s[sc->pos++]);
                    if (h < 0) return 0;
                    cp = cp * 16u + (unsigned int)h;
                }
                break;
            }
            default: return 0;
            }
            gin_str_feed(sc, info, cp);
        } else if (c < 0x20u) {
            return 0;
        } else {
            gin_str_feed(sc, info, c);
        }
    }
    return 0;
}

static int gin_raw_skip_value(gin_raw_scan *sc)
{
    unsigned char c;
    gin_raw_skip_ws(sc);
    if (sc->pos >= sc->len) return 0;
    c = sc->s[sc->pos];
    if (c == '"') {
        gin_str_info info;
        return gin_raw_scan_string(sc, &info);
    }
    if (c == '{' || c == '[') {
        char stack[GIN_SEND_DEPTH_MAX];
        size_t sp = 0;
        stack[sp++] = (char)c;
        sc->pos++;
        while (sp > 0) {
            if (sc->pos >= sc->len) return 0;
            c = sc->s[sc->pos];
            if (c == '"') {
                gin_str_info info;
                if (!gin_raw_scan_string(sc, &info)) return 0;
            } else if (c == '{' || c == '[') {
                if (1u + sp + 1u > GIN_SEND_DEPTH_MAX) return 0;
                stack[sp++] = (char)c;
                sc->pos++;
            } else if (c == '}' || c == ']') {
                char open = stack[sp - 1u];
                if ((open == '{' && c != '}') || (open == '[' && c != ']')) return 0;
                sp--;
                sc->pos++;
            } else if (c < 0x20u && c != '\t' && c != '\n' && c != '\r') {
                return 0;
            } else {
                sc->pos++;
            }
        }
        return 1;
    }
    {
        size_t start = sc->pos;
        while (sc->pos < sc->len) {
            c = sc->s[sc->pos];
            if (c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' ||
                c == '\n' || c == '\r')
                break;
            if (c < 0x20u || c == '"' || c == '{' || c == '[') return 0;
            sc->pos++;
        }
        return sc->pos > start;
    }
}

static int gin_raw_scan_id(gin_raw_scan *sc, int64_t *out)
{
    int64_t value = 0;
    if (sc->pos >= sc->len || sc->s[sc->pos] < '1' || sc->s[sc->pos] > '9') return 0;
    while (sc->pos < sc->len && sc->s[sc->pos] >= '0' && sc->s[sc->pos] <= '9') {
        int d = sc->s[sc->pos] - '0';
        if (value > (INT64_MAX - d) / 10) return 0;
        value = value * 10 + d;
        sc->pos++;
    }
    if (sc->pos < sc->len) {
        unsigned char c = sc->s[sc->pos];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != ',' &&
            c != '}' && c != ']')
            return 0;
    }
    *out = value;
    return 1;
}

static int gin_raw_scan_true(gin_raw_scan *sc)
{
    if (sc->len - sc->pos < 4u || memcmp(sc->s + sc->pos, "true", 4u) != 0) return 0;
    if (sc->pos + 4u < sc->len) {
        unsigned char c = sc->s[sc->pos + 4u];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != ',' &&
            c != '}' && c != ']')
            return 0;
    }
    sc->pos += 4u;
    return 1;
}

static int gin_raw_check(gin_raw_scan *sc, long status)
{
    unsigned int id_count = 0;
    unsigned int network_count = 0;
    unsigned int ok_count = 0;
    gin_raw_skip_ws(sc);
    if (sc->pos >= sc->len || sc->s[sc->pos] != '{') return 0;
    sc->pos++;
    gin_raw_skip_ws(sc);
    if (sc->pos < sc->len && sc->s[sc->pos] == '}') {
        sc->pos++;
    } else {
        for (;;) {
            gin_str_info key;
            gin_raw_skip_ws(sc);
            if (!gin_raw_scan_string(sc, &key)) return 0;
            gin_raw_skip_ws(sc);
            if (sc->pos >= sc->len || sc->s[sc->pos] != ':') return 0;
            sc->pos++;
            gin_raw_skip_ws(sc);
            if (sc->pos >= sc->len) return 0;
            if (status == 201 && key.eq_id) {
                if (++id_count != 1u) return 0;
                if (!gin_raw_scan_id(sc, &sc->raw_id)) return 0;
            } else if (status == 201 && key.eq_network) {
                gin_str_info value;
                if (++network_count != 1u) return 0;
                if (sc->s[sc->pos] != '"') return 0;
                if (!gin_raw_scan_string(sc, &value)) return 0;
                if (!value.eq_slug) return 0;
                sc->network_ok = 1;
            } else if (status == 202 && key.eq_ok) {
                if (++ok_count != 1u) return 0;
                if (!gin_raw_scan_true(sc)) return 0;
                sc->ok_true = 1;
            } else if (!gin_raw_skip_value(sc)) {
                return 0;
            }
            gin_raw_skip_ws(sc);
            if (sc->pos >= sc->len) return 0;
            if (sc->s[sc->pos] == ',') {
                sc->pos++;
                continue;
            }
            if (sc->s[sc->pos] == '}') {
                sc->pos++;
                break;
            }
            return 0;
        }
    }
    gin_raw_skip_ws(sc);
    if (sc->pos != sc->len) return 0;
    if (status == 201) return id_count == 1u && network_count == 1u && sc->network_ok;
    return ok_count == 1u && sc->ok_true;
}

enum gin_send_code gin_send_reply_parse(long status,
    const unsigned char *body, size_t length, const char *network_slug,
    int64_t *message_id, enum gin_send_outcome *outcome)
{
    json_error_t error = {0};
    json_t *root;
    size_t i;
    size_t slug_len;
    unsigned int depth = 0;
    int in_string = 0;
    int escaped = 0;

    if (network_slug == NULL || message_id == NULL || outcome == NULL)
        return GIN_SEND_INVALID;
    slug_len = strnlen(network_slug, 256u);
    if (slug_len == 0u || slug_len > 255u) return GIN_SEND_INVALID;
    for (i = 0; i < slug_len; i++) {
        unsigned char c = (unsigned char)network_slug[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-'))
            return GIN_SEND_INVALID;
    }
    if (body == NULL && length != 0)
        return GIN_SEND_INVALID;
    if (length > GIN_SEND_RESPONSE_MAX)
        return GIN_SEND_LIMIT;
    if (length == 0)
        return GIN_SEND_MALFORMED;
    for (i = 0; i < length; i++) {
        if (body[i] == 0)
            return GIN_SEND_MALFORMED;
    }
    for (i = 0; i < length; i++) {
        unsigned char c = body[i];
        if (in_string) {
            if (escaped) escaped = 0;
            else if (c == '\\') escaped = 1;
            else if (c == '"') in_string = 0;
        } else if (c == '"') {
            in_string = 1;
        } else if (c == '{' || c == '[') {
            depth++;
            if (depth > GIN_SEND_DEPTH_MAX)
                return GIN_SEND_LIMIT;
        } else if (c == '}' || c == ']') {
            if (depth > 0) depth--;
        }
    }
    if (status != 201 && status != 202)
        return GIN_SEND_MALFORMED;
    root = json_loadb((const char *)body, length, JSON_REJECT_DUPLICATES, &error);
    if (root == NULL)
        return GIN_SEND_MALFORMED;
    if (!json_is_object(root)) {
        json_decref(root);
        return GIN_SEND_MALFORMED;
    }
    if (status == 201) {
        json_t *id_value = json_object_get(root, "id");
        json_t *network_value = json_object_get(root, "network");
        const char *network_text;
        int64_t id;
        gin_raw_scan sc;
        if (!json_is_integer(id_value) || !json_is_string(network_value)) {
            json_decref(root);
            return GIN_SEND_MALFORMED;
        }
        id = (int64_t)json_integer_value(id_value);
        if (id <= 0) {
            json_decref(root);
            return GIN_SEND_MALFORMED;
        }
        network_text = json_string_value(network_value);
        if (network_text == NULL ||
            json_string_length(network_value) != slug_len ||
            memcmp(network_text, network_slug, slug_len) != 0) {
            json_decref(root);
            return GIN_SEND_MALFORMED;
        }
        json_decref(root);
        sc.s = body; sc.len = length; sc.pos = 0; sc.slug = network_slug;
        sc.slug_len = slug_len; sc.raw_id = 0; sc.network_ok = 0; sc.ok_true = 0;
        if (!gin_raw_check(&sc, status) || sc.raw_id != id)
            return GIN_SEND_MALFORMED;
        *message_id = id;
        *outcome = GIN_SEND_ACCEPTED_PERSISTED;
        return GIN_SEND_OK;
    } else {
        json_t *ok_value = json_object_get(root, "ok");
        gin_raw_scan sc;
        if (!json_is_true(ok_value)) {
            json_decref(root);
            return GIN_SEND_MALFORMED;
        }
        json_decref(root);
        sc.s = body; sc.len = length; sc.pos = 0; sc.slug = network_slug;
        sc.slug_len = slug_len; sc.raw_id = 0; sc.network_ok = 0; sc.ok_true = 0;
        if (!gin_raw_check(&sc, status))
            return GIN_SEND_MALFORMED;
        *message_id = 0;
        *outcome = GIN_SEND_ACCEPTED_NO_PERSIST;
        return GIN_SEND_OK;
    }
}
