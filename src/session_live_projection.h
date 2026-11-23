#ifndef GIN_SESSION_LIVE_PROJECTION_H
#define GIN_SESSION_LIVE_PROJECTION_H
#include "gin_session.h"
#include "gin_topology.h"
#include "gin_render.h"
#include "gin_history.h"
#include "session_live_queue.h"
#include <time.h>
/* Checked wallclock conversion; no clock sampling, unchanged output on failure. */
int gin_live_receive_ms(const struct timespec *wall, int64_t *out);
struct gin_live_projection {
    const struct gin_session_settings *settings;
    const char *subject, *own_nick, *source_target;
    int64_t network_id;
    int query;
};
/* Owner only, outside callbacks. Input is the exact copied source frame.
 * next/record require fresh NULL; terminal invalidates after an owned record. */
enum gin_session_code gin_live_project(const struct gin_live_view *view,
    const struct gin_live_projection *context, const struct gin_topology *topology,
    struct gin_topology **next, struct gin_render_record **record, int *terminal);
/* Explicit owner-only persisted-row projection. Scope must be valid and frozen
 * to this projection; history requires fresh NULL. An absent message ID leaves
 * the legacy record and NULL history (no persisted-progress evidence). A present
 * ID must pass the full live decoder; failures discard newly rendered output. */
enum gin_session_code gin_live_project_history(const struct gin_live_view *view,
    const struct gin_live_projection *context, const struct gin_topology *topology,
    struct gin_topology **next, struct gin_render_record **record, int *terminal,
    const struct gin_history_scope *scope, struct gin_history_page **history);
/* Explicit STATUS-only owner projection, outside callbacks. The owner binds the
 * credential UUID independently of the display subject and supplies the actual
 * CURRENT JOINED Native source index/topic/ref/epoch snapshot. This helper cannot
 * authenticate an index or infer authority from a filename. No window discovery,
 * creation, topology mutation or I/O. Both outputs require fresh NULL and are
 * owned on success; any failure publishes neither. Non-message metadata is ignored
 * only after whole-payload validity/evidence and encryption guards. */
enum gin_session_code gin_live_project_status_history(const struct gin_live_view *view,
    const struct gin_live_projection *context, const struct gin_topology *topology,
    const struct gin_history_scope *scope, struct gin_render_record **record,
    struct gin_history_page **history);
#endif
