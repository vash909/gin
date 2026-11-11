#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_render.h"
#include "gin_connect.h"
#include "gin_fs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct owner {
    struct gin_connect *connection;
    struct gin_render_record *queue[8];
    size_t count, bytes, delivered, errors, overflow;
    int inside;
};
static void frame(void *user, const struct gin_phoenix_frame *f, uint64_t epoch)
{
    struct owner *o = user;
    struct gin_render_record *r = NULL;
    const struct gin_connect_topic_status *source;
    struct gin_render_context c = {"Alice", "Net", "#ROOM", "#ROOM", "#", NULL, 0,
        GIN_RENDER_CHANNEL, GIN_RENDER_MODE_PLAIN};
    enum gin_render_code code;
    size_t i;
    o->inside = 1;
    source = NULL;
    for (i = 0; i < gin_connect_topic_count(o->connection); i++) {
        const struct gin_connect_topic_status *s = gin_connect_topic_at(o->connection, i);
        if (!strcmp(s->topic, gin_phoenix_frame_fields(f)->topic)) source = s;
    }
    assert(source && source->state == GIN_CONNECT_TOPIC_JOINED);
    c.join_ref = source->join_ref;
    c.socket_epoch = gin_connect_last_result(o->connection).socket_epoch;
    code = gin_render_event(f, &c, epoch, 0, &r);
    o->delivered++;
    if (code == GIN_RENDER_OK) {
        size_t n = gin_render_record_bytes(r).length;
        if (o->count == 8 || n > 262144u - o->bytes) {
            o->overflow++; gin_render_record_free(r);
        } else { o->queue[o->count++] = r; o->bytes += n; }
    } else if (code != GIN_RENDER_IGNORE) o->errors++;
    o->inside = 0;
}
int main(int argc, char **argv)
{
    struct owner o;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_credential c;
    struct gin_connect_settings s;
    struct gin_connect_subscription plan = {"Net", "#ROOM"};
    unsigned tries;
    size_t i, written = 0;
    int fd;
    assert(argc == 5); memset(&o, 0, sizeof(o)); memset(&c, 0, sizeof(c));
    assert(strlen(argv[1]) < sizeof(c.origin)); strcpy(c.origin, argv[1]);
    strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    strcpy(c.token, "aa~"); /* Public, local fixture credential only. */
    s.http.origin = c.origin; s.http.ca_file = argv[2];
    s.http.connect_ms = 5000; s.http.total_ms = 10000;
    s.policy.client_protocol = 4; s.policy.required_server_protocol = 4; s.join_ms = 5000;
    assert(gin_root_acquire(argv[3], &root) == 0);
    assert(gin_root_init(&root, "Net", "#ROOM", "#") == 0);
    fd = gin_root_output_open(&root, "Net", "#ROOM", "#"); assert(fd >= 0);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_connect_new(&s, &c, &plan, 1, frame, &o, &o.connection) == GIN_CONNECT_OK);
    assert(gin_connect_start(o.connection) == GIN_CONNECT_OK);
    for (tries = 0; o.delivered < (size_t)strtoul(argv[4], NULL, 10) && tries < 100; tries++)
        assert(gin_connect_service(o.connection, 100) == GIN_CONNECT_OK);
    assert(o.delivered == (size_t)strtoul(argv[4], NULL, 10));
    /* The callback's borrowed frames have all been destroyed by service. */
    for (i = 0; i < o.count; i++) {
        size_t n; assert(!o.inside);
        assert(gin_render_append(fd, o.queue[i], &n) == GIN_RENDER_OK);
        written += n; gin_render_record_free(o.queue[i]);
    }
    printf("records=%lu errors=%lu overflow=%lu bytes=%lu\n", (unsigned long)o.count,
        (unsigned long)o.errors, (unsigned long)o.overflow, (unsigned long)written);
    gin_connect_free(o.connection); gin_http_global_cleanup();
    assert(close(fd) == 0 && gin_root_release(&root) == 0);
    return 0;
}
