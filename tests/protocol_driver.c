/* Darwin hides O_NOFOLLOW under strict POSIX selection. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_protocol.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void zero(const void *p, size_t n)
{
    const unsigned char *v = p;
    size_t i;
    for (i = 0; i < n; i++) assert(v[i] == 0);
}

int main(int argc, char **argv)
{
    struct gin_auth_settings s;
    struct gin_protocol_policy policy;
    struct gin_protocol_info info;
    struct gin_protocol_auth_result r;
    struct gin_credential out;
    char secret[GIN_AUTH_PASSWORD_MAX + 2];
    int fd;
    ssize_t n;
    struct stat st;
    assert(argc == 8);
    s.origin = argv[2]; s.ca_file = argv[3];
    s.connect_ms = 300; s.total_ms = 700;
    /* Instrumented TLS setup is slower; production policy is unchanged. */
    if (!strcmp(argv[7], "instrumented")) {
        s.connect_ms = 1500; s.total_ms = 4000;
    }
    policy.client_protocol = atoi(argv[5]);
    policy.required_server_protocol = atoi(argv[6]);
    if (!strcmp(argv[7], "bad-timeout")) s.total_ms = 0;
    assert(gin_http_global_init() == GIN_HTTP_OK);
    memset(&info, 0xa5, sizeof(info));
    if (!strcmp(argv[1], "fetch")) {
        enum gin_protocol_code code = gin_protocol_fetch(&s, &policy, &info);
        if (code != GIN_PROTOCOL_OK) zero(&info, sizeof(info));
        else {
            assert(info.server_protocol > 0 && info.minimum_client_protocol > 0);
            assert(gin_protocol_server_at_least(&info, policy.required_server_protocol));
        }
        puts(gin_protocol_error(code));
    } else {
        /* Only synthetic secrets from checked private fixture files. */
        fd = open(argv[4], O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
        assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
        assert(st.st_uid == geteuid() && (st.st_mode & 07777) == 0600 && st.st_nlink == 1);
        n = read(fd, secret, sizeof(secret));
        assert(n > 0 && n <= GIN_AUTH_PASSWORD_MAX && close(fd) == 0);
        secret[n] = 0;
        assert((size_t)n == strlen(secret));
        {
            int calls = !strcmp(argv[1], "resume-twice") ? 2 : 1;
            int call;
            assert(calls == 2 || !strcmp(argv[1], "login") || !strcmp(argv[1], "resume"));
            for (call = 0; call < calls; call++) {
                memset(&out, 0xa5, sizeof(out));
                if (!strcmp(argv[1], "login"))
                    r = gin_protocol_login(&s, &policy, "Alice", secret, &out);
                else
                    r = gin_protocol_resume(&s, &policy, secret, &out);
                if (r.protocol != GIN_PROTOCOL_OK) {
                    assert(!r.auth_attempted);
                    zero(&r.server, sizeof(r.server));
                } else assert(r.auth_attempted == 1);
                if (!r.auth_attempted || r.auth != GIN_AUTH_OK) zero(&out, sizeof(out));
                else {
                    assert(gin_credential_validate(&out) == GIN_CREDENTIAL_OK);
                    assert(!strcmp(out.origin, s.origin) && !strcmp(out.token, secret));
                }
                printf("%s %d %s\n", gin_protocol_error(r.protocol), r.auth_attempted,
                    r.auth_attempted ? gin_auth_error(r.auth) : "not-attempted");
                gin_credential_clear(&out);
            }
        }
        memset(secret, 0, sizeof(secret));
    }
    gin_http_global_cleanup();
    return 0;
}
