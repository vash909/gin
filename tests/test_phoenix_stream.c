/* Real collector + real codec integration, with test-only stream allocation faults. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_phoenix.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_calloc, fail_realloc;
static void *stream_calloc(size_t n, size_t size)
{ return fail_calloc ? NULL : calloc(n, size); }
static void *stream_realloc(void *p, size_t n)
{ return fail_realloc ? NULL : realloc(p, n); }
#define calloc stream_calloc
#define realloc stream_realloc
#include "../src/phoenix_stream.c"
#undef calloc
#undef realloc

static const unsigned char message[] =
    "[\"01\",\"2\",\"grappa:user:fixture\",\"phx_reply\","
    "{\"status\":\"ok\",\"response\":{\"text\":\"\xc3\xa9 \xf0\x9f\x98\x80 \\uD83D\\uDE00 \\\" \\\\ []{}\"}}]";
static void check_frame(struct gin_phoenix_frame *f)
{
    const struct gin_phoenix_fields *fields = gin_phoenix_frame_fields(f);
    const struct gin_phoenix_reply *reply = gin_phoenix_frame_reply(f);
    assert(fields && !strcmp(fields->join_ref, "01") && !strcmp(fields->ref, "2"));
    assert(reply && reply->status == GIN_PHOENIX_REPLY_OK && reply->response.length > 2);
    assert(gin_phoenix_reply_matches(f, "grappa:user:fixture", "01", "2"));
    assert(!gin_phoenix_reply_matches(f, "grappa:user:fixture", "1", "2"));
}
static void complete(struct gin_phoenix_stream *s)
{
    struct gin_phoenix_frame *f = NULL;
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 1, &f) == GIN_PHOENIX_OK);
    check_frame(f); gin_phoenix_frame_free(f);
}
static void splits(void)
{
    struct gin_phoenix_stream *s = NULL;
    struct gin_phoenix_frame *f = NULL;
    size_t i;
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_OK);
    for (i = 0; i <= sizeof(message)-1; i++) {
        assert(gin_phoenix_stream_feed(s, message, i, 0, &f) == GIN_PHOENIX_NEED_MORE && !f);
        assert(gin_phoenix_stream_feed(s, message+i, sizeof(message)-1-i, 1, &f) == GIN_PHOENIX_OK);
        check_frame(f); gin_phoenix_frame_free(f); f = NULL;
    }
    for (i = 0; i < sizeof(message)-1; i++)
        assert(gin_phoenix_stream_feed(s, message+i, 1, 0, &f) == GIN_PHOENIX_NEED_MORE && !f);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, &f) == GIN_PHOENIX_OK);
    check_frame(f); gin_phoenix_frame_free(f); f = NULL;
    assert(gin_phoenix_stream_feed(s, NULL, 0, 0, &f) == GIN_PHOENIX_NEED_MORE && !f);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, &f) == GIN_PHOENIX_MALFORMED && !f);
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 0, &f) == GIN_PHOENIX_NEED_MORE && !f);
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 1, &f) == GIN_PHOENIX_MALFORMED && !f);
    complete(s);
    assert(gin_phoenix_stream_feed(s, message, 10, 0, &f) == GIN_PHOENIX_NEED_MORE);
    gin_phoenix_stream_reset(s); complete(s);
    assert(gin_phoenix_stream_feed(s, message, 10, 1, &f) == GIN_PHOENIX_MALFORMED && !f);
    complete(s);
    gin_phoenix_stream_free(s);
}
static void local_errors(void)
{
    struct gin_phoenix_stream *s = NULL, *same;
    struct gin_phoenix_frame *f = NULL, *occupied = NULL;
    assert(gin_phoenix_stream_new(NULL) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_OK); same = s;
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_INVALID && s == same);
    assert(gin_phoenix_decode(message, sizeof(message)-1, &occupied) == GIN_PHOENIX_OK);
    assert(gin_phoenix_stream_feed(s, message, 10, 0, &f) == GIN_PHOENIX_NEED_MORE);
    assert(gin_phoenix_stream_feed(s, NULL, 1, 1, &f) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 2, &f) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_feed(s, NULL, 0, -1, &f) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, NULL) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, &occupied) == GIN_PHOENIX_INVALID);
    check_frame(occupied); gin_phoenix_frame_free(occupied);
    assert(gin_phoenix_stream_feed(s, message+10, sizeof(message)-1-10, 1, &f) == GIN_PHOENIX_OK);
    check_frame(f); gin_phoenix_frame_free(f);
    gin_phoenix_stream_free(s); gin_phoenix_stream_free(NULL); gin_phoenix_stream_reset(NULL);
}
static void dropping(void)
{
    struct gin_phoenix_stream *s = NULL;
    struct gin_phoenix_frame *f = NULL;
    unsigned char *large = malloc(GIN_PHOENIX_BODY_MAX+1u);
    unsigned char chunk[300];
    size_t i;
    assert(large); memset(large, ' ', GIN_PHOENIX_BODY_MAX+1u); memset(chunk, ' ', sizeof(chunk));
    fail_calloc = 1;
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_NOMEM && !s);
    fail_calloc = 0;
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_OK);
    assert(gin_phoenix_stream_feed(s, message, 10, 0, &f) == GIN_PHOENIX_NEED_MORE);
    fail_realloc = 1;
    assert(gin_phoenix_stream_feed(s, chunk, sizeof(chunk), 0, &f) == GIN_PHOENIX_NOMEM && !f);
    fail_realloc = 0;
    assert(!s->length && s->dropping);
    for (i = 0; i < 10; i++) assert(!s->buffer[i]);
    assert(gin_phoenix_stream_feed(s, NULL, 1, 1, &f) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 0, &f) == GIN_PHOENIX_NOMEM && !f);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, &f) == GIN_PHOENIX_NOMEM && !f);
    complete(s);
    gin_phoenix_stream_free(s); s = NULL;
    assert(gin_phoenix_stream_new(&s) == GIN_PHOENIX_OK);
    fail_realloc = 1;
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 1, &f) == GIN_PHOENIX_NOMEM && !f);
    fail_realloc = 0; complete(s);
    assert(gin_phoenix_stream_feed(s, large, GIN_PHOENIX_BODY_MAX+1u, 0, &f) == GIN_PHOENIX_LIMIT && !f);
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 0, &f) == GIN_PHOENIX_LIMIT && !f);
    assert(gin_phoenix_stream_feed(s, message, sizeof(message)-1, 1, &f) == GIN_PHOENIX_LIMIT && !f);
    complete(s);
    assert(gin_phoenix_stream_feed(s, large, GIN_PHOENIX_BODY_MAX+1u, 1, &f) == GIN_PHOENIX_LIMIT && !f);
    complete(s);
    assert(gin_phoenix_stream_feed(s, large, GIN_PHOENIX_BODY_MAX, 0, &f) == GIN_PHOENIX_NEED_MORE);
    assert(s->capacity <= GIN_PHOENIX_BODY_MAX);
    assert(gin_phoenix_stream_feed(s, message, 1, 0, &f) == GIN_PHOENIX_LIMIT);
    assert(gin_phoenix_stream_feed(s, NULL, 0, 1, &f) == GIN_PHOENIX_LIMIT);
    complete(s);
    assert(gin_phoenix_stream_feed(s, large, GIN_PHOENIX_BODY_MAX+1u, 0, &f) == GIN_PHOENIX_LIMIT);
    gin_phoenix_stream_reset(s); complete(s);
    gin_phoenix_stream_free(s); free(large);
}
int main(void)
{
    splits(); local_errors(); dropping();
    puts("Phoenix real collector/codec and allocation-fault tests passed.");
    return 0;
}
