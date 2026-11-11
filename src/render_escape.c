#include "render_values.h"

static int gin_is_continuation(unsigned char c)
{
    return (c >= 0x80u && c <= 0xBFu);
}

static int gin_is_selected_escape(uint32_t cp)
{
    if (cp == 0x061Cu) {
        return 1;
    }
    if (cp == 0x200Eu || cp == 0x200Fu) {
        return 1;
    }
    if (cp == 0x2028u || cp == 0x2029u) {
        return 1;
    }
    if (cp >= 0x202Au && cp <= 0x202Eu) {
        return 1;
    }
    if (cp >= 0x2066u && cp <= 0x2069u) {
        return 1;
    }
    return 0;
}

static int gin_decode(const unsigned char *input, size_t length, size_t pos,
    uint32_t *cp_out, size_t *seq_len_out)
{
    unsigned char c = input[pos];
    size_t remaining = length - pos;
    if (c <= 0x7Fu) {
        *cp_out = (uint32_t)c;
        *seq_len_out = 1u;
        return 1;
    }
    if (c >= 0xC2u && c <= 0xDFu) {
        unsigned char c1;
        if (remaining < 2u) {
            return 0;
        }
        c1 = input[pos + 1u];
        if (!gin_is_continuation(c1)) {
            return 0;
        }
        *cp_out = ((uint32_t)(c & 0x1Fu) << 6) | (uint32_t)(c1 & 0x3Fu);
        *seq_len_out = 2u;
        return 1;
    }
    if (c >= 0xE0u && c <= 0xEFu) {
        unsigned char c1;
        unsigned char c2;
        if (remaining < 3u) {
            return 0;
        }
        c1 = input[pos + 1u];
        c2 = input[pos + 2u];
        if (!gin_is_continuation(c2)) {
            return 0;
        }
        if (c == 0xE0u) {
            if (c1 < 0xA0u || c1 > 0xBFu) {
                return 0;
            }
        } else if (c == 0xEDu) {
            if (c1 < 0x80u || c1 > 0x9Fu) {
                return 0;
            }
        } else {
            if (!gin_is_continuation(c1)) {
                return 0;
            }
        }
        *cp_out = ((uint32_t)(c & 0x0Fu) << 12) |
            ((uint32_t)(c1 & 0x3Fu) << 6) | (uint32_t)(c2 & 0x3Fu);
        *seq_len_out = 3u;
        return 1;
    }
    if (c >= 0xF0u && c <= 0xF4u) {
        unsigned char c1;
        unsigned char c2;
        unsigned char c3;
        if (remaining < 4u) {
            return 0;
        }
        c1 = input[pos + 1u];
        c2 = input[pos + 2u];
        c3 = input[pos + 3u];
        if (!gin_is_continuation(c2) || !gin_is_continuation(c3)) {
            return 0;
        }
        if (c == 0xF0u) {
            if (c1 < 0x90u || c1 > 0xBFu) {
                return 0;
            }
        } else if (c == 0xF4u) {
            if (c1 < 0x80u || c1 > 0x8Fu) {
                return 0;
            }
        } else {
            if (!gin_is_continuation(c1)) {
                return 0;
            }
        }
        *cp_out = ((uint32_t)(c & 0x07u) << 18) |
            ((uint32_t)(c1 & 0x3Fu) << 12) |
            ((uint32_t)(c2 & 0x3Fu) << 6) | (uint32_t)(c3 & 0x3Fu);
        *seq_len_out = 4u;
        return 1;
    }
    return 0;
}

static size_t gin_escaped_length(uint32_t cp, size_t seq_len)
{
    if (seq_len == 1u) {
        if (cp == 0x22u || cp == 0x5Cu) {
            return 2u;
        }
        if (cp < 0x20u || cp == 0x7Fu) {
            return 4u;
        }
        return 1u;
    }
    if (cp >= 0x80u && cp <= 0x9Fu) {
        return 6u;
    }
    if (gin_is_selected_escape(cp)) {
        return 6u;
    }
    return seq_len;
}

enum gin_render_value_code gin_render_escape(const unsigned char *input,
    size_t length, char *output, size_t capacity, size_t *written)
{
    static const char hex_digits[] = "0123456789ABCDEF";
    size_t pos;
    size_t needed;
    size_t out_pos;
    if (output == NULL || written == NULL) {
        return GIN_RENDER_VALUE_INVALID;
    }
    if (length > (size_t)GIN_RENDER_ESCAPE_INPUT_MAX) {
        return GIN_RENDER_VALUE_LIMIT;
    }
    if (input == NULL && length != 0u) {
        return GIN_RENDER_VALUE_INVALID;
    }
    needed = 0u;
    pos = 0u;
    while (pos < length) {
        uint32_t cp;
        size_t seq_len;
        size_t esc_len;
        if (!gin_decode(input, length, pos, &cp, &seq_len)) {
            return GIN_RENDER_VALUE_INVALID;
        }
        esc_len = gin_escaped_length(cp, seq_len);
        if (esc_len > (size_t)-1 - needed) {
            return GIN_RENDER_VALUE_LIMIT;
        }
        needed += esc_len;
        pos += seq_len;
    }
    if (needed == (size_t)-1) {
        return GIN_RENDER_VALUE_LIMIT;
    }
    if (capacity < needed + 1u) {
        return GIN_RENDER_VALUE_LIMIT;
    }
    out_pos = 0u;
    pos = 0u;
    while (pos < length) {
        uint32_t cp;
        size_t seq_len;
        size_t k;
        (void)gin_decode(input, length, pos, &cp, &seq_len);
        if (seq_len == 1u && (cp == 0x22u || cp == 0x5Cu)) {
            output[out_pos++] = (char)0x5C;
            output[out_pos++] = (char)cp;
        } else if (seq_len == 1u && (cp < 0x20u || cp == 0x7Fu)) {
            output[out_pos++] = (char)0x5C;
            output[out_pos++] = 'x';
            output[out_pos++] = hex_digits[(cp >> 4) & 0xFu];
            output[out_pos++] = hex_digits[cp & 0xFu];
        } else if (seq_len != 1u &&
            ((cp >= 0x80u && cp <= 0x9Fu) || gin_is_selected_escape(cp))) {
            output[out_pos++] = (char)0x5C;
            output[out_pos++] = 'u';
            output[out_pos++] = hex_digits[(cp >> 12) & 0xFu];
            output[out_pos++] = hex_digits[(cp >> 8) & 0xFu];
            output[out_pos++] = hex_digits[(cp >> 4) & 0xFu];
            output[out_pos++] = hex_digits[cp & 0xFu];
        } else {
            for (k = 0u; k < seq_len; k++) {
                output[out_pos++] = (char)input[pos + k];
            }
        }
        pos += seq_len;
    }
    output[out_pos] = '\0';
    *written = out_pos;
    return GIN_RENDER_VALUE_OK;
}
