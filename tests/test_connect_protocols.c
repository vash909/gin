#ifdef NDEBUG
#undef NDEBUG
#endif
#include "connect_values.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    char out[GIN_WS_PROTOCOL_CAP];
    size_t written = 99;
    memset(out, 'x', sizeof(out));
    assert(gin_ws_protocols("aa~", out, sizeof(out), &written) == GIN_WS_VALUE_OK);
    assert(!strcmp(out, "phoenix,base64url.bearer.phx.YWF+"));
    assert(written == strlen(out));
    {
        static const struct { const char *token; const char *expected; } vectors[] = {
            {"f", "phoenix,base64url.bearer.phx.Zg"},
            {"fo", "phoenix,base64url.bearer.phx.Zm8"},
            {"foo", "phoenix,base64url.bearer.phx.Zm9v"},
            {"foob", "phoenix,base64url.bearer.phx.Zm9vYg"},
            {"fooba", "phoenix,base64url.bearer.phx.Zm9vYmE"},
            {"foobar", "phoenix,base64url.bearer.phx.Zm9vYmFy"},
            {"aa~", "phoenix,base64url.bearer.phx.YWF+"},
            {"a/b+c_-.~", "phoenix,base64url.bearer.phx.YS9iK2NfLS5+"},
            {"a=", "phoenix,base64url.bearer.phx.YT0"},
            {"abc===", "phoenix,base64url.bearer.phx.YWJjPT09"},
        };
        static const char *bad[] = { "", "=a", "a=b", "a b", "a\r\nb", "a\t", "a\x7f", "a\xff", "Bearer abc" };
        char before[GIN_WS_PROTOCOL_CAP];
        size_t i, n;
        for (i = 0; i < sizeof(vectors)/sizeof(vectors[0]); i++) {
            assert(gin_ws_protocols(vectors[i].token, out, sizeof(out), &written) == GIN_WS_VALUE_OK);
            assert(!strcmp(out, vectors[i].expected) && written == strlen(vectors[i].expected));
            n = written;
            memset(out, 'z', sizeof(out)); memcpy(before, out, sizeof(out)); written = 99;
            assert(gin_ws_protocols(vectors[i].token, out, n, &written) == GIN_WS_VALUE_LIMIT);
            assert(written == 99 && !memcmp(before, out, sizeof(out)));
            assert(gin_ws_protocols(vectors[i].token, out, n+1, &written) == GIN_WS_VALUE_OK);
            assert(written == n && !strcmp(out, vectors[i].expected));
            assert(out[n+1] == 'z');
        }
        memset(out, 'z', sizeof(out)); memcpy(before, out, sizeof(out)); written = 99;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            assert(gin_ws_protocols(bad[i], out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
            assert(written == 99 && !memcmp(before, out, sizeof(out)));
        }
        assert(gin_ws_protocols(NULL, out, sizeof(out), &written) == GIN_WS_VALUE_INVALID);
        assert(gin_ws_protocols("a", NULL, sizeof(out), &written) == GIN_WS_VALUE_INVALID && written == 99);
        assert(gin_ws_protocols("a", out, sizeof(out), NULL) == GIN_WS_VALUE_INVALID && !memcmp(before, out, sizeof(out)));
        {
            char token[GIN_CREDENTIAL_TOKEN_MAX+2];
            memset(token, 'a', sizeof(token)); token[GIN_CREDENTIAL_TOKEN_MAX] = 0;
            assert(gin_ws_protocols(token, out, sizeof(out), &written) == GIN_WS_VALUE_OK);
            assert(written == strlen(out) && out[written] == 0);
            memset(out, 'z', sizeof(out)); memcpy(before, out, sizeof(out)); written = 99;
            token[GIN_CREDENTIAL_TOKEN_MAX] = 'a'; token[GIN_CREDENTIAL_TOKEN_MAX+1] = 0;
            assert(gin_ws_protocols(token, out, sizeof(out), &written) == GIN_WS_VALUE_LIMIT);
            assert(written == 99 && !memcmp(before, out, sizeof(out)));
        }
    }
    puts("Connect auth encoding, RFC6750 grammar, bounds and failure controls passed");
    return 0;
}
