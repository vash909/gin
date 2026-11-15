#ifndef GIN_CONNECT_CONTROL_H
#define GIN_CONNECT_CONTROL_H
#include "gin_liveness.h"
#include "http_control.h"
/* Private synchronous opt-in. Borrow control only for this call; never retain
 * it in the connection. Controlled upgrade retains Native-owned multi/easy
 * membership for WS I/O; the owner context is not retained. */
enum gin_connect_code gin_connect_start_controlled(struct gin_connect *,
    struct gin_http_control *);
enum gin_retry_step gin_connect_retry_controlled(struct gin_connect *,
    struct gin_http_control *);
/* Unexpected cleanup failure is sticky and prohibits reuse. A quarantined
 * connection deliberately retains referenced storage; do not tear down curl
 * global state underneath it. Process restart is required in that anomaly. */
int gin_connect_cleanup_failed(const struct gin_connect *);
int gin_connect_quarantined(const struct gin_connect *);
/* Reduce a bounded HTTP wait using Native deadlines/productive backlog;
 * zero means immediate service, not that a partial frame is complete. */
enum gin_connect_code gin_connect_http_wait_budget(struct gin_connect *, unsigned *);
#endif
