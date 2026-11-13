#ifndef GIN_SESSION_LIVE_PROJECTION_H
#define GIN_SESSION_LIVE_PROJECTION_H
#include "gin_session.h"
#include "gin_topology.h"
#include "gin_render.h"
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
#endif
