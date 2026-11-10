#include "gin_phoenix.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

void gin_phoenix_refs_reset(struct gin_phoenix_refs *refs)
{
    if (!refs)
        return;
    refs->current = 0;
    refs->exhausted = 0;
}

enum gin_phoenix_code gin_phoenix_refs_next(struct gin_phoenix_refs *refs,
    char *out, size_t capacity)
{
    char local[GIN_PHOENIX_REF_CAP];
    uint64_t next;
    int written;

    if (!refs || !out || capacity < (size_t)GIN_PHOENIX_REF_CAP)
        return GIN_PHOENIX_INVALID;
    if (refs->exhausted != 0 || refs->current == UINT64_MAX)
        return GIN_PHOENIX_LIMIT;
    next = (uint64_t)(refs->current + 1u);
    written = snprintf(local, sizeof(local), "%" PRIu64, next);
    if (written < 0 || (size_t)written >= sizeof(local))
        return GIN_PHOENIX_INVALID;
    memcpy(out, local, (size_t)written + 1u);
    refs->current = next;
    if (next == UINT64_MAX)
        refs->exhausted = 1;
    return GIN_PHOENIX_OK;
}
