#include "connect_values.h"
#include <stddef.h>

static const char gin_b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int gin_token_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '~' || c == '+' || c == '/';
}

enum gin_ws_value_code gin_ws_protocols(const char *token, char *out,
    size_t capacity, size_t *written)
{
    static const char prefix[] = "phoenix,base64url.bearer.phx.";
    size_t len = 0, i, pos = 0, enc_len, total;
    int found = 0, padding = 0;
    if (out == NULL || written == NULL)
        return GIN_WS_VALUE_INVALID;
    if (token == NULL)
        return GIN_WS_VALUE_INVALID;
    for (i = 0; i <= (size_t)GIN_CREDENTIAL_TOKEN_MAX; i++) {
        if (token[i] == '\0') {
            len = i;
            found = 1;
            break;
        }
    }
    if (!found)
        return GIN_WS_VALUE_LIMIT;
    if (len == 0 || token[0] == '=')
        return GIN_WS_VALUE_INVALID;
    for (i = 0; i < len; i++) {
        if (token[i] == '=') {
            padding = 1;
        } else if (padding || !gin_token_char(token[i])) {
            return GIN_WS_VALUE_INVALID;
        }
    }
    enc_len = ((len + 2u) / 3u) * 4u;
    if (len % 3u == 1u)
        enc_len -= 2u;
    else if (len % 3u == 2u)
        enc_len -= 1u;
    total = (sizeof(prefix) - 1u) + enc_len;
    if (capacity < total + 1u)
        return GIN_WS_VALUE_LIMIT;
    for (i = 0; i < sizeof(prefix) - 1u; i++)
        out[pos++] = prefix[i];
    for (i = 0; i < len; i += 3u) {
        size_t rem = len - i;
        unsigned long v = (unsigned long)(unsigned char)token[i] << 16;
        if (rem > 1u)
            v |= (unsigned long)(unsigned char)token[i + 1u] << 8;
        if (rem > 2u)
            v |= (unsigned long)(unsigned char)token[i + 2u];
        out[pos++] = gin_b64[(v >> 18) & 63u];
        out[pos++] = gin_b64[(v >> 12) & 63u];
        if (rem > 1u)
            out[pos++] = gin_b64[(v >> 6) & 63u];
        if (rem > 2u)
            out[pos++] = gin_b64[v & 63u];
    }
    out[pos] = '\0';
    *written = pos;
    return GIN_WS_VALUE_OK;
}
