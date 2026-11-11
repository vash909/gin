#ifdef NDEBUG
#undef NDEBUG
#endif
#include "render_values.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check(const unsigned char *data, size_t length, const char *expected)
{
    char out[256], before[256];
    size_t n = strlen(expected), written = 777;
    memset(out, 0x5a, sizeof(out));
    assert(gin_render_escape(data, length, out, n + 1, &written) == GIN_RENDER_VALUE_OK);
    assert(written == n && !memcmp(out, expected, n + 1));
    assert((unsigned char)out[n + 1] == 0x5a);
    memset(out, 0x5a, sizeof(out)); memcpy(before, out, sizeof(out)); written = 777;
    assert(gin_render_escape(data, length, out, n, &written) == GIN_RENDER_VALUE_LIMIT);
    assert(written == 777 && !memcmp(out, before, sizeof(out)));
}
static void bad(const unsigned char *data, size_t length)
{
    char out[32], before[32]; size_t n = 99;
    memset(out, 0x42, sizeof(out)); memcpy(before, out, sizeof(out));
    assert(gin_render_escape(data, length, out, sizeof(out), &n) == GIN_RENDER_VALUE_INVALID);
    assert(n == 99 && !memcmp(out, before, sizeof(out)));
}
int main(void)
{
    unsigned char c, long_input[GIN_RENDER_ESCAPE_INPUT_MAX + 1];
    char out[32], expected[8], before[32]; size_t n, i;
    static const unsigned char invalid[][4] = {
        {0x80,0,0,0}, {0xff,0,0,0}, {0xc0,0xaf,0,0}, {0xc1,0xbf,0,0},
        {0xe0,0x80,0xaf,0}, {0xed,0xa0,0x80,0}, {0xf4,0x90,0x80,0x80},
        {0xf5,0x80,0x80,0x80}, {0xe2,0x28,0xa1,0}, {0xf0,0x80,0x80,0xaf}
    };
    static const size_t lengths[] = {1,1,2,2,3,3,4,4,3,4};
    check(NULL, 0, ""); check((const unsigned char *)"Hello", 5, "Hello");
    check((const unsigned char *)"a\"b\\c", 5, "a\\\"b\\\\c");
    check((const unsigned char *)"a\0b", 3, "a\\x00b");
    for (i = 0; i < 32; ++i) { c = (unsigned char)i; (void)snprintf(expected, sizeof(expected), "\\x%02X", (unsigned)i); check(&c, 1, expected); }
    c = 0x7f; check(&c, 1, "\\x7F");
    check((const unsigned char *)"\xc2\x85", 2, "\\u0085");
    check((const unsigned char *)"\xc2\x9b", 2, "\\u009B");
    check((const unsigned char *)"h\xc3\xa9llo", 6, "h\xc3\xa9llo");
    check((const unsigned char *)"\xf0\x9f\x98\x80", 4, "\xf0\x9f\x98\x80");
    check((const unsigned char *)"\xd8\x9c", 2, "\\u061C");
    check((const unsigned char *)"\xe2\x80\x8e", 3, "\\u200E");
    check((const unsigned char *)"\xe2\x80\xa8", 3, "\\u2028");
    check((const unsigned char *)"\xe2\x80\xae", 3, "\\u202E");
    check((const unsigned char *)"\xe2\x81\xa6", 3, "\\u2066");
    check((const unsigned char *)"\xe2\x81\xa9", 3, "\\u2069");
    check((const unsigned char *)"A\x1b[31mB", 7, "A\\x1B[31mB");
    for (i = 0; i < sizeof(lengths)/sizeof(lengths[0]); ++i) bad(invalid[i], lengths[i]);
    bad((const unsigned char *)"\xc2", 1); bad((const unsigned char *)"\xe2\x82", 2);
    bad((const unsigned char *)"\xf0\x9f\x98", 3); bad(NULL, 1);
    memset(out, 0x61, sizeof(out)); memcpy(before, out, sizeof(out)); n = 91;
    assert(gin_render_escape((const unsigned char *)"a", 1, NULL, 2, &n) == GIN_RENDER_VALUE_INVALID);
    assert(gin_render_escape((const unsigned char *)"a", 1, out, sizeof(out), NULL) == GIN_RENDER_VALUE_INVALID);
    memset(long_input, 'a', sizeof(long_input));
    assert(gin_render_escape(long_input, sizeof(long_input), out, sizeof(out), &n) == GIN_RENDER_VALUE_LIMIT);
    assert(gin_render_escape(long_input, SIZE_MAX, out, sizeof(out), &n) == GIN_RENDER_VALUE_LIMIT);
    assert(n == 91 && !memcmp(out, before, sizeof(out)));
    puts("Render escape controls, UTF-8, bounds and unchanged failure tests passed");
    return 0;
}
