#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_command.h"
#include "gin_input.h"
#include "gin_naming.h"
#include <assert.h>
#include <string.h>

static void good(const char *input, enum gin_command_kind kind, const char *data)
{
    struct gin_command out;
    memset(&out, 0xa5, sizeof(out));
    assert(gin_command_parse((const unsigned char *)input, strlen(input),
                             "#&+!", &out) == GIN_COMMAND_OK);
    assert(out.kind == kind && out.length == strlen(data));
    assert(memcmp(out.data, data, out.length + 1u) == 0);
}
static void bad_bytes(const unsigned char *input, size_t length,
                      const char *types, enum gin_command_code code)
{
    struct gin_command out, before;
    memset(&out, 0xa5, sizeof(out));
    memcpy(&before, &out, sizeof(out));
    assert(gin_command_parse(input, length, types, &out) == code);
    assert(memcmp(&before, &out, sizeof(out)) == 0);
}
static void bad(const char *input, enum gin_command_code code)
{
    bad_bytes((const unsigned char *)input, strlen(input), "#&+!", code);
}
int main(void)
{
    unsigned char bytes[GIN_COMMAND_RECORD_MAX + 1u];
    char text[160];
    size_t i;
    assert(GIN_COMMAND_RECORD_MAX == GIN_INPUT_LIMIT);
    good("/join #MiXeD", GIN_COMMAND_JOIN, "#MiXeD");
    good("/part &Room", GIN_COMMAND_PART, "&Room");
    good("/query foo~", GIN_COMMAND_QUERY, "foo~");
    good("/query 0AAAAAAAB", GIN_COMMAND_QUERY, "0AAAAAAAB");
    good("/query ~foo", GIN_COMMAND_QUERY, "~foo");
    good("/query Bob:$", GIN_COMMAND_QUERY, "Bob:$");
    good("/join #", GIN_COMMAND_JOIN, "#");
    good("/part &", GIN_COMMAND_PART, "&");
    good("hello \xc3\xa8", GIN_COMMAND_SEND, "hello \xc3\xa8");
    good("//join #Room", GIN_COMMAND_SEND, "/join #Room");
    good("//", GIN_COMMAND_SEND, "/");
    good("///query Bob", GIN_COMMAND_SEND, "//query Bob");
    bad("", GIN_COMMAND_EMPTY);
    bad("/", GIN_COMMAND_UNSUPPORTED);
    bad("/raw JOIN #a", GIN_COMMAND_UNSUPPORTED);
    bad("/JOIN #a", GIN_COMMAND_UNSUPPORTED);
    bad("/joinx #a", GIN_COMMAND_UNSUPPORTED);
    bad("/join", GIN_COMMAND_INVALID);
    bad("/join ", GIN_COMMAND_INVALID);
    bad("/join  #a", GIN_COMMAND_INVALID);
    bad("/join #a ", GIN_COMMAND_INVALID);
    bad("/join #a key", GIN_COMMAND_INVALID);
    bad("/part #a reason", GIN_COMMAND_INVALID);
    bad("/join #a,#b", GIN_COMMAND_INVALID);
    bad("/join +", GIN_COMMAND_INVALID);
    bad("/join !", GIN_COMMAND_INVALID);
    bad("/join Bob", GIN_COMMAND_INVALID);
    bad("/query #a", GIN_COMMAND_INVALID);
    bad("/query $server", GIN_COMMAND_INVALID);
    bad("/query :Bob", GIN_COMMAND_INVALID);
    bad("/query Bob.name", GIN_COMMAND_INVALID);
    bad("/query Bob@host", GIN_COMMAND_INVALID);
    bad("/query Bob*", GIN_COMMAND_INVALID);
    bad("/query Bob?", GIN_COMMAND_INVALID);
    bad("/query Bob+", GIN_COMMAND_INVALID);
    bad("hi\tthere", GIN_COMMAND_INVALID);
    bad("\xed\xa0\x80", GIN_COMMAND_INVALID);
    bad("\xc0\xaf", GIN_COMMAND_INVALID);
    bad("\xf4\x90\x80\x80", GIN_COMMAND_INVALID);
    bad_bytes((const unsigned char *)"A\0B", 3u, "#", GIN_COMMAND_INVALID);
    bad_bytes((const unsigned char *)"/join &a", 8u, "#", GIN_COMMAND_INVALID);
    bad_bytes((const unsigned char *)"hello", 5u, NULL, GIN_COMMAND_INVALID);
    bad_bytes(NULL, 1u, "#", GIN_COMMAND_INVALID);
    memset(bytes, 'a', sizeof(bytes));
    bad_bytes(bytes, sizeof(bytes), "#", GIN_COMMAND_LIMIT);
    { struct gin_command out;
      assert(gin_command_parse(bytes, GIN_COMMAND_RECORD_MAX, "#", &out) == GIN_COMMAND_OK);
      assert(out.length == GIN_COMMAND_RECORD_MAX && out.data[out.length] == 0); }
    memcpy(text, "/join #", 7u); memset(text + 7, 'a', 49u); text[56] = 0;
    assert(strlen(text + 6) == GIN_COMMAND_CHANNEL_MAX);
    good(text, GIN_COMMAND_JOIN, text + 6);
    text[56] = 'a'; text[57] = 0;
    bad(text, GIN_COMMAND_LIMIT);
    { struct gin_command out;
      assert(gin_chantypes_validate("") == 0);
      assert(gin_command_parse((const unsigned char *)"/query Bob", 10u, "", &out) == GIN_COMMAND_OK);
      assert(out.kind == GIN_COMMAND_QUERY); }
    memcpy(text, "/query ", 7u); memset(text + 7, 'a', 128u); text[135] = 0;
    good(text, GIN_COMMAND_QUERY, text + 7);
    text[135] = 'a'; text[136] = 0; bad(text, GIN_COMMAND_LIMIT);
    memcpy(text, "/query ", 7u); memset(text + 7, '[', 85u); text[92] = 0;
    bad(text, GIN_COMMAND_LIMIT);
    for (i = 0; i < 32u; i++) {
        unsigned char control[3] = {'a', (unsigned char)i, 'b'};
        bad_bytes(control, sizeof(control), "#", GIN_COMMAND_INVALID);
    }
    return 0;
}
