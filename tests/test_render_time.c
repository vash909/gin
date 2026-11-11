#ifdef NDEBUG
#undef NDEBUG
#endif
#include "render_values.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    static const struct { int64_t ms; const char *expected; } cases[] = {
        {(-INT64_C(62135596800000)), "0001-01-01T00:00:00.000Z"},
        {(-INT64_C(1)), "1969-12-31T23:59:59.999Z"},
        {INT64_C(0), "1970-01-01T00:00:00.000Z"},
        {INT64_C(1), "1970-01-01T00:00:00.001Z"},
        {INT64_C(999), "1970-01-01T00:00:00.999Z"},
        {INT64_C(1000), "1970-01-01T00:00:01.000Z"},
        {INT64_C(951827696789), "2000-02-29T12:34:56.789Z"},
        {INT64_C(4107542400000), "2100-03-01T00:00:00.000Z"},
        {INT64_C(253402300799999), "9999-12-31T23:59:59.999Z"}
    };
    char out[GIN_RENDER_TIME_CAP + 1], before[sizeof(out)]; size_t i;
    for (i = 0; i < sizeof(cases)/sizeof(cases[0]); ++i) {
        memset(out, 0x5a, sizeof(out));
        assert(gin_render_time(cases[i].ms, out) == GIN_RENDER_VALUE_OK);
        assert(strlen(out) == 24 && !strcmp(out, cases[i].expected));
        assert(out[GIN_RENDER_TIME_CAP] == 0x5a);
    }
    memset(out, 0x5a, sizeof(out)); memcpy(before, out, sizeof(out));
    assert(gin_render_time(INT64_MIN, out) == GIN_RENDER_VALUE_RANGE);
    assert(gin_render_time(INT64_MAX, out) == GIN_RENDER_VALUE_RANGE);
    assert(gin_render_time(GIN_RENDER_TIME_MIN_MS - 1, out) == GIN_RENDER_VALUE_RANGE);
    assert(gin_render_time(GIN_RENDER_TIME_MAX_MS + 1, out) == GIN_RENDER_VALUE_RANGE);
    assert(!memcmp(out, before, sizeof(out)));
    assert(gin_render_time(0, NULL) == GIN_RENDER_VALUE_INVALID);
    puts("Render UTC millisecond, Gregorian, range and failure tests passed");
    return 0;
}
