#include "history_values.h"
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdio.h>

static int is_unreserved(unsigned char c) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        return 1;
    if (c == '-' || c == '.' || c == '_' || c == '~')
        return 1;
    return 0;
}

static int encode_segment(const char *src, char *dst, size_t dst_cap, size_t *pos) {
    size_t i = 0;
    while (src[i] != '\0') {
        unsigned char b = (unsigned char)src[i];
        if (is_unreserved(b)) {
            if (*pos + 1 >= dst_cap)
                return 0;
            dst[*pos] = (char)b;
            (*pos)++;
        } else {
            if (*pos + 3 >= dst_cap)
                return 0;
            dst[*pos] = '%';
            dst[*pos + 1] = "0123456789ABCDEF"[(b >> 4) & 0xF];
            dst[*pos + 2] = "0123456789ABCDEF"[b & 0xF];
            *pos += 3;
        }
        i++;
    }
    return 1;
}

static int append_str(const char *s, char *dst, size_t dst_cap, size_t *pos) {
    size_t len = strlen(s);
    if (*pos + len >= dst_cap)
        return 0;
    memcpy(dst + *pos, s, len);
    *pos += len;
    return 1;
}

enum gin_history_code gin_history_path(const struct gin_history_scope *scope, const struct gin_history_request *request, char *output, size_t capacity, size_t *written) {
    if (output == NULL || written == NULL)
        return GIN_HISTORY_INVALID;

    {
        enum gin_history_code sc = gin_history_scope_check(scope);
        if (sc != GIN_HISTORY_OK)
            return sc;
    }
    {
        enum gin_history_code rc = gin_history_request_check(request);
        if (rc != GIN_HISTORY_OK)
            return rc;
    }

    /* capacity includes NUL */
    if (capacity == 0)
        return GIN_HISTORY_LIMIT;

    /* Use local buffer GIN_HISTORY_PATH_MAX+1, publish only after full success */
    char buf[GIN_HISTORY_PATH_MAX + 1];
    size_t pos = 0;
    size_t cap = sizeof(buf);

    if (!append_str("/networks/", buf, cap, &pos))
        return GIN_HISTORY_LIMIT;
    if (!encode_segment(scope->network_slug, buf, cap, &pos))
        return GIN_HISTORY_LIMIT;
    if (!append_str("/channels/", buf, cap, &pos))
        return GIN_HISTORY_LIMIT;
    if (!encode_segment(scope->target, buf, cap, &pos))
        return GIN_HISTORY_LIMIT;
    if (!append_str("/messages?limit=", buf, cap, &pos))
        return GIN_HISTORY_LIMIT;

    /* limit 1..200 already validated by gin_history_request_check, format decimal */
    {
        char numbuf[32];
        int n = 0;
        /* use snprintf with portable formatting */
        /* limit is unsigned, fits in small buffer */
        n = snprintf(numbuf, sizeof(numbuf), "%u", request->limit);
        if (n < 0 || (size_t)n >= sizeof(numbuf))
            return GIN_HISTORY_LIMIT;
        if (!append_str(numbuf, buf, cap, &pos))
            return GIN_HISTORY_LIMIT;
    }

    if (request->cursor == GIN_HISTORY_BEFORE) {
        char numbuf[32];
        int n;
        if (!append_str("&before=", buf, cap, &pos))
            return GIN_HISTORY_LIMIT;
        n = snprintf(numbuf, sizeof(numbuf), "%" PRId64, request->anchor);
        if (n < 0 || (size_t)n >= sizeof(numbuf))
            return GIN_HISTORY_LIMIT;
        if (!append_str(numbuf, buf, cap, &pos))
            return GIN_HISTORY_LIMIT;
    } else if (request->cursor == GIN_HISTORY_AFTER) {
        char numbuf[32];
        int n;
        if (!append_str("&after=", buf, cap, &pos))
            return GIN_HISTORY_LIMIT;
        n = snprintf(numbuf, sizeof(numbuf), "%" PRId64, request->anchor);
        if (n < 0 || (size_t)n >= sizeof(numbuf))
            return GIN_HISTORY_LIMIT;
        if (!append_str(numbuf, buf, cap, &pos))
            return GIN_HISTORY_LIMIT;
    } else {
        /* GIN_HISTORY_LATEST: no anchor param */
    }

    /* NUL terminate local buffer */
    if (pos >= cap)
        return GIN_HISTORY_LIMIT;
    buf[pos] = '\0';

    /* Check output capacity includes NUL; on any error output bytes and *written unchanged already ensured */
    {
        size_t needed = pos + 1;
        if (needed > capacity)
            return GIN_HISTORY_LIMIT;
        if (needed > GIN_HISTORY_PATH_MAX + 1)
            return GIN_HISTORY_LIMIT;
        memcpy(output, buf, needed);
        *written = pos;
    }

    return GIN_HISTORY_OK;
}
