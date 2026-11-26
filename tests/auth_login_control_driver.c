#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_control.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;
static void stop(int signo) { stopped = signo; }
static int tick(void *opaque, unsigned *wait_ms)
{
    (void)opaque;
    if (*wait_ms > 20) *wait_ms = 20;
    return stopped != 0;
}
int main(int argc, char **argv)
{
    struct sigaction sa;
    struct gin_auth_settings s;
    struct gin_http_control control = {tick, NULL, 0};
    struct gin_credential out, zero;
    enum gin_auth_code code;
    int valid, cleared;
    assert(argc == 4);
    if (strcmp(argv[1], "preint") && strcmp(argv[1], "preterm") &&
        strcmp(argv[1], "legacy") && strcmp(argv[1], "null") &&
        strcmp(argv[1], "healthy") && strcmp(argv[1], "held")) return 2;
    alarm(12);
    memset(&sa, 0, sizeof(sa)); sa.sa_handler = stop;
    assert(sigemptyset(&sa.sa_mask) == 0);
    assert(sigaction(SIGINT, &sa, NULL) == 0);
    assert(sigaction(SIGTERM, &sa, NULL) == 0);
    s.origin = argv[2]; s.ca_file = argv[3];
    s.connect_ms = 3000; s.total_ms = 20000;
    memset(&out, 0xa5, sizeof(out)); memset(&zero, 0, sizeof(zero));
    assert(gin_http_global_init() == GIN_HTTP_OK);
    if (!strcmp(argv[1], "preint")) assert(raise(SIGINT) == 0);
    if (!strcmp(argv[1], "preterm")) assert(raise(SIGTERM) == 0);
    if (!strcmp(argv[1], "legacy"))
        code = gin_auth_login(&s, "control-user", "synthetic-password-LC", &out);
    else {
#ifdef LEGACY_LOGIN_BRIDGE
        code = gin_auth_login(&s, "control-user", "synthetic-password-LC", &out);
#else
        code = gin_auth_login_controlled(&s, "control-user", "synthetic-password-LC", &out,
            !strcmp(argv[1], "null") ? NULL : &control);
#endif
    }
    valid = gin_credential_validate(&out) == GIN_CREDENTIAL_OK;
    if (valid) {
        assert(!strcmp(out.origin, s.origin));
        assert(!strcmp(out.token, "synthetic-token-LC"));
        assert(!strcmp(out.subject_id, "abcdefab-2345-4567-89ab-abcdefabcdef"));
        assert(!strcmp(out.subject_name, "ControlUser"));
    }
    cleared = !memcmp(&out, &zero, sizeof(out));
    printf("%s %d %d %d\n", gin_auth_error(code), valid, cleared, control.aborted);
    gin_credential_clear(&out);
    assert(!memcmp(&out, &zero, sizeof(out)));
    gin_http_global_cleanup();
    return 0;
}
