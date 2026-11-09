#include "gin_naming.h"
#include <errno.h>
#include <string.h>

static const char hex[] = "0123456789ABCDEF";

static int
invalid(void)
{
    errno = EINVAL;
    return -1;
}

static int
copy_result(const char *value, char *out, size_t capacity)
{
    size_t length = strlen(value);
    if (length >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, value, length + 1);
    return 0;
}

static int
readable(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static int
encode_bytes(const char *input, int dots, const char *prefix,
             char *out, size_t capacity)
{
    char encoded[256];
    size_t used = strlen(prefix);
    const unsigned char *p = (const unsigned char *)input;
    memcpy(encoded, prefix, used);
    for (; *p != 0; ++p) {
        int plain = readable(*p) || (dots && *p == '.');
        size_t amount = plain ? 1 : 3;
        if (used + amount > 255) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (plain)
            encoded[used++] = (char)*p;
        else {
            encoded[used++] = '%';
            encoded[used++] = hex[*p >> 4];
            encoded[used++] = hex[*p & 15];
        }
    }
    encoded[used] = '\0';
    return copy_result(encoded, out, capacity);
}

/* Syntax only here; re-encoding below rejects unnecessary escapes/aliases. */
static int
decode_bytes(const char *component, char *out, size_t capacity)
{
    char decoded[257];
    size_t i = 0, used = 0, length = strlen(component);
    if (length == 0 || length > 255)
        return invalid();
    while (i < length) {
        unsigned char c = (unsigned char)component[i++];
        if (c == '%') {
            const char *high, *low;
            if (i + 2 > length || component[i] == '\0' || component[i + 1] == '\0')
                return invalid();
            high = strchr(hex, component[i++]);
            low = strchr(hex, component[i++]);
            if (high == NULL || low == NULL)
                return invalid();
            c = (unsigned char)((high - hex) * 16 + (low - hex));
            if (c == 0)
                return invalid();
        }
        decoded[used++] = (char)c;
    }
    decoded[used] = '\0';
    return copy_result(decoded, out, capacity);
}

/* Strict UTF-8 scalars; no normalization or Unicode case folding. */
static int
valid_target_text(const char *target)
{
    const unsigned char *p = (const unsigned char *)target;
    size_t left = strlen(target);
    if (left == 0 || left > 256)
        return 0;
    while (left > 0) {
        unsigned long scalar;
        size_t count, i;
        unsigned char c = *p;
        if (c < 128) {
            if (c <= 32 || c == 127 || c == '/')
                return 0;
            count = 1;
            scalar = c;
        } else {
            if (c >= 0xC2 && c <= 0xDF) {
                count = 2;
                scalar = c & 0x1F;
            } else if (c >= 0xE0 && c <= 0xEF) {
                count = 3;
                scalar = c & 0x0F;
            } else if (c >= 0xF0 && c <= 0xF4) {
                count = 4;
                scalar = c & 0x07;
            } else {
                return 0;
            }
            if (left < count)
                return 0;
            for (i = 1; i < count; ++i) {
                if (p[i] < 0x80 || p[i] > 0xBF)
                    return 0;
                scalar = (scalar << 6) | (p[i] & 0x3F);
            }
            if ((count == 3 && scalar < 0x800) ||
                (count == 4 && scalar < 0x10000) ||
                (scalar >= 0xD800 && scalar <= 0xDFFF) || scalar > 0x10FFFF ||
                (scalar >= 0x80 && scalar <= 0x9F))
                return 0;
        }
        p += count;
        left -= count;
    }
    return 1;
}

int
gin_chantypes_validate(const char *chantypes)
{
    const char *p;
    if (strlen(chantypes) > 4)
        return invalid();
    for (p = chantypes; *p != '\0'; ++p)
        if (strchr("#&+!", *p) == NULL || strchr(p + 1, *p) != NULL)
            return invalid();
    return 0;
}

static int
canonicalize(const char *target, const char *chantypes, char canonical[257],
             enum gin_window_kind *kind)
{
    size_t i;
    const char *body = target;
    if (gin_chantypes_validate(chantypes) == -1 || !valid_target_text(target))
        return invalid();
    *kind = strchr(chantypes, *target) != NULL ?
        GIN_WINDOW_CHANNEL : GIN_WINDOW_NONCHANNEL;
    if (*kind == GIN_WINDOW_CHANNEL)
        ++body;
    if (*body == '\0' || strcmp(body, ".") == 0 || strcmp(body, "..") == 0)
        return invalid();
    for (i = 0; target[i] != '\0'; ++i) {
        unsigned char c = (unsigned char)target[i];
        canonical[i] = (char)(c >= 'A' && c <= 'Z' ? c + 32 : c);
    }
    canonical[i] = '\0';
    return 0;
}

int
gin_window_encode(const char *target, const char *chantypes,
                  char *out, size_t capacity, enum gin_window_kind *kind)
{
    char canonical[257], prefix[4] = "";
    const char *body = canonical;
    enum gin_window_kind type;
    if (canonicalize(target, chantypes, canonical, &type) == -1)
        return -1;
    if (type == GIN_WINDOW_CHANNEL) {
        unsigned char c = (unsigned char)*body++;
        if (c != '#') {
            prefix[0] = '+';
            prefix[1] = hex[c >> 4];
            prefix[2] = hex[c & 15];
            prefix[3] = '\0';
        }
    } else {
        prefix[0] = '@';
        prefix[1] = '\0';
    }
    if (encode_bytes(body, 1, prefix, out, capacity) == -1)
        return -1;
    *kind = type;
    return 0;
}

int
gin_window_decode(const char *component, const char *chantypes,
                  char *out, size_t capacity, enum gin_window_kind *kind)
{
    char target[257], body[257], encoded[256];
    const char *suffix = component;
    char prefix = '#';
    enum gin_window_kind type;
    if (strlen(component) == 0 || strlen(component) > 255)
        return invalid();
    if (*suffix == '@') {
        prefix = '\0';
        ++suffix;
    } else if (*suffix == '+') {
        if (strncmp(suffix, "+26", 3) == 0)
            prefix = '&';
        else if (strncmp(suffix, "+2B", 3) == 0)
            prefix = '+';
        else if (strncmp(suffix, "+21", 3) == 0)
            prefix = '!';
        else
            return invalid();
        suffix += 3;
    }
    if (decode_bytes(suffix, body, sizeof(body)) == -1)
        return -1;
    target[0] = prefix;
    if (copy_result(body, target + (prefix != '\0'),
                    sizeof(target) - (prefix != '\0')) == -1 ||
        gin_window_encode(target, chantypes, encoded, sizeof(encoded), &type) == -1)
        return -1;
    if (strcmp(component, encoded) != 0)
        return invalid();
    if (copy_result(target, out, capacity) == -1)
        return -1;
    *kind = type;
    return 0;
}

int
gin_name_prepare(const char *slug, const char *target, const char *chantypes,
                 struct gin_name *name)
{
    struct gin_name prepared;
    if (gin_network_encode(slug, prepared.network_component,
                           sizeof(prepared.network_component)) == -1 ||
        canonicalize(target, chantypes, prepared.canonical_target,
                     &prepared.kind) == -1 ||
        gin_window_encode(target, chantypes, prepared.window_component,
                          sizeof(prepared.window_component), &prepared.kind) == -1)
        return -1;
    memcpy(prepared.input_network_slug, slug, strlen(slug) + 1);
    memcpy(prepared.original_wire_target, target, strlen(target) + 1);
    *name = prepared;
    return 0;
}

int
gin_network_encode(const char *slug, char *out, size_t capacity)
{
    const unsigned char *p = (const unsigned char *)slug;
    size_t length = strlen(slug);
    if (length == 0 || length > 255)
        return invalid();
    for (; *p != 0; ++p)
        if (!readable(*p) && !(*p >= 'A' && *p <= 'Z'))
            return invalid();
    return encode_bytes(slug, 0, "", out, capacity);
}

int
gin_network_decode(const char *component, char *out, size_t capacity)
{
    char decoded[257], encoded[256];
    if (decode_bytes(component, decoded, sizeof(decoded)) == -1 ||
        gin_network_encode(decoded, encoded, sizeof(encoded)) == -1)
        return -1;
    if (strcmp(component, encoded) != 0)
        return invalid();
    return copy_result(decoded, out, capacity);
}
