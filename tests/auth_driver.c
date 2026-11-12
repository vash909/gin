#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_auth.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
/* Synthetic secrets enter through a private fixture file, never argv. */
int main(int argc, char **argv)
{
    char secret[4098];
    struct stat st;
    int fd;
    ssize_t n;
    struct gin_credential value, out;
    struct gin_auth_settings s;
    enum gin_auth_code code;
    const char *prefix = getenv("GIN_AUTH_PREFIX");
    assert(argc == 5 || argc == 6);
    fd = open(argv[4], O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    assert(st.st_uid == geteuid() && (st.st_mode & 07777) == 0600 && st.st_nlink == 1);
    n = read(fd, secret, sizeof(secret));
    assert(n > 0 && n <= 4096 && close(fd) == 0);
    secret[n] = 0;
    assert((size_t)n == strlen(secret));
    s.origin = argv[2]; s.ca_file = argv[3]; s.connect_ms = 500; s.total_ms = 1500;
    /* Instrumented fixture TLS setup needs time; production policy is unchanged. */
    if (prefix != NULL && *prefix != '\0') {
        s.connect_ms = 3000; s.total_ms = 6000;
    }
    memset(&value, 0, sizeof(value)); memset(&out, 0xa5, sizeof(out));
    assert(gin_http_global_init() == GIN_HTTP_OK);
    if (!strcmp(argv[1], "cycle")) {
        struct gin_root root = GIN_ROOT_INITIALIZER;
        struct gin_credential loaded;
        enum gin_auth_code after;
        assert(argc == 6 && gin_root_acquire(argv[5], &root) == 0);
        assert(gin_auth_login(&s, "a\"b\\c", secret, &value) == GIN_AUTH_OK);
        assert(gin_credential_save(&root, &value) == GIN_CREDENTIAL_OK);
        assert(gin_auth_identity(&s, &value, &out) == GIN_AUTH_OK);
        code = gin_auth_logout(&s, &value);
        assert(gin_credential_load(&root, s.origin, &loaded) == GIN_CREDENTIAL_OK);
        assert(!memcmp(&loaded, &value, sizeof(value)));
        after = gin_auth_identity(&s, &value, &out);
        if (code == GIN_AUTH_OK) {
            assert(after == GIN_AUTH_UNAUTHORIZED);
            assert(gin_credential_forget(&root) == GIN_CREDENTIAL_OK);
        } else assert(after == GIN_AUTH_OK);
        assert(gin_root_release(&root) == 0);
        gin_credential_clear(&loaded);
        gin_credential_clear(&out);
    } else if (!strcmp(argv[1], "login") || !strcmp(argv[1], "passwordlogin")) {
        code = gin_auth_login(&s, "a\"b\\c", secret, &out);
    } else if (!strcmp(argv[1], "resume")) {
        code = gin_auth_resume(&s, secret, &out);
    } else {
        assert(strlen(s.origin) <= GIN_CREDENTIAL_ORIGIN_MAX);
        strcpy(value.origin, s.origin); strcpy(value.token, secret);
        strcpy(value.subject_id, "12345678-1234-1234-1234-123456789abc");
        strcpy(value.subject_name, "Alice");
        if (!strcmp(argv[1], "mismatch")) strcpy(value.origin, "https://other.invalid");
        if (!strcmp(argv[1], "logout")) {
            struct gin_credential before = value;
            code = gin_auth_logout(&s, &value);
            assert(!memcmp(&before, &value, sizeof(value)));
        } else code = gin_auth_identity(&s, &value, &out);
    }
    if (strcmp(argv[1], "logout") && code != GIN_AUTH_OK) {
        struct gin_credential zero;
        memset(&zero, 0, sizeof(zero));
        assert(!memcmp(&out, &zero, sizeof(out)));
    }
    if (code == GIN_AUTH_OK && strcmp(argv[1], "logout") && strcmp(argv[1], "cycle")) {
        assert(!strcmp(out.origin, s.origin));
        if (strcmp(argv[1], "passwordlogin")) assert(!strcmp(out.token, secret));
        assert(!strcmp(out.subject_name, "Alice"));
    }
    puts(gin_auth_error(code));
    gin_credential_clear(&out); gin_credential_clear(&value);
    memset(secret, 0, sizeof(secret));
    gin_http_global_cleanup();
    return 0;
}
