#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_send.h"
#include "gin_fs.h"
#include "gin_input.h"
#include "gin_connect.h"
#include "gin_render.h"
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
struct receiver {
    struct gin_connect *connection;
    struct gin_render_record *queue[2];
    unsigned received;
    int inside;
};
static void receive(void *user, const struct gin_phoenix_frame *frame, uint64_t epoch)
{
    struct receiver *o = user;
    struct gin_render_context c = {"Alice", "Net", "#ROOM", "#ROOM", "#", NULL, 0,
        GIN_RENDER_CHANNEL, GIN_RENDER_MODE_PLAIN};
    const struct gin_connect_topic_status *topic = gin_connect_topic_at(o->connection, 2);
    o->inside = 1;
    assert(topic && topic->state == GIN_CONNECT_TOPIC_JOINED && o->received < 2);
    c.join_ref = topic->join_ref;
    c.socket_epoch = gin_connect_last_result(o->connection).socket_epoch;
    assert(gin_render_event(frame, &c, epoch, 0, &o->queue[o->received]) == GIN_RENDER_OK);
    o->received++;
    o->inside = 0;
}
static void drain(struct receiver *o, int output, unsigned target)
{
    unsigned tries, i;
    size_t written;
    for (tries = 0; o->received < target && tries < 200; tries++)
        assert(gin_connect_service(o->connection, 100) == GIN_CONNECT_OK);
    assert(o->received == target && !o->inside);
    for (i = 0; i < target; i++) {
        assert(o->queue[i]);
        assert(gin_render_append(output, o->queue[i], &written) == GIN_RENDER_OK);
        gin_render_record_free(o->queue[i]); o->queue[i] = NULL;
    }
}
int main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_credential c;
    struct gin_send_settings s;
    struct gin_send_route route = {"Net", "#ROOM", "#", GIN_SEND_ROLE_CHANNEL, GIN_SEND_MODE_PLAIN};
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record;
    unsigned count = 0, expected, loops = 0;
    int fd, output = -1;
    struct receiver receiver = {0};
    assert(argc == 6); memset(&c, 0, sizeof(c));
    assert(strlen(argv[1]) < sizeof(c.origin)); strcpy(c.origin, argv[1]);
    strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    strcpy(c.token, "aa~");
    s.auth.origin = argv[1]; s.auth.ca_file = argv[2];
    s.auth.connect_ms = 5000; s.auth.total_ms = 10000;
    if (!strcmp(argv[4], "timeout")) s.auth.total_ms = 5000;
    s.protocol.client_protocol = 4; s.protocol.required_server_protocol = 4;
    if (!strcmp(argv[4], "query")) { route.wire_target = "Nick"; route.role = GIN_SEND_ROLE_QUERY; }
    if (!strcmp(argv[4], "origin")) c.origin[8] = c.origin[8] == '1' ? '2' : '1';
    if (!strcmp(argv[4], "mode")) route.mode = GIN_SEND_MODE_ENCRYPTED;
    if (!strcmp(argv[4], "unknown")) route.role = GIN_SEND_ROLE_UNKNOWN;
    if (!strcmp(argv[4], "deadline")) s.auth.total_ms = GIN_HTTP_TIMEOUT_MAX + 1;
    assert(gin_root_acquire(argv[3], &root) == 0);
    assert(gin_root_init(&root, "Net", "#ROOM", "#") == 0);
    fd = gin_root_input_open(&root, "Net", "#ROOM", "#"); assert(fd >= 0);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    if (!strcmp(argv[4], "echo")) {
        struct gin_connect_settings cs;
        struct gin_connect_subscription plan = {"Net", "#ROOM"};
        cs.http = s.auth; cs.policy = s.protocol; cs.join_ms = 5000;
        output = gin_root_output_open(&root, "Net", "#ROOM", "#"); assert(output >= 0);
        assert(gin_connect_new(&cs, &c, &plan, 1, receive, &receiver, &receiver.connection) == GIN_CONNECT_OK);
        assert(gin_connect_start(receiver.connection) == GIN_CONNECT_OK);
        for (loops = 0; gin_connect_get_state(receiver.connection) != GIN_CONNECT_READY && loops < 200; loops++)
            assert(gin_connect_service(receiver.connection, 100) == GIN_CONNECT_OK);
        assert(gin_connect_get_state(receiver.connection) == GIN_CONNECT_READY);
        loops = 0;
    }
    expected = (unsigned)strtoul(argv[5], NULL, 10);
    puts("ready"); assert(fflush(stdout) == 0);
    while (count < expected && ++loops < 3000) {
        enum gin_input_result ir = gin_input_next(&input, fd, &record);
        if (ir == GIN_INPUT_LINE) {
            uint64_t id = !strcmp(argv[4], "id") ? 0 : (uint64_t)count + 101;
            struct gin_send_result r = gin_send_plain(!strcmp(argv[4], "root") ? NULL : &root,
                &s, &c, &route, id, record.data, record.length);
            assert(r.request_id == id);
            printf("%s %s %llu %lld %ld %d %d\n", gin_send_error(r.code),
                gin_send_outcome_name(r.outcome), (unsigned long long)r.request_id,
                (long long)r.message_id, r.http_status, r.auth_attempted, r.post_attempted);
            count++;
        } else if (ir == GIN_INPUT_REJECTED || ir == GIN_INPUT_EMPTY) {
            puts("rejected"); count++;
        } else {
            struct pollfd p;
            assert(ir != GIN_INPUT_ERROR);
            p.fd = fd; p.events = POLLIN; p.revents = 0;
            /* Bound EOF/HUP spinning even when no writer is connected yet. */
            if (ir == GIN_INPUT_EOF) (void)poll(NULL, 0, 10);
            else (void)poll(&p, 1, 10);
        }
        assert(fflush(stdout) == 0);
    }
    assert(count == expected);
    if (receiver.connection) {
        drain(&receiver, output, 2);
        gin_connect_free(receiver.connection);
        assert(close(output) == 0);
    }
    gin_credential_clear(&c); gin_http_global_cleanup();
    assert(close(fd) == 0); assert(gin_root_release(&root) == 0);
    return 0;
}
