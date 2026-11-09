/* Real-pipe bounded-state and output-capacity checks, not a runtime. */
#include "gin_input.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    struct guarded_input {
        unsigned long before;
        struct gin_input state;
        unsigned long after;
    } input = { 12345, GIN_INPUT_INITIALIZER, 67890 };
    struct guarded_record {
        unsigned long before;
        struct gin_record record;
        unsigned long after;
    } output;
    unsigned char data[128];
    int fd[2];
    size_t i;
    alarm(10);
    memset(&output, 0xa5, sizeof(output));
    output.before = 23456;
    output.after = 78901;
    assert(gin_input_next(&input.state, -1, &output.record) == GIN_INPUT_ERROR);
    assert(errno == EBADF && output.record.length == 0);
    assert(pipe(fd) == 0);
    assert(fcntl(fd[0], F_SETFL, O_NONBLOCK) == 0);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_AGAIN);
    memset(data, 'x', sizeof(data));
    for (i = 0; i < GIN_INPUT_LIMIT / sizeof(data); ++i) {
        assert(write(fd[1], data, sizeof(data)) == (ssize_t)sizeof(data));
        assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_MORE);
        assert(input.state.length == (i + 1) * sizeof(data));
    }
    assert(write(fd[1], "\n", 1) == 1);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_LINE);
    assert(output.record.length == GIN_INPUT_LIMIT);
    for (i = 0; i < output.record.length; ++i)
        assert(output.record.data[i] == 'x');
    /* Arbitrarily long discarded input cannot grow state or emit a prefix. */
    assert(write(fd[1], "\0", 1) == 1);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_MORE);
    for (i = 0; i < 100; ++i) {
        assert(write(fd[1], data, sizeof(data)) == (ssize_t)sizeof(data));
        assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_MORE);
        assert(input.state.length == 0 && input.state.discarding);
        assert(output.record.length == 0);
    }
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_AGAIN);
    assert(write(fd[1], "\nlast\n", 6) == 6);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_REJECTED);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_LINE);
    assert(output.record.length == 4 && memcmp(output.record.data, "last", 4) == 0);
    assert(close(fd[1]) == 0);
    assert(gin_input_next(&input.state, fd[0], &output.record) == GIN_INPUT_EOF);
    assert(close(fd[0]) == 0);
    assert(input.before == 12345 && input.after == 67890);
    assert(output.before == 23456 && output.after == 78901);
    puts("Input pipe/capacity tests passed.");
    return 0;
}
