#include "gin_phoenix.h"
#include <stdlib.h>
#include <string.h>

struct gin_phoenix_stream {
    unsigned char *buffer;
    size_t length;
    size_t capacity;
    int dropping;
    enum gin_phoenix_code drop_code;
};

static void wipe(void *data, size_t length)
{
    volatile unsigned char *p = data;
    while (length--) *p++ = 0;
}

static void wipe_pending(struct gin_phoenix_stream *stream)
{
    if (stream->buffer && stream->length > 0)
        wipe(stream->buffer, stream->length);
    stream->length = 0;
}

static void clear_drop(struct gin_phoenix_stream *stream)
{
    stream->dropping = 0;
    stream->drop_code = GIN_PHOENIX_OK;
}

enum gin_phoenix_code gin_phoenix_stream_new(struct gin_phoenix_stream **out)
{
    struct gin_phoenix_stream *stream;
    if (!out || *out)
        return GIN_PHOENIX_INVALID;
    stream = (struct gin_phoenix_stream *)calloc(1, sizeof(*stream));
    if (!stream)
        return GIN_PHOENIX_NOMEM;
    stream->drop_code = GIN_PHOENIX_OK;
    *out = stream;
    return GIN_PHOENIX_OK;
}

void gin_phoenix_stream_free(struct gin_phoenix_stream *stream)
{
    if (!stream)
        return;
    if (stream->buffer) {
        if (stream->capacity > 0)
            wipe(stream->buffer, stream->capacity);
        free(stream->buffer);
    }
    wipe(stream, sizeof(*stream));
    free(stream);
}

void gin_phoenix_stream_reset(struct gin_phoenix_stream *stream)
{
    if (!stream)
        return;
    wipe_pending(stream);
    clear_drop(stream);
}

enum gin_phoenix_code gin_phoenix_stream_feed(struct gin_phoenix_stream *stream,
    const unsigned char *data, size_t length, int final,
    struct gin_phoenix_frame **out)
{
    enum gin_phoenix_code code;
    size_t needed;
    size_t new_capacity;
    unsigned char *new_buffer;
    const unsigned char *whole;
    size_t whole_length;

    if (!stream || !out || *out || (final != 0 && final != 1) || (!data && length > 0))
        return GIN_PHOENIX_INVALID;

    if (stream->dropping) {
        code = stream->drop_code;
        if (final) {
            wipe_pending(stream);
            clear_drop(stream);
        }
        return code;
    }

    if (length > (size_t)GIN_PHOENIX_BODY_MAX - stream->length) {
        wipe_pending(stream);
        if (final)
            return GIN_PHOENIX_LIMIT;
        stream->dropping = 1;
        stream->drop_code = GIN_PHOENIX_LIMIT;
        return GIN_PHOENIX_LIMIT;
    }

    if (length > 0) {
        needed = stream->length + length;
        if (needed > stream->capacity) {
            new_capacity = stream->capacity ? stream->capacity : 256u;
            while (new_capacity < needed) {
                if (new_capacity > (size_t)GIN_PHOENIX_BODY_MAX / 2u)
                    new_capacity = (size_t)GIN_PHOENIX_BODY_MAX;
                else
                    new_capacity *= 2u;
            }
            new_buffer = (unsigned char *)realloc(stream->buffer, new_capacity);
            if (!new_buffer) {
                wipe_pending(stream);
                if (final)
                    return GIN_PHOENIX_NOMEM;
                stream->dropping = 1;
                stream->drop_code = GIN_PHOENIX_NOMEM;
                return GIN_PHOENIX_NOMEM;
            }
            stream->buffer = new_buffer;
            stream->capacity = new_capacity;
        }
        memcpy(stream->buffer + stream->length, data, length);
        stream->length = needed;
    }

    if (!final)
        return GIN_PHOENIX_NEED_MORE;

    whole_length = stream->length;
    whole = whole_length ? stream->buffer : NULL;
    code = gin_phoenix_decode(whole, whole_length, out);
    wipe_pending(stream);
    clear_drop(stream);
    return code;
}
