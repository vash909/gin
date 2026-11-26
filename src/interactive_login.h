#ifndef GIN_INTERACTIVE_LOGIN_H
#define GIN_INTERACTIVE_LOGIN_H
#include "http_control.h"
#include "terminal_password.h"
enum gin_interactive_reason {
    GIN_INTERACTIVE_OK, GIN_INTERACTIVE_ARGUMENT, GIN_INTERACTIVE_CALLER,
    GIN_INTERACTIVE_STOPPED, GIN_INTERACTIVE_PROTOCOL,
    GIN_INTERACTIVE_TTY, GIN_INTERACTIVE_AUTH
};
struct gin_interactive_result {
    enum gin_interactive_reason reason;
    enum gin_protocol_code protocol;
    enum tp_status tty;
    enum gin_auth_code auth;
    int protocol_attempted, tty_attempted, auth_attempted;
    int interrupted;
    struct gin_protocol_info server;
};
/* Private synchronous main-thread composition, not a CLI/account owner.
 * Caller owns HTTP global initialization, handlers and all signal policy.
 * All seven TTY cancellation signals must initially be unblocked and have
 * returning nondefault/nonignoring handlers; tick must observe owner stop.
 * Borrow non-NULL control with initialized tick/opaque/aborted for this call;
 * aborted is initially zero, or a previously latched one (never reset here).
 * Inputs/output must not alias; caller keeps inputs valid for the whole call.
 * Only successful output is a memory credential; no persistence/retry/logout.
 * Cancellation is not race-atomic with server mutation: an aborted POST may
 * create a session. A successful TTY read is not a cancellation barrier.
 * Full local password wipe is best-effort, not OS/kernel/Jansson erasure;
 * SIGKILL/SIGSTOP, process death, swap and dumps remain outside this guarantee.
 */
struct gin_interactive_result gin_interactive_login(
    const struct gin_auth_settings *, const struct gin_protocol_policy *,
    const char *, struct gin_http_control *, struct gin_credential *);
#endif
