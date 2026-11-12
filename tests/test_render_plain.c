#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_render.h"
#include <assert.h>
#include <string.h>
static void check(const char *kind, const char *body, size_t length, const char *expected)
{
    struct gin_render_record *r = NULL;
    struct gin_phoenix_view v;
    assert(gin_render_plain_message(kind, (const unsigned char *)"Nick", 4,
        (const unsigned char *)body, length, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_OK);
    v = gin_render_record_bytes(r);
    assert(v.length == strlen(expected) && !memcmp(v.data, expected, v.length));
    assert(gin_render_plain_message(kind, NULL, 0, NULL, 0, 0,
        GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_INVALID);
    gin_render_record_free(r);
}
int main(void)
{
    struct gin_render_record *r = NULL;
    const unsigned char bad[] = {0xc0,0x80};
    check("privmsg", "hi\n", 3, "[1970-01-01T00:00:00.000Z] privmsg sender=\"Nick\" body=\"hi\\x0A\"\n");
    check("notice", NULL, 0, "[1970-01-01T00:00:00.000Z] notice sender=\"Nick\" body=\"\"\n");
    check("action", "\001ACTION waves\001", 14, "[1970-01-01T00:00:00.000Z] action sender=\"Nick\" body=\"waves\"\n");
    check("action", "\001ACTION waves", 13, "[1970-01-01T00:00:00.000Z] action sender=\"Nick\" body=\"waves\"\n");
    assert(gin_render_plain_message("other", NULL, 0, NULL, 0, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_IGNORE && !r);
    assert(gin_render_plain_message("e2e_new", NULL, 0, NULL, 0, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_UNSUPPORTED_ENCRYPTION && !r);
    assert(gin_render_plain_message("other", NULL, 0, (const unsigned char *)"\001E2E", 4, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_UNSUPPORTED_ENCRYPTION && !r);
    assert(gin_render_plain_message("privmsg", NULL, 0, NULL, 0, 0, GIN_RENDER_MODE_UNKNOWN, &r) == GIN_RENDER_UNSUPPORTED_ENCRYPTION && !r);
    assert(gin_render_plain_message("privmsg", bad, sizeof(bad), NULL, 0, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_MALFORMED && !r);
    assert(gin_render_plain_message("privmsg", NULL, 0, NULL, 1, 0, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_INVALID && !r);
    assert(gin_render_plain_message("privmsg", NULL, 0, NULL, 0, INT64_MAX, GIN_RENDER_MODE_PLAIN, &r) == GIN_RENDER_RANGE && !r);
    return 0;
}
