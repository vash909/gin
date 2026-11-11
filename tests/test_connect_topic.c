#ifdef NDEBUG
#undef NDEBUG
#endif
#include "connect_values.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char out[GIN_WS_TOPIC_CAP];
    size_t written = 99;
    memset(out, 'x', sizeof(out));
    assert(gin_ws_topic("Alice", "Net", "#\xc3\x84LPHA[\\]^", out, sizeof(out), &written) == GIN_WS_VALUE_OK);
    assert(!strcmp(out, "grappa:user:Alice/network:Net/channel:#\xc3\x84lpha[\\]^"));
    assert(written == strlen(out));
    {
        static const char *bad[] = { "\x01", "\x7f", "\xc2\x80", "\xc2\x9f", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80", "a\r\nb" };
        char before[GIN_WS_TOPIC_CAP];
        size_t i, field, n;
        assert(gin_ws_topic("Alice", NULL, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_OK);
        assert(!strcmp(out, "grappa:user:Alice"));
        assert(gin_ws_topic("Alice", "Net", NULL, out, sizeof(out), &written) == GIN_WS_VALUE_OK);
        assert(!strcmp(out, "grappa:user:Alice/network:Net"));
        assert(gin_ws_topic("Alice", "Net", "#A/B", out, sizeof(out), &written) == GIN_WS_VALUE_OK);
        assert(!strcmp(out, "grappa:user:Alice/network:Net/channel:#a/b"));
        n = written;
        memset(out, 'z', sizeof(out)); memcpy(before, out, sizeof(out)); written = 99;
        assert(gin_ws_topic("Alice", "Net", "#A/B", out, n, &written) == GIN_WS_VALUE_LIMIT);
        assert(written == 99 && !memcmp(out, before, sizeof(out)));
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            for (field = 0; field < 3; field++) {
                assert(gin_ws_topic(field == 0 ? bad[i] : "Alice", field == 1 ? bad[i] : "Net",
                    field == 2 ? bad[i] : "#chan", out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
                assert(written == 99 && !memcmp(out, before, sizeof(out)));
            }
        }
        assert(gin_ws_topic(NULL, NULL, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("", NULL, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("Alice/x", NULL, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("Alice", "Net/x", NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("Alice", "", NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("Alice", "Net", "", out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_topic("Alice", NULL, "#x", out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(written == 99 && !memcmp(out, before, sizeof(out)));
        assert(gin_ws_topic("Alice", NULL, NULL, NULL, sizeof(out), &written) == GIN_WS_VALUE_INVALID && written == 99);
        assert(gin_ws_topic("Alice", NULL, NULL, out, sizeof(out), NULL) == GIN_WS_VALUE_INVALID && !memcmp(out, before, sizeof(out)));
        {
            char label[257], target[258];
            memset(label, 'a', sizeof(label)); label[255] = 0;
            memset(target, 'b', sizeof(target)); target[256] = 0;
            assert(gin_ws_topic(label, label, target, out, sizeof(out), &written) == GIN_WS_VALUE_OK);
            assert(written == strlen(out));
            memset(out, 'z', sizeof(out)); memcpy(before, out, sizeof(out)); written = 99;
            label[255] = 'a'; label[256] = 0;
            assert(gin_ws_topic(label, NULL, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_LIMIT);
            assert(gin_ws_topic("Alice", label, NULL, out, sizeof(out), &written) == GIN_WS_VALUE_LIMIT);
            target[256] = 'b'; target[257] = 0;
            assert(gin_ws_topic("Alice", "Net", target, out, sizeof(out), &written) == GIN_WS_VALUE_LIMIT);
            assert(written == 99 && !memcmp(out, before, sizeof(out)));
        }
    }
    puts("Connect topic identity, UTF-8, namespace, bounds and failure controls passed");
    return 0;
}
