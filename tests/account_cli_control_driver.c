/* Private typed-result mapping test, not an actual terminal restore fault. */
#include "interactive_login.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>
static int owner_stop, restore_error, calls;
static struct gin_interactive_result controlled_login(
    const struct gin_auth_settings *http, const struct gin_protocol_policy *policy,
    const char *identifier, struct gin_http_control *control,
    struct gin_credential *credential)
{
    struct gin_interactive_result result = {0};
    (void)http; (void)policy; (void)identifier; (void)control; (void)credential;
    calls++;
    result.reason = GIN_INTERACTIVE_STOPPED;
    result.tty_attempted = 1;
    result.tty = restore_error ? TP_RESTORE : TP_INTERRUPTED;
    result.interrupted = SIGINT;
    if (owner_stop && raise(SIGTERM) != 0) calls += 100;
    return result;
}
#define gin_interactive_login controlled_login
#include "../src/account_cli.c"
#undef gin_interactive_login
static void returning_handler(int number) { (void)number; }
static int same_mask(const sigset_t *a, const sigset_t *b)
{
    int i;
    for (i = 1; i <= SIGRTMAX; i++) {
        int x = sigismember(a, i), y = sigismember(b, i);
        if (x != y) return 0;
    }
    return 1;
}
int main(int argc, char **argv)
{
    const int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGPIPE};
    struct sigaction action, before[8], after;
    sigset_t mask, current;
    char *args[] = {"gin", "https://127.0.0.1:1", "SyntheticUser", NULL};
    size_t i;
    int got, expected;
    if (argc != 2) return 2;
    if (!strcmp(argv[1], "restore-owner")) { restore_error = 1; owner_stop = 1; }
    else if (!strcmp(argv[1], "restore-interrupted")) restore_error = 1;
    else if (!strcmp(argv[1], "interrupted")) { /* no outer stop */ }
    else return 2;
    memset(&action, 0, sizeof(action));
    action.sa_handler = returning_handler; sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR1);
    for (i = 0; i < 8; i++) {
        if (sigaction(signals[i], &action, NULL) || sigaction(signals[i], NULL, &before[i])) return 3;
    }
    if (sigprocmask(SIG_SETMASK, NULL, &mask)) return 3;
    got = gin_account_cli(3, args); expected = restore_error ? 1 : 0;
    if (sigprocmask(SIG_SETMASK, NULL, &current) || !same_mask(&mask, &current)) return 4;
    for (i = 0; i < 8; i++) {
        if (sigaction(signals[i], NULL, &after) ||
            after.sa_handler != before[i].sa_handler || after.sa_flags != before[i].sa_flags ||
            !same_mask(&after.sa_mask, &before[i].sa_mask)) return 4;
    }
    if (calls != 1 || got != expected) {
        fprintf(stderr, "FAIL typed-result expected=%d actual=%d calls=%d\n", expected, got, calls);
        return 1;
    }
    puts("PASS private typed-result mapping and signal restoration");
    return 0;
}
