#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_credential.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
static void check(int condition) { ++checks; assert(condition); }
static void fixture(struct gin_credential *v)
{
    memset(v, 0, sizeof(*v));
    strcpy(v->origin, "https://example.test");
    strcpy(v->token, "Synthetic-._~+/==");
    strcpy(v->subject_id, "01234567-89AB-cdef-0123-456789abcdef");
    strcpy(v->subject_name, "Synthetic user");
}
static int zero(const struct gin_credential *v)
{
    size_t i;
    for (i = 0; i < sizeof(*v); ++i)
        if (((const unsigned char *)v)[i] != 0) return 0;
    return 1;
}
int main(void)
{
    struct gin_credential v;
    struct gin_root empty = GIN_ROOT_INITIALIZER;
    const char *bad_origins[] = {"", "https://", "http://example.test",
        "HTTPS://example.test", "https://example.test/", "https://example.test?q=1",
        "https://example.test#x", "https://user@example.test", "https://x\\y",
        "https://x\t", "https://\303\251.test", "https://:443", "https://x:",
        "https://x:abc", "https://[]", "https://[::1", "https://[::1]x"};
    const char *good_origins[] = {"https://example.test", "https://example.test:443",
        "https://127.0.0.1:443", "https://[::1]:443", "https://[2001:db8::1]"};
    size_t i;
    fixture(&v);
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_OK);
    check(gin_credential_validate(NULL) == GIN_CREDENTIAL_INVALID);
    gin_credential_clear(NULL);
    for (i = 0; i < sizeof(bad_origins) / sizeof(bad_origins[0]); ++i) {
        fixture(&v); strcpy(v.origin, bad_origins[i]);
        check(gin_credential_validate(&v) == GIN_CREDENTIAL_INVALID);
    }
    for (i = 0; i < sizeof(good_origins) / sizeof(good_origins[0]); ++i) {
        fixture(&v); strcpy(v.origin, good_origins[i]);
        check(gin_credential_validate(&v) == GIN_CREDENTIAL_OK);
    }
    for (i = 1; i <= 255; ++i) {
        int valid = (i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z') ||
            (i >= '0' && i <= '9') || strchr("-._~+/", (int)i) != NULL;
        fixture(&v); v.token[0] = (char)i; v.token[1] = 0;
        check(gin_credential_validate(&v) == (valid ? GIN_CREDENTIAL_OK : GIN_CREDENTIAL_INVALID));
    }
    fixture(&v); memset(v.origin, 'x', sizeof(v.origin));
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_INVALID);
    fixture(&v); memset(v.subject_id, 'a', sizeof(v.subject_id));
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_INVALID);
    fixture(&v); memset(v.subject_name, 'x', sizeof(v.subject_name));
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_INVALID);
    fixture(&v); memset(v.token, 'x', sizeof(v.token));
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_INVALID);
    fixture(&v); memset(v.token, 'x', sizeof(v.token) - 1); v.token[4096] = 0;
    memset(v.subject_name, 'x', sizeof(v.subject_name) - 1); v.subject_name[255] = 0;
    check(gin_credential_validate(&v) == GIN_CREDENTIAL_OK);
    fixture(&v);
    check(gin_credential_load(&empty, v.origin, NULL) == GIN_CREDENTIAL_INVALID);
    check(gin_credential_load(NULL, "https://example.test", &v) == GIN_CREDENTIAL_INVALID);
    check(zero(&v));
    fixture(&v);
    check(gin_credential_load(&empty, "http://invalid", &v) == GIN_CREDENTIAL_INVALID);
    check(zero(&v));
    fixture(&v);
    check(gin_credential_load(&empty, NULL, &v) == GIN_CREDENTIAL_INVALID);
    check(zero(&v));
    fixture(&v);
    check(gin_credential_save(NULL, &v) == GIN_CREDENTIAL_INVALID);
    check(gin_credential_save(&empty, NULL) == GIN_CREDENTIAL_INVALID);
    check(gin_credential_forget(NULL) == GIN_CREDENTIAL_INVALID);
    for (i = GIN_CREDENTIAL_OK; i <= GIN_CREDENTIAL_NOT_FOUND; ++i)
        check(gin_credential_error((enum gin_credential_code)i) != NULL);
    check(gin_credential_error((enum gin_credential_code)99) != NULL);
    gin_credential_clear(&v);
    check(zero(&v));
    printf("credential unit checks: %u\n", checks);
    return 0;
}
