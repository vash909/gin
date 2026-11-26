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
/* Private cold opt-in: only IDLE/STOPPED, outside callbacks, with no owned
 * transport, live FD, membership, cleanup failure/quarantine or query receipt.
 * Take even terminal query receipts explicitly before calling. No implicit
 * close, I/O, start, JOIN, query or send. count <=64; NULL plan requires count=0;
 * a non-NULL unused plan is also valid at count=0. Constructor validation,
 * deep copies, ordering and the 64-topic limit including root still apply.
 * Failure leaves all existing bytes and borrowed views unchanged. Success
 * replaces ONLY topics/count, preserving refs, epoch and owner retry authority.
 * Exception to public stable-view lifetime: success invalidates ALL previously
 * borrowed topic status/views. Callers must clear old copied queues and use the
 * next actual socket epoch before rebinding. This does not qualify WSS recovery.
 */
enum gin_connect_code gin_connect_replan_closed(struct gin_connect *,
    const struct gin_connect_subscription *, size_t count);
/* Reduce a bounded HTTP wait using Native deadlines/productive backlog;
 * zero means immediate service, not that a partial frame is complete. */
enum gin_connect_code gin_connect_http_wait_budget(struct gin_connect *, unsigned *);
#endif
