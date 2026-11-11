/* Native synthetic verified HTTPS/WSS fixture, not connected CLI wiring. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_liveness.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct owner {
    unsigned events;
    unsigned char payload[1024];
    size_t length;
    uint64_t epoch;
};
/* Borrowed data is only copied; owner commands run after service returns. */
static void frame(void *user, const struct gin_phoenix_frame *f, uint64_t epoch)
{
    struct owner *o = user;
    const struct gin_phoenix_fields *p = gin_phoenix_frame_fields(f);
    assert(p && p->payload.length <= sizeof(o->payload));
    memcpy(o->payload, p->payload.data, p->payload.length);
    o->length = p->payload.length;
    o->epoch = epoch;
    o->events++;
}
static double now(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (double)t.tv_sec + (double)t.tv_nsec / 1000000000.0;
}
static void report(struct gin_connect *s, const struct owner *o, int step)
{
    struct gin_liveness_status l = gin_connect_liveness_status(s);
    struct gin_connect_result r = gin_connect_last_result(s);
    struct gin_query_result q = gin_connect_query_result(s);
    printf("{\"code\":\"%s\",\"connection\":%d,\"state\":%d,\"reason\":%d,"
        "\"heartbeat\":%d,\"used\":%u,\"limit\":%u,\"wait\":%u,"
        "\"epoch\":%llu,\"events\":%u,\"event_epoch\":%llu,\"step\":%d,"
        "\"query\":%d,\"query_code\":%d,\"operation\":%llu,\"query_epoch\":%llu,"
        "\"query_ref\":\"%s\",\"query_join\":\"%s\",\"http\":%ld}\n",
        gin_connect_error(r.code), (int)gin_connect_get_state(s), (int)l.state,
        (int)l.reason, (int)l.heartbeat, l.retries_used, l.retries_limit,
        l.retry_wait_ms, (unsigned long long)l.socket_epoch, o->events,
        (unsigned long long)o->epoch, step, (int)q.outcome, (int)q.code,
        (unsigned long long)q.operation_id, (unsigned long long)q.socket_epoch,
        q.request_ref, q.join_ref, r.http_status);
    assert(fflush(stdout) == 0);
}
int main(int argc, char **argv)
{
    const char boot[] = "{\"networks\":[{\"id\":7,\"slug\":\"Net\",\"nick\":\"Self\"}],\"channels\":{\"Net\":[{\"name\":\"#Room\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
    struct gin_connect_settings settings;
    struct gin_liveness_settings live;
    struct gin_credential c = {0}, saved;
    struct gin_connect_subscription plan = {"Net", "#Room"};
    struct gin_connect *s = NULL;
    struct gin_topology *topology = NULL;
    struct owner o = {0};
    char line[128], command[24];
    unsigned a, b;
    int instrumented = getenv("GIN_LIVENESS_PREFIX") != NULL;
    assert(argc == 8);
    assert(strlen(argv[1]) < sizeof(c.origin));
    strcpy(c.origin, argv[1]); strcpy(c.token, "aa~");
    strcpy(c.subject_name, "Alice");
    strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc"); saved = c;
    settings.http.origin = c.origin; settings.http.ca_file = argv[2];
    settings.http.connect_ms = instrumented ? 10000 : 2000;
    settings.http.total_ms = instrumented ? 20000 : 4000;
    settings.policy.client_protocol = 4; settings.policy.required_server_protocol = 4;
    settings.join_ms = instrumented ? 10000 : 4000;
    live.heartbeat_interval_ms = (unsigned)strtoul(argv[3], NULL, 10);
    live.heartbeat_timeout_ms = (unsigned)strtoul(argv[4], NULL, 10);
    live.retry_base_ms = (unsigned)strtoul(argv[5], NULL, 10);
    live.retry_cap_ms = (unsigned)strtoul(argv[6], NULL, 10);
    live.max_retries = (unsigned)strtoul(argv[7], NULL, 10);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &topology) == GIN_TOPOLOGY_OK);
    assert(gin_connect_new(&settings, &c, &plan, 1, frame, &o, &s) == GIN_CONNECT_OK);
    assert(gin_connect_set_liveness(s, &live) == GIN_CONNECT_OK);
    report(s, &o, -1);
    while (fgets(line, sizeof(line), stdin)) {
        int step = -1;
        a = b = 0;
        assert(sscanf(line, "%23s %u %u", command, &a, &b) >= 1);
        if (!strcmp(command, "start")) step = (int)gin_connect_start(s);
        else if (!strcmp(command, "retry")) step = (int)gin_connect_retry(s);
        else if (!strcmp(command, "service")) step = (int)gin_connect_service(s, a);
        else if (!strcmp(command, "close")) gin_connect_close(s);
        else if (!strcmp(command, "query")) step = (int)gin_connect_open_query(s, topology,
            "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 42, a);
        else if (!strcmp(command, "take")) {
            struct gin_query_result q;
            step = gin_connect_query_take(s, &q);
        } else if (!strcmp(command, "drive") || !strcmp(command, "until")) {
            double end;
            assert(a <= 60000 && b <= 10000);
            end = now() + (double)a / 1000.0;
            do {
                if (gin_connect_get_state(s) == GIN_CONNECT_FAILED ||
                    gin_connect_get_state(s) == GIN_CONNECT_STOPPED) break;
                if (!strcmp(command, "until") && o.events >= b) break;
                (void)gin_connect_service(s, 20);
            } while (now() < end);
        } else if (strcmp(command, "status")) assert(!"unknown owner command");
        assert(!memcmp(&c, &saved, sizeof(c)));
        report(s, &o, step);
    }
    assert(!ferror(stdin));
    gin_connect_free(s); gin_topology_free(topology); gin_http_global_cleanup();
    return 0;
}
