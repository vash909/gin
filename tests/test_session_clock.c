#include "session_live_projection.h"
#include <stdint.h>
#include <stdio.h>
/* Explicit checks remain active with NDEBUG. */
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "clock check failed: %s at %d\n", #x, __LINE__); return 1; } } while (0)
extern int gin_live_receive_ms(const struct timespec *, int64_t *);
int main(void)
{
    struct timespec t;
    int64_t out = -42;
    t.tv_sec = 0; t.tv_nsec = 0;
    CHECK(gin_live_receive_ms(&t, &out) == 1 && out == 0);
    t.tv_sec = 1; t.tv_nsec = 999999999;
    CHECK(gin_live_receive_ms(&t, &out) == 1 && out == 1999);
    t.tv_sec = (time_t)(INT64_MAX / 1000); t.tv_nsec = 807999999;
    CHECK(gin_live_receive_ms(&t, &out) == 1 && out == INT64_MAX);
    out = -42; t.tv_nsec = 808000000;
    CHECK(gin_live_receive_ms(&t, &out) == 0 && out == -42);
    t.tv_sec = (time_t)(INT64_MAX / 1000 + 1); t.tv_nsec = 0;
    CHECK(gin_live_receive_ms(&t, &out) == 0 && out == -42);
    t.tv_sec = -1;
    CHECK(gin_live_receive_ms(&t, &out) == 0 && out == -42);
    t.tv_sec = 0; t.tv_nsec = -1;
    CHECK(gin_live_receive_ms(&t, &out) == 0 && out == -42);
    t.tv_nsec = 1000000000;
    CHECK(gin_live_receive_ms(&t, &out) == 0 && out == -42);
    CHECK(gin_live_receive_ms(NULL, &out) == 0 && out == -42);
    CHECK(gin_live_receive_ms(&t, NULL) == 0);
    {
        struct gin_live_view view = {0};
        struct gin_live_projection context = {0};
        struct gin_topology *next = NULL;
        struct gin_render_record *record = NULL;
        int terminal = 7;
        CHECK(gin_live_project(NULL, &context, NULL, &next, &record, &terminal) == GIN_SESSION_INVALID);
        CHECK(terminal == 7 && !next && !record);
        CHECK(gin_live_project(&view, NULL, NULL, &next, &record, &terminal) == GIN_SESSION_INVALID);
        CHECK(gin_live_project(&view, &context, NULL, NULL, &record, &terminal) == GIN_SESSION_INVALID);
        CHECK(gin_live_project(&view, &context, NULL, &next, NULL, &terminal) == GIN_SESSION_INVALID);
        CHECK(gin_live_project(&view, &context, NULL, &next, &record, NULL) == GIN_SESSION_INVALID);
    }
    puts("session receive clock and private argument checks passed");
    return 0;
}
