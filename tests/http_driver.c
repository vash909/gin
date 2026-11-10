/* Test-only exercise of the public C adapter; never installed. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gin_http.h"

int main(int argc, char **argv)
{
    unsigned char body[GIN_HTTP_BODY_MAX];
    struct gin_http_settings settings;
    struct gin_http_result result;
    struct sigaction before[4], after;
    const int signals[] = { SIGINT, SIGTERM, SIGPIPE, SIGALRM };
    const char *zeroed_env = getenv("GIN_HTTP_EXPECT_ZEROED_PREFIX");
    size_t zeroed_prefix = 0;
    size_t i;
    assert(argc == 8);
    if (zeroed_env) {
        char *end = NULL;
        long value;
        assert(getenv("GIN_HTTP_REPEAT"));
        assert(zeroed_env[0] != '\0');
        errno = 0;
        value = strtol(zeroed_env, &end, 10);
        assert(errno == 0 && end != zeroed_env && *end == '\0');
        assert(value >= 1 && value <= 16);
        zeroed_prefix = (size_t)value;
    }
    memset(body, 0xA5, sizeof(body));
    memset(&settings, 0, sizeof(settings));
    settings.url = argv[1];
    settings.ca_file = strcmp(argv[2], "-") ? argv[2] : NULL;
    settings.allow_loopback_http = atoi(argv[3]);
    settings.connect_ms = strtol(argv[4], NULL, 10);
    settings.total_ms = strtol(argv[5], NULL, 10);
    settings.header_limit = (size_t)strtoull(argv[7], NULL, 10);
    for (i = 0; i < 4; i++) assert(sigaction(signals[i], NULL, &before[i]) == 0);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    result = gin_http_get(&settings, body,
        (size_t)strtoull(argv[6], NULL, 10));
    /* A second request in-process verifies handle isolation and storage reuse. */
    if (getenv("GIN_HTTP_REPEAT")) {
        if (zeroed_env) {
            assert(result.code == GIN_HTTP_OK);
            assert(result.body_length > zeroed_prefix);
            for (i = 0; i < zeroed_prefix; i++) assert(body[i] != 0);
        }
        result = gin_http_get(&settings, body,
            (size_t)strtoull(argv[6], NULL, 10));
        if (zeroed_env) {
            assert(result.code != GIN_HTTP_OK);
            assert(result.body_length == 0);
            for (i = 0; i < zeroed_prefix; i++) assert(body[i] == 0);
        }
    }
    gin_http_global_cleanup();
    for (i = 0; i < 4; i++) {
        assert(sigaction(signals[i], NULL, &after) == 0);
        assert(after.sa_handler == before[i].sa_handler);
        assert(after.sa_flags == before[i].sa_flags);
    }
    printf("%s %ld %zu %zu\n", gin_http_error(result.code),
        result.http_status, result.body_length, result.header_length);
    assert(result.body_length <= sizeof(body));
    assert(fwrite(body, 1, result.body_length, stdout) == result.body_length);
    return 0;
}
