#include "connect_values.h"
#include <stddef.h>
#include <string.h>

static int gin_bounded_len(const char *s, size_t max, size_t *len)
{
    size_t i;
    for (i = 0; i <= max; i++) {
        if (s[i] == '\0') {
            *len = i;
            return 1;
        }
    }
    return 0;
}

static int gin_field_valid(const char *s, size_t len, int allow_slash)
{
    size_t i = 0;
    while (i < len) {
        unsigned char c = (unsigned char)s[i];
        unsigned long cp;
        unsigned char lo = 0x80, hi = 0xbf;
        size_t n, k;
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f)
                return 0;
            if (c == '/' && !allow_slash)
                return 0;
            i++;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf) {
            n = 2; cp = c & 0x1f;
        } else if (c == 0xe0) {
            n = 3; cp = c & 0x0f; lo = 0xa0;
        } else if ((c >= 0xe1 && c <= 0xec) || c == 0xee || c == 0xef) {
            n = 3; cp = c & 0x0f;
        } else if (c == 0xed) {
            n = 3; cp = c & 0x0f; hi = 0x9f;
        } else if (c == 0xf0) {
            n = 4; cp = c & 0x07; lo = 0x90;
        } else if (c >= 0xf1 && c <= 0xf3) {
            n = 4; cp = c & 0x07;
        } else if (c == 0xf4) {
            n = 4; cp = c & 0x07; hi = 0x8f;
        } else {
            return 0;
        }
        if (i + n > len)
            return 0;
        if ((unsigned char)s[i + 1] < lo || (unsigned char)s[i + 1] > hi)
            return 0;
        cp = (cp << 6) | ((unsigned char)s[i + 1] & 0x3f);
        for (k = 2; k < n; k++) {
            unsigned char cc = (unsigned char)s[i + k];
            if (cc < 0x80 || cc > 0xbf)
                return 0;
            cp = (cp << 6) | (cc & 0x3f);
        }
        if (cp >= 0x80 && cp <= 0x9f)
            return 0;
        i += n;
    }
    return 1;
}

enum gin_ws_value_code gin_ws_topic(const char *subject, const char *network,
    const char *target, char *out, size_t capacity, size_t *written)
{
    static const char p1[] = "grappa:user:";
    static const char p2[] = "/network:";
    static const char p3[] = "/channel:";
    size_t slen = 0, nlen = 0, tlen = 0, total, pos = 0, i;
    if (out == NULL || written == NULL)
        return GIN_WS_VALUE_INVALID;
    if (subject == NULL || (network == NULL && target != NULL))
        return GIN_WS_VALUE_INVALID;
    if (!gin_bounded_len(subject, 255u, &slen))
        return GIN_WS_VALUE_LIMIT;
    if (slen == 0 || !gin_field_valid(subject, slen, 0))
        return GIN_WS_VALUE_INVALID;
    if (network != NULL) {
        if (!gin_bounded_len(network, 255u, &nlen))
            return GIN_WS_VALUE_LIMIT;
        if (nlen == 0 || !gin_field_valid(network, nlen, 0))
            return GIN_WS_VALUE_INVALID;
    }
    if (target != NULL) {
        if (!gin_bounded_len(target, 256u, &tlen))
            return GIN_WS_VALUE_LIMIT;
        if (tlen == 0 || !gin_field_valid(target, tlen, 1))
            return GIN_WS_VALUE_INVALID;
    }
    total = (sizeof(p1) - 1u) + slen;
    if (network != NULL)
        total += (sizeof(p2) - 1u) + nlen;
    if (target != NULL)
        total += (sizeof(p3) - 1u) + tlen;
    if (total > (size_t)GIN_PHOENIX_TOPIC_MAX || capacity < total + 1u)
        return GIN_WS_VALUE_LIMIT;
    memcpy(out + pos, p1, sizeof(p1) - 1u); pos += sizeof(p1) - 1u;
    memcpy(out + pos, subject, slen); pos += slen;
    if (network != NULL) {
        memcpy(out + pos, p2, sizeof(p2) - 1u); pos += sizeof(p2) - 1u;
        memcpy(out + pos, network, nlen); pos += nlen;
    }
    if (target != NULL) {
        memcpy(out + pos, p3, sizeof(p3) - 1u); pos += sizeof(p3) - 1u;
        for (i = 0; i < tlen; i++) {
            char ch = target[i];
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            out[pos++] = ch;
        }
    }
    out[pos] = '\0';
    *written = pos;
    return GIN_WS_VALUE_OK;
}
