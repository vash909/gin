#include "gin_input.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>

enum gin_input_result
gin_input_next(struct gin_input *input, int fd, struct gin_record *record)
{
    record->length = 0;
    if (input->position == input->available) {
        ssize_t count = read(fd, input->pending, sizeof(input->pending));
        if (count == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return GIN_INPUT_AGAIN;
            if (errno == EINTR)
                return GIN_INPUT_INTERRUPTED;
            return GIN_INPUT_ERROR;
        }
        if (count == 0) {
            int rejected = input->length != 0 || input->discarding;
            input->length = 0;
            input->discarding = 0;
            return rejected ? GIN_INPUT_REJECTED : GIN_INPUT_EOF;
        }
        input->position = 0;
        input->available = (size_t)count;
    }
    while (input->position < input->available) {
        unsigned char byte = input->pending[input->position++];
        if (byte == '\n') {
            int rejected = input->discarding;
            if (!rejected) {
                record->length = input->length;
                memcpy(record->data, input->line, input->length);
            }
            input->length = 0;
            input->discarding = 0;
            if (rejected)
                return GIN_INPUT_REJECTED;
            return record->length == 0 ? GIN_INPUT_EMPTY : GIN_INPUT_LINE;
        }
        if (input->discarding)
            continue;
        if (byte < 32 || byte == 127 || input->length == GIN_INPUT_LIMIT) {
            input->discarding = 1;
            input->length = 0;
        } else {
            input->line[input->length++] = byte;
        }
    }
    return GIN_INPUT_MORE;
}
