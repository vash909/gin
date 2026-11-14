#ifndef GIN_SESSION_OPERATION_H
#define GIN_SESSION_OPERATION_H
#include <stddef.h>
#include <stdint.h>

#define GIN_OPERATION_RECORD_MAX 192u
#define GIN_OPERATION_CODE_MAX 65535u

enum gin_operation_kind {
    GIN_OPERATION_INPUT, GIN_OPERATION_SEND, GIN_OPERATION_JOIN,
    GIN_OPERATION_PART, GIN_OPERATION_QUERY, GIN_OPERATION_KIND_COUNT
};
enum gin_operation_state {
    GIN_OPERATION_PENDING, GIN_OPERATION_REJECTED, GIN_OPERATION_NOT_ATTEMPTED,
    GIN_OPERATION_ACCEPTED_PERSISTED, GIN_OPERATION_ACCEPTED_NO_PERSIST,
    GIN_OPERATION_ACCEPTED_PENDING, GIN_OPERATION_ACCEPTED_ACK,
    GIN_OPERATION_REMOTE_FAILURE, GIN_OPERATION_NOT_SENT,
    GIN_OPERATION_UNCERTAIN, GIN_OPERATION_STATE_COUNT
};
struct gin_operation_event {
    uint64_t id;
    enum gin_operation_kind kind;
    enum gin_operation_state state;
    unsigned code;
};
/* Pure bounded formatting, no allocation/I/O/clock/global state. Nonzero id;
 * code<=CODE_MAX; enums and kind/state combinations must be valid. Grammar:
 * OP1 SP decimal-id SP KIND SP STATE SP decimal-code LF
 * Names are exactly the enum suffixes. INPUT permits only REJECTED. SEND:
 * PENDING/NOT_ATTEMPTED/ACCEPTED_PERSISTED/ACCEPTED_NO_PERSIST/REMOTE_FAILURE/
 * UNCERTAIN. JOIN/PART: PENDING/NOT_ATTEMPTED/ACCEPTED_PENDING/REMOTE_FAILURE/
 * UNCERTAIN. QUERY: PENDING/NOT_ATTEMPTED/ACCEPTED_ACK/REMOTE_FAILURE/NOT_SENT/
 * UNCERTAIN. Decimal has no sign/leading zero except code0. No payloads,
 * targets, credentials, URLs, refs, timestamps or delivery assertions.
 * Success returns1, copies at most RECORD_MAX bytes INCLUDING LF plus a NUL,
 * sets *length excluding NUL. Capacity must include NUL. Failure returns0 and
 * leaves output and *length unchanged. Event always unchanged. Caller supplies
 * valid, disjoint storage; NULL pointers are rejected. */
int gin_operation_format(const struct gin_operation_event *event,
    char *output, size_t capacity, size_t *length);
#endif
