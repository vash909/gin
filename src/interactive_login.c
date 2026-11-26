#include "interactive_login.h"
#include <signal.h>
#include <string.h>

static int interactive_caller_qualified(void)
{
    const int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU};
    sigset_t mask;
    struct sigaction action;
    size_t i;
    if (sigprocmask(SIG_SETMASK,NULL,&mask) != 0) return 0;
    for (i=0;i<sizeof(signals)/sizeof(signals[0]);i++) {
        if (sigismember(&mask,signals[i]) != 0 ||
            sigaction(signals[i],NULL,&action) != 0 ||
            action.sa_handler == SIG_DFL || action.sa_handler == SIG_IGN) return 0;
    }
    return 1;
}
static int interactive_stopped(struct gin_http_control *control)
{
    unsigned wait_ms=0;
    /* Always tick even when latched: final owner observation follows wipe. */
    if (control->tick(control->opaque,&wait_ms)) control->aborted=1;
    return control->aborted != 0;
}
struct gin_interactive_result gin_interactive_login(
    const struct gin_auth_settings *settings, const struct gin_protocol_policy *policy,
    const char *identifier, struct gin_http_control *control, struct gin_credential *out)
{
    struct gin_interactive_result result;
    char password[4097];
    size_t length=0;
    int qualified=0;
    memset(&result,0,sizeof(result));
    result.reason=GIN_INTERACTIVE_ARGUMENT;
    result.protocol=GIN_PROTOCOL_INVALID;
    result.tty=TP_ARGUMENT;
    result.auth=GIN_AUTH_INVALID;
    if (out) gin_credential_clear(out);
    if (!settings || !policy || !identifier || !out || !control ||
        !control->tick || !control->opaque ||
        (control->aborted != 0 && control->aborted != 1) ||
        !*identifier || strnlen(identifier,GIN_AUTH_IDENTIFIER_MAX+1)>GIN_AUTH_IDENTIFIER_MAX)
        goto done;
    result.reason=GIN_INTERACTIVE_CALLER;
    if (!interactive_caller_qualified()) goto done;
    qualified=1;
    result.reason=GIN_INTERACTIVE_STOPPED;
    if (interactive_stopped(control)) goto done;
    result.protocol_attempted=1;
    result.protocol=gin_protocol_fetch_controlled(settings,policy,&result.server,control);
    result.reason=GIN_INTERACTIVE_PROTOCOL;
    if (result.protocol != GIN_PROTOCOL_OK) goto done;
    result.reason=GIN_INTERACTIVE_STOPPED;
    if (interactive_stopped(control)) goto done;
    result.tty_attempted=1;
    result.tty=terminal_password_read(password,&length,&result.interrupted);
    result.reason=GIN_INTERACTIVE_TTY;
    if (result.tty != TP_OK) goto done;
    result.reason=GIN_INTERACTIVE_STOPPED;
    if (interactive_stopped(control)) goto done;
    result.auth_attempted=1;
    result.auth=gin_auth_login_controlled(settings,identifier,password,out,control);
    result.reason=result.auth==GIN_AUTH_OK ? GIN_INTERACTIVE_OK : GIN_INTERACTIVE_AUTH;
done:
    terminal_password_wipe(password,sizeof(password));
    if (qualified && interactive_stopped(control)) result.reason=GIN_INTERACTIVE_STOPPED;
    if (out && result.reason != GIN_INTERACTIVE_OK) gin_credential_clear(out);
    return result;
}
