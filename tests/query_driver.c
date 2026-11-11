/* Synthetic trusted WSS only; no live credentials or connected CLI. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_connect.h"
#include "gin_command.h"
#include "gin_fs.h"
#include "gin_input.h"
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct owner {
    struct gin_connect *s;
    struct gin_topology *topology;
    unsigned snapshots, pending_snapshots, accepted_snapshots;
    unsigned char payloads[4][1024];
    size_t lengths[4];
};
static void frame(void *user, const struct gin_phoenix_frame *f, uint64_t epoch)
{
    struct owner *o = user;
    const struct gin_phoenix_fields *fields = gin_phoenix_frame_fields(f);
    struct gin_query_result r = gin_connect_query_result(o->s), sentinel = r;
    assert(epoch == 1);
    assert(gin_connect_open_query(o->s, o->topology, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 9, 100) == GIN_QUERY_INVALID);
    assert(gin_connect_query_take(o->s, &sentinel) == -1);
    assert(!memcmp(&sentinel, &r, sizeof(r)));
    assert(o->snapshots < 4 && fields->payload.length <= sizeof(o->payloads[0]));
    memcpy(o->payloads[o->snapshots], fields->payload.data, fields->payload.length);
    o->lengths[o->snapshots++] = fields->payload.length;
    if (r.outcome == GIN_QUERY_PENDING) o->pending_snapshots++;
    if (r.outcome == GIN_QUERY_ACCEPTED) o->accepted_snapshots++;
}
int main(int argc, char **argv)
{
    const char boot[] = "{\"networks\":[{\"id\":7,\"slug\":\"Net\",\"nick\":\"Self\"}],\"channels\":{\"Net\":[{\"name\":\"#Room\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
    struct gin_connect_settings settings;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record;
    struct gin_command command;
    struct gin_credential c = {0};
    struct owner o = {0};
    struct gin_query_result r;
    enum gin_connect_code code;
    unsigned i;
    int floor, fd;
    unsigned deadline;
    assert(argc == 5);
    floor = !strcmp(argv[3], "config-floor") || !strcmp(argv[3], "root-floor");
    assert(strlen(argv[1]) < sizeof(c.origin)); strcpy(c.origin, argv[1]);
    strcpy(c.token, "aa~"); strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    settings.http.origin = c.origin; settings.http.ca_file = argv[2]; settings.http.connect_ms = 1500; settings.http.total_ms = 3000;
    settings.policy.client_protocol = 4; settings.policy.required_server_protocol = 1; settings.join_ms = 1000;
    deadline = 150;
    if (getenv("GIN_QUERY_PREFIX") != NULL) {
        settings.http.connect_ms = 5000; settings.http.total_ms = 10000;
        settings.join_ms = 5000;
        if (strcmp(argv[3], "silent")) deadline = 5000;
    }
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &o.topology) == GIN_TOPOLOGY_OK);
    assert(gin_connect_new(&settings, &c, NULL, 0, frame, &o, &o.s) == GIN_CONNECT_OK);
    code = gin_connect_start(o.s);
    for (i = 0; code == GIN_CONNECT_OK && gin_connect_get_state(o.s) != GIN_CONNECT_READY && i < 50; i++) code = gin_connect_service(o.s, 100);
    assert(code == GIN_CONNECT_OK && gin_connect_get_state(o.s) == GIN_CONNECT_READY);
    assert(gin_root_acquire(argv[4], &root) == 0);
    assert(gin_root_init(&root, "Net", "#Room", "#") == 0);
    fd = gin_root_input_open(&root, "Net", "#Room", "#");
    assert(fd >= 0);
    puts("fifo-ready"); assert(fflush(stdout) == 0);
    for (i = 0; i < 1000; i++) {
        enum gin_input_result ir = gin_input_next(&input, fd, &record);
        struct pollfd p = {fd, POLLIN, 0};
        if (ir == GIN_INPUT_LINE) break;
        assert(ir == GIN_INPUT_MORE || ir == GIN_INPUT_AGAIN ||
               ir == GIN_INPUT_INTERRUPTED || ir == GIN_INPUT_EOF);
        if (ir == GIN_INPUT_EOF) (void)poll(NULL, 0, 10);
        else (void)poll(&p, 1, 10);
    }
    assert(i < 1000);
    if (!strcmp(argv[3], "rejected")) {
        assert(gin_command_parse(record.data, record.length, "#", &command) == GIN_COMMAND_UNSUPPORTED);
        assert(gin_connect_query_result(o.s).outcome == GIN_QUERY_NONE);
        goto done;
    }
    assert(gin_command_parse(record.data, record.length, "#", &command) == GIN_COMMAND_OK);
    assert(command.kind == GIN_COMMAND_QUERY);
    assert(gin_connect_open_query(o.s, o.topology, "Net", (const char *)command.data, "#", GIN_QUERY_MODE_PLAIN, 42, deadline) == (floor ? GIN_QUERY_UNSUPPORTED : GIN_QUERY_OK));
    if (!floor) {
        assert(gin_connect_query_take(o.s, &r) == 0);
        for (i = 0; gin_connect_query_result(o.s).outcome == GIN_QUERY_PENDING && i < 100; i++) (void)gin_connect_service(o.s, 1000);
        r = gin_connect_query_result(o.s);
        if (!strcmp(argv[3], "error")) assert(r.code == GIN_QUERY_REMOTE && r.outcome == GIN_QUERY_REMOTE_FAILURE);
        else if (!strcmp(argv[3], "unknown") || !strcmp(argv[3], "malformed")) assert(r.code == GIN_QUERY_MALFORMED && r.outcome == GIN_QUERY_UNCERTAIN);
        else if (!strcmp(argv[3], "silent")) assert(r.code == GIN_QUERY_TIMEOUT && r.outcome == GIN_QUERY_UNCERTAIN);
        else if (!strcmp(argv[3], "disconnect") || !strcmp(argv[3], "reconnect")) assert(r.outcome == GIN_QUERY_UNCERTAIN);
        else assert(r.code == GIN_QUERY_OK && r.outcome == GIN_QUERY_ACCEPTED);
        assert(r.operation_id == 42 && r.socket_epoch == 1 && !strcmp(r.join_ref, "1") && !strcmp(r.request_ref, "2"));
        assert(gin_connect_open_query(o.s, o.topology, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 43, 100) == GIN_QUERY_BUSY);
        if (!strcmp(argv[3], "before") || !strcmp(argv[3], "after")) {
            for (i = 0; o.snapshots < 2 && i < 50; i++) assert(gin_connect_service(o.s, 50) == GIN_CONNECT_OK);
            assert(o.snapshots == 2);
            for (i = 0; i < o.snapshots; i++) {
                struct gin_topology *next = NULL;
                assert(gin_topology_apply_queries(o.topology, o.payloads[i], o.lengths[i], &next) == GIN_TOPOLOGY_OK);
                gin_topology_free(o.topology); o.topology = next;
                if (i == 0) assert(gin_topology_find_window(o.topology, 7, "bob"));
            }
            assert(gin_topology_queries_known(o.topology));
            assert(gin_topology_window_count(o.topology) == 1 && !gin_topology_find_window(o.topology, 7, "bob"));
            assert(o.pending_snapshots == (!strcmp(argv[3], "before") ? 1u : 0u));
            assert(o.accepted_snapshots == (!strcmp(argv[3], "before") ? 1u : 2u));
        } else assert(!gin_topology_queries_known(o.topology));
        gin_connect_close(o.s);
        assert(gin_connect_start(o.s) == GIN_CONNECT_INVALID);
        assert(gin_connect_query_take(o.s, &r) == 1);
        assert(gin_connect_query_take(o.s, &r) == 0);
        if (!strcmp(argv[3], "reconnect")) {
            /* Explicit owner restart with a new operation 43; never a replay or retry of 42. */
            code = gin_connect_start(o.s);
            assert(code == GIN_CONNECT_OK);
            for (i = 0; code == GIN_CONNECT_OK && gin_connect_get_state(o.s) != GIN_CONNECT_READY && i < 50; i++) code = gin_connect_service(o.s, 100);
            assert(code == GIN_CONNECT_OK && gin_connect_get_state(o.s) == GIN_CONNECT_READY);
            assert(gin_connect_open_query(o.s, o.topology, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 43, deadline) == GIN_QUERY_OK);
            assert(gin_connect_query_take(o.s, &r) == 0);
            for (i = 0; gin_connect_query_result(o.s).outcome == GIN_QUERY_PENDING && i < 100; i++) (void)gin_connect_service(o.s, 1000);
            r = gin_connect_query_result(o.s);
            assert(r.code == GIN_QUERY_OK && r.outcome == GIN_QUERY_ACCEPTED);
            assert(r.operation_id == 43 && r.socket_epoch == 2 && !strcmp(r.join_ref, "3") && !strcmp(r.request_ref, "4"));
            assert(gin_connect_open_query(o.s, o.topology, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 44, 100) == GIN_QUERY_BUSY);
            gin_connect_close(o.s);
            assert(gin_connect_query_take(o.s, &r) == 1);
            assert(r.code == GIN_QUERY_OK && r.outcome == GIN_QUERY_ACCEPTED && r.operation_id == 43 && r.socket_epoch == 2 && !strcmp(r.join_ref, "3") && !strcmp(r.request_ref, "4"));
            assert(gin_connect_query_take(o.s, &r) == 0);
        }
    }
done:
    printf("query %s verified\n", argv[3]);
    assert(close(fd) == 0);
    assert(gin_root_release(&root) == 0);
    gin_connect_free(o.s); gin_topology_free(o.topology); gin_http_global_cleanup(); return 0;
}
