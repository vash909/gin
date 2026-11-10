/* Collector-only contract facade; real codec integration has a separate unit. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_phoenix.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const unsigned char golden[] =
    "[null,\"1\",\"grappa:user:fixture\",\"event\",{\"text\":\"\xc3\xa9\"}]";
static size_t decode_calls;
struct gin_phoenix_frame { int marker; };
/* Test adapter only: checks assembly byte-for-byte, not JSON/schema validity. */
enum gin_phoenix_code gin_phoenix_decode(const unsigned char *data, size_t length,
    struct gin_phoenix_frame **out)
{
    decode_calls++;
    assert(out && !*out);
    if (length != sizeof(golden)-1 || !data || memcmp(data, golden, length))
        return GIN_PHOENIX_MALFORMED;
    *out = malloc(sizeof(**out));
    assert(*out);
    (*out)->marker = 1;
    return GIN_PHOENIX_OK;
}
void gin_phoenix_frame_free(struct gin_phoenix_frame *frame) { free(frame); }

int main(void)
{
    struct gin_phoenix_stream *stream = NULL;
    struct gin_phoenix_frame *out = NULL;
    assert(gin_phoenix_stream_new(&stream) == GIN_PHOENIX_OK && stream);
    assert(gin_phoenix_stream_feed(stream, golden, sizeof(golden)-1, 0, &out) == GIN_PHOENIX_NEED_MORE);
    assert(!out && decode_calls == 0);
    assert(gin_phoenix_stream_feed(stream, NULL, 0, 1, &out) == GIN_PHOENIX_OK);
    assert(out && out->marker == 1 && decode_calls == 1);
    gin_phoenix_frame_free(out); out = NULL;
    {
        size_t n = sizeof(golden)-1, i, before;
        for (i = 0; i <= n; i++) {
            before = decode_calls;
            assert(gin_phoenix_stream_feed(stream, golden, i, 0, &out) == GIN_PHOENIX_NEED_MORE);
            assert(!out && decode_calls == before);
            assert(gin_phoenix_stream_feed(stream, golden+i, n-i, 1, &out) == GIN_PHOENIX_OK);
            assert(out && decode_calls == before+1);
            gin_phoenix_frame_free(out); out = NULL;
        }
        before = decode_calls;
        for (i = 0; i < n; i++) {
            assert(gin_phoenix_stream_feed(stream, golden+i, 1, i+1 == n, &out) ==
                (i+1 == n ? GIN_PHOENIX_OK : GIN_PHOENIX_NEED_MORE));
            assert(decode_calls == before + (i+1 == n));
        }
        gin_phoenix_frame_free(out); out = NULL;
        before = decode_calls;
        assert(gin_phoenix_stream_feed(stream, golden, n/2, 0, &out) == GIN_PHOENIX_NEED_MORE);
        assert(gin_phoenix_stream_feed(stream, NULL, 1, 0, &out) == GIN_PHOENIX_INVALID);
        assert(gin_phoenix_stream_feed(stream, golden+n/2, n-n/2, 2, &out) == GIN_PHOENIX_INVALID);
        assert(gin_phoenix_stream_feed(stream, golden, n, 1, NULL) == GIN_PHOENIX_INVALID);
        assert(gin_phoenix_stream_feed(NULL, golden, n, 1, &out) == GIN_PHOENIX_INVALID);
        out = malloc(sizeof(*out)); assert(out); out->marker = 99;
        assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_INVALID);
        assert(out->marker == 99 && decode_calls == before);
        gin_phoenix_frame_free(out); out = NULL;
        assert(gin_phoenix_stream_feed(stream, golden+n/2, n-n/2, 1, &out) == GIN_PHOENIX_OK);
        gin_phoenix_frame_free(out); out = NULL;
        assert(gin_phoenix_stream_feed(stream, golden, n/2, 0, &out) == GIN_PHOENIX_NEED_MORE);
        gin_phoenix_stream_reset(stream);
        assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_OK);
        gin_phoenix_frame_free(out); out = NULL;
        assert(gin_phoenix_stream_feed(stream, NULL, 0, 0, &out) == GIN_PHOENIX_NEED_MORE);
        assert(gin_phoenix_stream_feed(stream, NULL, 0, 1, &out) == GIN_PHOENIX_MALFORMED && !out);
        assert(gin_phoenix_stream_feed(stream, (const unsigned char *)"{}", 2, 1, &out) == GIN_PHOENIX_MALFORMED);
        assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_OK);
        gin_phoenix_frame_free(out); out = NULL;
        {
            unsigned char *large = malloc((size_t)GIN_PHOENIX_BODY_MAX+1);
            assert(large); memset(large, 'x', (size_t)GIN_PHOENIX_BODY_MAX+1);
            before = decode_calls;
            assert(gin_phoenix_stream_feed(stream, golden, n/2, 0, &out) == GIN_PHOENIX_NEED_MORE);
            assert(gin_phoenix_stream_feed(stream, large, (size_t)GIN_PHOENIX_BODY_MAX+1, 0, &out) == GIN_PHOENIX_LIMIT);
            assert(gin_phoenix_stream_feed(stream, golden, n, 0, &out) == GIN_PHOENIX_LIMIT);
            assert(gin_phoenix_stream_feed(stream, NULL, 0, 2, &out) == GIN_PHOENIX_INVALID);
            assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_LIMIT);
            assert(!out && decode_calls == before);
            assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_OK);
            gin_phoenix_frame_free(out); out = NULL;
            assert(gin_phoenix_stream_feed(stream, large, (size_t)GIN_PHOENIX_BODY_MAX+1, 1, &out) == GIN_PHOENIX_LIMIT);
            assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_OK);
            gin_phoenix_frame_free(out); out = NULL;
            assert(gin_phoenix_stream_feed(stream, large, (size_t)GIN_PHOENIX_BODY_MAX+1, 0, &out) == GIN_PHOENIX_LIMIT);
            assert(gin_phoenix_stream_feed(stream, NULL, 0, 1, &out) == GIN_PHOENIX_LIMIT);
            assert(gin_phoenix_stream_feed(stream, golden, n, 1, &out) == GIN_PHOENIX_OK);
            gin_phoenix_frame_free(out); out = NULL;
            memcpy(large, golden, n); memset(large+n, ' ', (size_t)GIN_PHOENIX_BODY_MAX-n);
            before = decode_calls;
            /* Facade rejects padded bytes; reaching it proves exact cap admitted. */
            assert(gin_phoenix_stream_feed(stream, large, GIN_PHOENIX_BODY_MAX, 1, &out) == GIN_PHOENIX_MALFORMED);
            assert(!out && decode_calls == before+1);
            free(large);
        }
    }
    {
        struct gin_phoenix_stream *occupied = stream;
        assert(gin_phoenix_stream_new(&occupied) == GIN_PHOENIX_INVALID && occupied == stream);
        assert(gin_phoenix_stream_new(NULL) == GIN_PHOENIX_INVALID);
    }
    gin_phoenix_stream_reset(NULL); gin_phoenix_stream_free(NULL);
    gin_phoenix_stream_free(stream);
    puts("Phoenix collector boundary, split, discard and recovery controls passed");
    return 0;
}
