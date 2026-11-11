#include "send_values.h"
#include "gin_naming.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>

static int gin_is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~';
}

static int gin_valid_utf8(const unsigned char *s, size_t n)
{
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        uint32_t cp;
        size_t extra;
        size_t k;
        if (c < 0x20 || c == 0x7f) return 0;
        if (c < 0x80) { i++; continue; }
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 0x1f; extra = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 0x0f; extra = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 0x07; extra = 3; }
        else return 0;
        if (i + extra >= n) return 0;
        for (k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xc0) != 0x80) return 0;
            cp = (cp << 6) | (uint32_t)(s[i + k] & 0x3f);
        }
        if (extra == 2) {
            if (cp < 0x800) return 0;
            if (cp >= 0xd800 && cp <= 0xdfff) return 0;
        } else if (extra == 3) {
            if (cp < 0x10000 || cp > 0x10ffff) return 0;
        }
        for (k = 0; k <= extra; k++) {
            if (s[i + k] < 0x20 || s[i + k] == 0x7f) return 0;
        }
        i += extra + 1;
    }
    return 1;
}

static size_t gin_encoded_len(const char *s)
{
    size_t n = 0;
    size_t i;
    for (i = 0; s[i] != '\0'; i++)
        n += gin_is_unreserved((unsigned char)s[i]) ? 1u : 3u;
    return n;
}

static char *gin_write_encoded(char *dst, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t i;
    for (i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (gin_is_unreserved(c)) {
            *dst++ = (char)c;
        } else {
            *dst++ = '%';
            *dst++ = hex[c >> 4];
            *dst++ = hex[c & 0x0f];
        }
    }
    return dst;
}

enum gin_send_code gin_send_payload_build(const struct gin_send_route *route,
    const unsigned char *body, size_t length, char **path,
    unsigned char **json, size_t *json_length)
{
    struct gin_name name;
    const char *middle;
    size_t slug_len;
    size_t target_len;
    size_t total;
    char *built_path;
    char *p;
    json_t *object;
    json_t *text;
    char *dumped;
    size_t dumped_len;

    if (route == NULL || path == NULL || json == NULL || json_length == NULL)
        return GIN_SEND_INVALID;
    if ((void *)path == (void *)json || (void *)path == (void *)json_length ||
        (void *)json == (void *)json_length)
        return GIN_SEND_INVALID;
    if (*path != NULL || *json != NULL)
        return GIN_SEND_INVALID;
    if (route->network_slug == NULL || route->wire_target == NULL ||
        route->chantypes == NULL)
        return GIN_SEND_INVALID;
    if (route->role != GIN_SEND_ROLE_UNKNOWN &&
        route->role != GIN_SEND_ROLE_CHANNEL &&
        route->role != GIN_SEND_ROLE_QUERY)
        return GIN_SEND_INVALID;
    if (route->mode != GIN_SEND_MODE_UNKNOWN &&
        route->mode != GIN_SEND_MODE_PLAIN &&
        route->mode != GIN_SEND_MODE_ENCRYPTED)
        return GIN_SEND_INVALID;
    if (route->role == GIN_SEND_ROLE_UNKNOWN)
        return GIN_SEND_INVALID;
    if (route->mode != GIN_SEND_MODE_PLAIN)
        return GIN_SEND_UNSUPPORTED;
    if (body == NULL)
        return GIN_SEND_INVALID;
    if (length == 0)
        return GIN_SEND_INVALID;
    if (length > GIN_SEND_BODY_MAX)
        return GIN_SEND_LIMIT;
    if (!gin_valid_utf8(body, length))
        return GIN_SEND_INVALID;
    if (route->wire_target[0] == '\0' || route->network_slug[0] == '\0')
        return GIN_SEND_INVALID;
    if (gin_name_prepare(route->network_slug, route->wire_target,
                         route->chantypes, &name) != 0)
        return GIN_SEND_INVALID;
    if (strcmp(name.canonical_target, "$server") == 0)
        return GIN_SEND_INVALID;
    if (route->role == GIN_SEND_ROLE_QUERY) {
        char first = route->wire_target[0];
        if (first == '#' || first == '&' || first == '+' || first == '!')
            return GIN_SEND_INVALID;
        if (name.kind != GIN_WINDOW_NONCHANNEL)
            return GIN_SEND_INVALID;
        middle = "/channels/";
    } else {
        if (name.kind != GIN_WINDOW_CHANNEL)
            return GIN_SEND_INVALID;
        middle = "/channels/";
    }
    slug_len = gin_encoded_len(route->network_slug);
    target_len = gin_encoded_len(route->wire_target);
    total = strlen("/networks/") + slug_len + strlen(middle) + target_len +
            strlen("/messages");
    if (total > GIN_SEND_PATH_MAX)
        return GIN_SEND_LIMIT;
    built_path = (char *)malloc(total + 1);
    if (built_path == NULL)
        return GIN_SEND_NOMEM;
    p = built_path;
    memcpy(p, "/networks/", strlen("/networks/"));
    p += strlen("/networks/");
    p = gin_write_encoded(p, route->network_slug);
    memcpy(p, middle, strlen(middle));
    p += strlen(middle);
    p = gin_write_encoded(p, route->wire_target);
    memcpy(p, "/messages", strlen("/messages"));
    p += strlen("/messages");
    *p = '\0';
    object = json_object();
    if (object == NULL) { free(built_path); return GIN_SEND_NOMEM; }
    text = json_stringn((const char *)body, length);
    if (text == NULL) { json_decref(object); free(built_path); return GIN_SEND_NOMEM; }
    if (json_object_set_new(object, "body", text) != 0) {
        json_decref(object);
        free(built_path);
        return GIN_SEND_NOMEM;
    }
    dumped = json_dumps(object, JSON_COMPACT);
    json_decref(object);
    if (dumped == NULL) { free(built_path); return GIN_SEND_NOMEM; }
    dumped_len = strlen(dumped);
    if (dumped_len > GIN_SEND_JSON_MAX) {
        free(dumped);
        free(built_path);
        return GIN_SEND_LIMIT;
    }
    *path = built_path;
    *json = (unsigned char *)dumped;
    *json_length = dumped_len;
    return GIN_SEND_OK;
}
