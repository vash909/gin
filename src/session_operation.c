#include "session_operation.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *const kKinds[GIN_OPERATION_KIND_COUNT] = {
    "INPUT",
    "SEND",
    "JOIN",
    "PART",
    "QUERY"
};

static const char *const kStates[GIN_OPERATION_STATE_COUNT] = {
    "PENDING",
    "REJECTED",
    "NOT_ATTEMPTED",
    "ACCEPTED_PERSISTED",
    "ACCEPTED_NO_PERSIST",
    "ACCEPTED_PENDING",
    "ACCEPTED_ACK",
    "REMOTE_FAILURE",
    "NOT_SENT",
    "UNCERTAIN"
};

static const unsigned char kAllowed[GIN_OPERATION_KIND_COUNT][GIN_OPERATION_STATE_COUNT] = {
    {0,1,0,0,0,0,0,0,0,0},
    {1,0,1,1,1,0,0,1,0,1},
    {1,0,1,0,0,1,0,1,0,1},
    {1,0,1,0,0,1,0,1,0,1},
    {1,0,1,0,0,0,1,1,1,1}
};

int gin_operation_format(const struct gin_operation_event *event,
    char *output, size_t capacity, size_t *length)
{
    char tmp[GIN_OPERATION_RECORD_MAX + 1u];
    int n;
    size_t len;

    if (event == NULL || output == NULL || length == NULL) {
        return 0;
    }
    if (event->id == 0) {
        return 0;
    }
    if (event->code > GIN_OPERATION_CODE_MAX) {
        return 0;
    }
    if ((int)event->kind < 0 || event->kind >= GIN_OPERATION_KIND_COUNT) {
        return 0;
    }
    if ((int)event->state < 0 || event->state >= GIN_OPERATION_STATE_COUNT) {
        return 0;
    }
    if (!kAllowed[event->kind][event->state]) {
        return 0;
    }

    n = snprintf(tmp, sizeof(tmp), "OP1 %" PRIu64 " %s %s %u\n",
        event->id,
        kKinds[event->kind],
        kStates[event->state],
        event->code);
    if (n < 0) {
        return 0;
    }
    if ((size_t)n >= sizeof(tmp)) {
        return 0;
    }
    len = (size_t)n;
    if (len == 0 || len > GIN_OPERATION_RECORD_MAX) {
        return 0;
    }
    if (capacity < len + 1u) {
        return 0;
    }
    memcpy(output, tmp, len + 1u);
    *length = len;
    return 1;
}
