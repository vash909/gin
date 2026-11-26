#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_control.h"
#include "terminal_password.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
static volatile sig_atomic_t stopped;
static int mode, after_auth, wiped, wipe_calls;
static char *live_password;
static void handler(int s) { stopped = s; }
static int tick(void *p, unsigned *ms)
{
    size_t i;
    assert(p == &mode); (void)ms;
    if (after_auth && live_password) {
        wiped = 1;
        for (i = 0; i < 4097; i++) if (live_password[i]) wiped = 0;
        assert(wiped);
    }
    return stopped != 0;
}
static enum tp_status read_facade(char out[4097], size_t *n, int *s)
{
    enum tp_status result = terminal_password_read(out, n, s);
    live_password = out;
    if (result == TP_OK) memset(out + *n + 1, 0xa5, 4097 - *n - 1);
    if (result == TP_OK && (mode == 1 || mode == 2)) raise(mode == 1 ? SIGINT : SIGTERM);
    return result;
}
static enum gin_auth_code auth_facade(const struct gin_auth_settings *s,
    const char *id, const char *pw, struct gin_credential *out, struct gin_http_control *c)
{
    enum gin_auth_code result = gin_auth_login_controlled(s,id,pw,out,c);
    after_auth = 1;
    if (mode == 5) {
        assert(result == GIN_AUTH_OK);
        assert(gin_credential_validate(out) == GIN_CREDENTIAL_OK);
        raise(SIGINT);
    }
    return result;
}
static void wipe_facade(void *buffer, size_t n)
{
    const unsigned char *bytes = buffer;
    size_t i;
    assert(n == 4097);
    terminal_password_wipe(buffer, n);
    for (i = 0; i < n; i++) assert(bytes[i] == 0);
    wipe_calls++;
}
static void same_mask(const sigset_t *a, const sigset_t *b)
{
    int signal_number;
    for (signal_number = 1; signal_number <= SIGRTMAX; signal_number++)
        assert(sigismember(a, signal_number) == sigismember(b, signal_number));
}
static int same_termios(const struct termios *a, const struct termios *b)
{
    size_t i;
    if (a->c_iflag != b->c_iflag) return 0;
    if (a->c_oflag != b->c_oflag) return 0;
    if (a->c_cflag != b->c_cflag) return 0;
    if (a->c_lflag != b->c_lflag) return 0;
    if (cfgetispeed(a) != cfgetispeed(b)) return 0;
    if (cfgetospeed(a) != cfgetospeed(b)) return 0;
    for (i = 0; i < NCCS; i++) if (a->c_cc[i] != b->c_cc[i]) return 0;
    return 1;
}
#ifdef LEGACY_COMPOSITION_BRIDGE
/* Working private baseline, not a historical public frontend claim. */
static int bridge(const struct gin_auth_settings *s, const struct gin_protocol_policy *p,
    struct gin_http_control *c, struct gin_credential *out)
{
    struct gin_protocol_info info;
    char pw[4097]; size_t n; int sig;
    enum gin_auth_code a = GIN_AUTH_INVALID;
    enum gin_protocol_code protocol = gin_protocol_fetch_controlled(s,p,&info,c);
    if (protocol == GIN_PROTOCOL_OK && read_facade(pw,&n,&sig) == TP_OK)
        a = gin_auth_login(s,"SyntheticUser",pw,out);
    terminal_password_wipe(pw,sizeof(pw));
    return a == GIN_AUTH_OK;
}
#else
#define terminal_password_wipe wipe_facade
#define terminal_password_read read_facade
#define gin_auth_login_controlled auth_facade
#include "../src/interactive_login.c"
#undef terminal_password_wipe
#undef terminal_password_read
#undef gin_auth_login_controlled
#endif
int main(int argc, char **argv)
{
    struct gin_auth_settings s;
    struct gin_protocol_policy p = {5,4};
    struct gin_http_control c = {tick,&mode,0};
    struct gin_credential out;
    struct sigaction a, saved[7], observed;
    sigset_t mask_before, mask_after, blocked;
    int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
    size_t i; int ok;
    int tty_fd;
    struct termios tty_before = {0}, tty_after = {0};
    alarm(7);
    if (argc != 4) return 2;
    if (!strcmp(argv[1],"healthy")) mode=0;
    else if (!strcmp(argv[1],"int")) mode=1;
    else if (!strcmp(argv[1],"term")) mode=2;
    else if (!strcmp(argv[1],"bad")) mode=3;
    else if (!strcmp(argv[1],"failure")) mode=4;
    else if (!strcmp(argv[1],"postauthstop")) mode=5;
    else if (!strcmp(argv[1],"preaborted")) mode=6;
    else if (!strcmp(argv[1],"default")) mode=7;
    else if (!strcmp(argv[1],"ignored")) mode=8;
    else if (!strcmp(argv[1],"blocked")) mode=9;
    else if (!strcmp(argv[1],"heldpost")) mode=10;
    else return 2;
    memset(&a,0,sizeof(a)); a.sa_handler=handler; sigemptyset(&a.sa_mask);
    for (i=0;i<sizeof(signals)/sizeof(signals[0]);i++) assert(sigaction(signals[i],&a,NULL)==0);
    if (mode == 7 || mode == 8) {
        a.sa_handler = mode == 7 ? SIG_DFL : SIG_IGN;
        assert(sigaction(SIGTERM, &a, NULL) == 0);
    }
    if (mode == 9) {
        sigemptyset(&blocked); sigaddset(&blocked, SIGTERM);
        assert(sigprocmask(SIG_BLOCK, &blocked, NULL) == 0);
    }
    if (mode == 6) { c.aborted = 1; assert(stopped == 0); }
    assert(sigprocmask(SIG_SETMASK, NULL, &mask_before) == 0);
    for (i = 0; i < 7; i++) assert(sigaction(signals[i], NULL, &saved[i]) == 0);
    s.origin=argv[2]; s.ca_file=argv[3]; s.connect_ms=1500; s.total_ms=3000;
    assert(gin_http_global_init()==GIN_HTTP_OK);
    memset(&out,0xa5,sizeof(out));
    /* Observe restoration before the session leader can revoke its TTY. */
    tty_fd = open("/dev/tty", O_RDONLY|O_NOCTTY|O_NONBLOCK|O_CLOEXEC);
    assert(tty_fd >= 0);
    assert(tcgetattr(tty_fd, &tty_before) == 0);
#ifdef LEGACY_COMPOSITION_BRIDGE
    (void)auth_facade;
    ok=bridge(&s,&p,&c,&out);
    printf("ok=%d attempted=%d wiped=0\n",ok,ok);
#else
    {
        struct gin_interactive_result result=gin_interactive_login(&s,&p,"SyntheticUser",&c,&out);
        assert(wipe_calls == 1);
        if (mode == 5) {
            assert(result.reason == GIN_INTERACTIVE_STOPPED);
            assert(result.auth == GIN_AUTH_OK && result.auth_attempted == 1);
            assert(wiped == 1 && stopped == SIGINT);
        }
        if (mode == 6 || (mode >= 7 && mode <= 9)) {
            assert(result.reason == (mode == 6 ? GIN_INTERACTIVE_STOPPED : GIN_INTERACTIVE_CALLER));
            assert(!result.protocol_attempted && !result.tty_attempted && !result.auth_attempted);
            assert(stopped == 0);
            if (mode == 6) assert(c.aborted == 1);
        }
        if (mode == 10) {
            assert(result.reason == GIN_INTERACTIVE_STOPPED);
            assert(result.auth_attempted == 1 && c.aborted == 1);
            assert(stopped == SIGTERM && wiped == 1);
        }
        ok=result.reason==GIN_INTERACTIVE_OK;
        printf("ok=%d attempted=%d wiped=%d protocol_attempted=%d tty_attempted=%d reason=%d\n",
            ok,result.auth_attempted,wiped,result.protocol_attempted,result.tty_attempted,result.reason);
        if (!ok) { const unsigned char *v=(const unsigned char *)&out;
            for(i=0;i<sizeof(out);i++) assert(v[i]==0); }
    }
#endif
    assert(tcgetattr(tty_fd, &tty_after) == 0);
    assert(same_termios(&tty_before, &tty_after));
    assert(close(tty_fd) == 0);
    assert(sigprocmask(SIG_SETMASK, NULL, &mask_after) == 0);
    same_mask(&mask_before, &mask_after);
    for (i = 0; i < 7; i++) {
        assert(sigaction(signals[i], NULL, &observed) == 0);
        assert(saved[i].sa_handler == observed.sa_handler);
        assert(saved[i].sa_flags == observed.sa_flags);
        same_mask(&saved[i].sa_mask, &observed.sa_mask);
    }
    if(ok) assert(gin_credential_validate(&out)==GIN_CREDENTIAL_OK);
    live_password=NULL;
    gin_credential_clear(&out); gin_http_global_cleanup(); return 0;
}
