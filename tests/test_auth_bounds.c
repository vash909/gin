#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_auth.h"
#include <assert.h>
#include <string.h>

static void cleared(const struct gin_credential *c)
{
    struct gin_credential zero;
    memset(&zero, 0, sizeof(zero));
    assert(!memcmp(c, &zero, sizeof(zero)));
}

int main(void)
{
    struct gin_auth_settings s = { "http://127.0.0.1", NULL, 100, 200 };
    struct gin_credential out;
    char long_password[GIN_AUTH_PASSWORD_MAX + 2];
    memset(long_password, 'x', sizeof(long_password));
    long_password[sizeof(long_password) - 1] = 0;
    memset(&out, 0xa5, sizeof(out));
    assert(gin_auth_login(&s, "account", "secret", &out) == GIN_AUTH_INVALID);
    cleared(&out);
    memset(&out, 0xa5, sizeof(out));
    assert(gin_auth_login(&s, "account", long_password, &out) == GIN_AUTH_INVALID);
    cleared(&out);
    memset(&out, 0xa5, sizeof(out));
    assert(gin_auth_login(&s, "bad\xff", "secret", &out) == GIN_AUTH_INVALID);
    cleared(&out);
    assert(gin_auth_login(NULL, "account", "secret", &out) == GIN_AUTH_INVALID);
    assert(gin_auth_resume(&s, "bearer", &out) == GIN_AUTH_INVALID);
    cleared(&out);
    assert(!gin_http_origin_valid("https://example.invalid:"));
    s.origin = "https://example.invalid:";
    assert(gin_auth_resume(&s, "bearer", &out) == GIN_AUTH_INVALID);
    cleared(&out);
    assert(!gin_http_origin_valid("https://example.invalid:0"));
    assert(!gin_http_origin_valid("https://example.invalid:"));
    assert(!gin_http_origin_valid("https://example.invalid:65536"));
    assert(gin_http_origin_valid("https://example.invalid:65535"));
    return 0;
}
