#ifndef TERMINAL_PASSWORD_H
#define TERMINAL_PASSWORD_H
#include <stddef.h>
#define TP_PASSWORD_MAX 4096
/* Private synchronous API; main thread only, no concurrent signal/TTY changes.
 * out must address 4097 bytes; length and interrupted must be non-NULL.
 * TP_OK permits using out only subject to caller stop policy below.
 * Every failure wipes all 4097 bytes and sets
 * length=0. interrupted reports the first caught cancellation signal, even if
 * TP_RESTORE takes precedence. No signal is re-raised: caller decides exit policy.
 * SIGTSTP/TTIN/TTOU cancel rather than suspend; foreground controlling TTY only.
 * Reads /dev/tty, not stdin. UTF-8 1..4096 bytes; whitespace is preserved.
 * LF/CR submits; Ctrl-D cancels; DEL/BS removes one UTF-8 character; Ctrl-U clears.
 * Overflow drains through newline/EOF/cancellation with echo disabled. Failure
 * flushes queued input before restoration. Bytes typed AFTER return belong to
 * the caller's terminal, not this reader. No echo, masking, network or storage.
 * Exact termios, signal dispositions and mask are restored or TP_RESTORE is
 * returned. Caller MUST NOT authenticate on any failure, especially TP_RESTORE.
 * Cancellation policy precondition: ALL cancellation signals relied upon by
 * the frontend must be initially unblocked. Originally blocked signals remain
 * blocked, including during waits, and may remain pending after TP_OK.
 * Successful submit is NOT a cancellation barrier: a late pending signal may
 * run a restored returning caller handler without appearing in interrupted.
 * The future frontend must own nonignoring stop handlers, check its stop flag
 * before authentication even after TP_OK, and wipe all 4097 bytes on ALL paths.
 * These requirements do not constitute frontend/authentication integration.
 * Best-effort volatile wipe cannot erase kernel/library copies, swap, dumps,
 * or defeat SIGKILL/SIGSTOP, OS failure or process death. No cleanup guarantee
 * exists for uncatchable signals. Restoring an already-hung-up TTY may fail.
 */
enum tp_status { TP_OK, TP_ARGUMENT, TP_TTY, TP_EOF, TP_INVALID,
                 TP_OVERLONG, TP_INTERRUPTED, TP_IO, TP_RESTORE };
enum tp_status terminal_password_read(char out[4097], size_t *length,
                                      int *interrupted);
void terminal_password_wipe(void *p, size_t n);
#endif
