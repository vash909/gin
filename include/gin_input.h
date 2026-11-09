#ifndef GIN_INPUT_H
#define GIN_INPUT_H

#include <stddef.h>

/* Local input policy, NOT a Grappa or PIPE_BUF limit. LF is not counted. */
#define GIN_INPUT_LIMIT 4096
#define GIN_INPUT_CHUNK 512

enum gin_input_result {
    GIN_INPUT_ERROR = -1,
    GIN_INPUT_MORE = 0,
    GIN_INPUT_LINE = 1,
    GIN_INPUT_EMPTY = 2,
    GIN_INPUT_REJECTED = 3,
    GIN_INPUT_AGAIN = 4,
    GIN_INPUT_INTERRUPTED = 5,
    GIN_INPUT_EOF = 6
};
struct gin_record {
    unsigned char data[GIN_INPUT_LIMIT];
    size_t length;
};
struct gin_input {
    unsigned char pending[GIN_INPUT_CHUNK];
    unsigned char line[GIN_INPUT_LIMIT];
    size_t position, available, length;
    int discarding;
};
#define GIN_INPUT_INITIALIZER { {0}, {0}, 0, 0, 0, 0 }

/* Non-NULL, disjoint caller-owned storage; initialize input once per fd.
 * fd must be a nonblocking read descriptor, exclusively consumed by this state.
 * At most one read and CHUNK bytes scanned per call, plus LIMIT bytes copied.
 * Output is length-aware, not NUL-terminated; valid only for LINE (EMPTY has 0).
 * Other results clear output length. No fd open/close, allocation, dispatch or
 * internal retry. MORE needs another bounded call; AGAIN/EINTR/EOF scheduling
 * belongs to the caller. See docs/INPUT.md for byte and writer contracts. */
enum gin_input_result gin_input_next(struct gin_input *input, int fd,
                                      struct gin_record *record);

#endif
