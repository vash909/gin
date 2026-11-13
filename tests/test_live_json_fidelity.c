#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_phoenix.h"
#include "gin_render.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t calls, failure = (size_t)-1;
static void *fail_malloc(size_t n)
{ if (calls++ == failure) return NULL; return malloc(n); }
static const char wire[] = "[\"1\",null,\"grappa:user:Alice/network:NetA/channel:#one\",\"event\",{\"kind\":\"join_failed\",\"network\":\"NetA\",\"channel\":\"#one\",\"state\":\"failed\",\"numeric\":9223372036854775807,\"reason\":\"long reason with caf\\u00e9 and escaped \\\"quotes\\\" plus more than thirty two characters\"}]";
/* Goldens are derived from the original fixture, not the decoder under test. */
static const char payload_golden[] = "{\"kind\":\"join_failed\",\"network\":\"NetA\",\"channel\":\"#one\",\"state\":\"failed\",\"numeric\":9223372036854775807,\"reason\":\"long reason with caf\xC3\xA9 and escaped \\\"quotes\\\" plus more than thirty two characters\"}";
static const char render_golden[] = "[1970-01-01T00:00:00.000Z recv] join_failed target=\"#one\" state=\"failed\" reason=\"long reason with caf\xC3\xA9 and escaped \\\"quotes\\\" plus more than thirty two characters\" numeric=9223372036854775807\n";
int main(int argc, char **argv)
{
    json_malloc_t saved_malloc; json_free_t saved_free;
    struct gin_phoenix_frame *base = NULL;
    struct gin_render_record *record = NULL;
    struct gin_render_context ctx = {"Alice","NetA","#one","#one","#","1",1,GIN_RENDER_CHANNEL,GIN_RENDER_MODE_PLAIN};
    struct gin_phoenix_view expected;
    unsigned char *copy;
    size_t i, successes = 0, failures = 0, max_calls = 0;
    int rendering = argc > 1 && !strcmp(argv[1], "render");
    assert(gin_phoenix_decode((const unsigned char *)wire, strlen(wire), &base) == GIN_PHOENIX_OK);
    if (rendering) {
        assert(gin_render_event(base, &ctx, 1, 0, &record) == GIN_RENDER_OK);
        expected.data = (const unsigned char *)render_golden; expected.length = sizeof(render_golden)-1u;
        { struct gin_phoenix_view b = gin_render_record_bytes(record);
          assert(b.length == expected.length && !memcmp(b.data, expected.data, b.length)); }
    } else {
        struct gin_phoenix_view b = gin_phoenix_frame_fields(base)->payload;
        expected.data = (const unsigned char *)payload_golden; expected.length = sizeof(payload_golden)-1u;
        assert(b.length == expected.length && !memcmp(b.data, expected.data, b.length));
    }
    copy = malloc(expected.length); assert(copy); memcpy(copy, expected.data, expected.length);
    json_get_alloc_funcs(&saved_malloc, &saved_free);
    for (i = 0; i < 256; ++i) {
        struct gin_phoenix_frame *frame = NULL;
        struct gin_render_record *result = NULL;
        struct gin_phoenix_view actual;
        int ok;
        calls = 0; failure = i;
        json_set_alloc_funcs(fail_malloc, saved_free);
        if (rendering) {
            enum gin_render_code code = gin_render_event(base, &ctx, 1, 0, &result);
            ok = code == GIN_RENDER_OK;
            actual = gin_render_record_bytes(result);
        } else {
            enum gin_phoenix_code code = gin_phoenix_decode((const unsigned char *)wire, strlen(wire), &frame);
            ok = code == GIN_PHOENIX_OK;
            actual.data = NULL; actual.length = 0;
            if (ok) actual = gin_phoenix_frame_fields(frame)->payload;
        }
        /* Native APIs have released their temporary DOMs before returning. */
        json_set_alloc_funcs(saved_malloc, saved_free);
        if (calls > max_calls) max_calls = calls;
        if (ok) {
            ++successes;
            if (actual.length != expected.length || memcmp(actual.data, copy, expected.length)) {
                fprintf(stderr, "SUCCESSFUL_DECODE_FIDELITY_VIOLATION phase=%s fail_at=%zu calls=%zu expected_length=%zu actual_length=%zu\n", rendering ? "render" : "codec", i, calls, expected.length, actual.length);
                fprintf(stderr, "ACTUAL_SYNTHETIC_FIXTURE: %.*s\n", (int)actual.length, actual.data);
                gin_render_record_free(result); gin_phoenix_frame_free(frame);
                gin_render_record_free(record); gin_phoenix_frame_free(base); free(copy);
                return 1;
            }
        } else ++failures;
        gin_render_record_free(result); gin_phoenix_frame_free(frame);
    }
    gin_render_record_free(record); gin_phoenix_frame_free(base); free(copy);
    assert(max_calls < 256 && successes && failures);
    printf("Fidelity %s: %zu exact successes, %zu explicit failures; max allocations %zu, fail indices 0..255.\n", rendering ? "render" : "codec", successes, failures, max_calls);
    return 0;
}
