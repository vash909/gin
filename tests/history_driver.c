/* Synthetic native HTTPS/WSS -> owned history -> existing held-root append.
 * No connected CLI, durable checkpoint or complete-archive claim. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_history.h"
#include "gin_connect.h"
#include "gin_fs.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define QUEUE_MAX 8u
#define QUEUE_BYTES ((size_t)65536)
struct owner {
    struct gin_connect *session;
    struct gin_history_scope scope;
    struct gin_history_page *queue[QUEUE_MAX];
    size_t count, bytes, count_cap, byte_cap;
    unsigned received, ignored, overflow_count, overflow_bytes;
    enum gin_history_code callback_code;
    int in_callback;
    const char *source;
};
static uint64_t now_ms(void)
{
    struct timespec t;
    assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
/* Read-only current-source getters, pure decoding and bounded owned copies only.
 * No HTTP, filesystem, planner feed or connection reentry in this callback. */
static void frame(void *user, const struct gin_phoenix_frame *f, uint64_t epoch)
{
    struct owner *o = user;
    struct gin_history_page *p = NULL;
    struct gin_render_context c;
    const struct gin_connect_topic_status *source;
    enum gin_history_code code;
    size_t bytes;
    o->in_callback = 1;
    source = gin_connect_topic_at(o->session, 2);
    if (!source || source->state != GIN_CONNECT_TOPIC_JOINED) {
        o->ignored++; o->in_callback = 0; return;
    }
    o->scope.socket_epoch = gin_connect_last_result(o->session).socket_epoch;
    c.subject = "Alice"; c.network = "Net"; c.target = o->scope.target;
    c.source_target = o->source; c.chantypes = "#"; c.join_ref = source->join_ref;
    c.socket_epoch = o->scope.socket_epoch; c.role = o->scope.role;
    c.encryption = GIN_RENDER_MODE_PLAIN;
    code = gin_history_live_decode(f, &c, &o->scope, epoch, &p);
    if (code == GIN_HISTORY_IGNORE) o->ignored++;
    else if (code != GIN_HISTORY_OK) o->callback_code = code;
    else {
        o->received++;
        bytes = gin_history_page_owned_bytes(p);
        if (o->count == o->count_cap) o->overflow_count++;
        else if (bytes > o->byte_cap - o->bytes) o->overflow_bytes++;
        else { o->queue[o->count++] = p; o->bytes += bytes; p = NULL; }
    }
    gin_history_page_free(p);
    o->in_callback = 0;
}
static void pump(struct owner *o, unsigned received)
{
    uint64_t end = now_ms() + 10000;
    do {
        assert(gin_connect_get_state(o->session) != GIN_CONNECT_FAILED);
        assert(gin_connect_get_state(o->session) != GIN_CONNECT_STOPPED);
        assert(gin_connect_service(o->session, 20) == GIN_CONNECT_OK);
        if (gin_connect_get_state(o->session) == GIN_CONNECT_READY &&
            o->received + (o->callback_code != GIN_HISTORY_OK) >= received) return;
    } while (now_ms() < end);
    assert(!"actual READY/client receive barrier deadline");
}
static enum gin_history_code drain(struct owner *o, struct gin_history_recovery *r)
{
    size_t i;
    enum gin_history_code code = GIN_HISTORY_OK;
    assert(!o->in_callback);
    for (i = 0; i < o->count; i++) {
        if (code == GIN_HISTORY_OK) code = gin_history_recovery_live(r, o->queue[i], now_ms());
        gin_history_page_free(o->queue[i]); o->queue[i] = NULL;
    }
    o->count = o->bytes = 0;
    return code;
}
static void report(struct owner *o, struct gin_history_recovery *r,
    enum gin_history_code code, int64_t checkpoint, size_t appended)
{
    struct gin_history_status s = gin_history_recovery_status(r);
    const struct gin_history_page *v = gin_history_recovery_view(r);
    struct gin_query_result q = gin_connect_query_result(o->session);
    size_t i;
    printf("{\"code\":%d,\"state\":%d,\"ready\":%d,\"epoch\":%llu,"
        "\"after\":%lld,\"unknown\":%d,\"pages\":%u,\"live\":%u,"
        "\"queued\":%zu,\"queue_bytes\":%zu,\"received\":%u,\"ignored\":%u,"
        "\"callback\":%d,\"overflow_count\":%u,\"overflow_bytes\":%u,"
        "\"checkpoint\":%lld,\"appended\":%zu,\"query\":%d,\"operation\":%llu,\"ids\":[",
        (int)code, (int)s.state, (int)gin_connect_get_state(o->session),
        (unsigned long long)gin_connect_last_result(o->session).socket_epoch,
        (long long)s.fetch_after, s.unknown_before, s.pages, s.live_rows,
        o->count, o->bytes, o->received, o->ignored, (int)o->callback_code,
        o->overflow_count, o->overflow_bytes, (long long)checkpoint, appended,
        (int)q.outcome, (unsigned long long)q.operation_id);
    for (i = 0; i < gin_history_page_count(v); i++)
        printf("%s%lld", i ? "," : "", (long long)gin_history_page_row(v, i)->id);
    puts("]}"); assert(fflush(stdout) == 0);
}
int main(int argc, char **argv)
{
    const char boot[] = "{\"networks\":[{\"id\":7,\"slug\":\"Net\",\"nick\":\"Self\"}],\"channels\":{\"Net\":[{\"name\":\"#Room\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
    const char queries[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"7\":[{\"network_id\":7,\"target_nick\":\"Bob\",\"opened_at\":\"x\"}]}}";
    struct gin_connect_settings settings;
    struct gin_connect_subscription plan = {"Net", "#Room"};
    struct gin_credential credential = {0};
    struct gin_topology *base = NULL, *topology = NULL;
    struct gin_history_recovery *recovery = NULL;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct owner o;
    struct stat initial, current, lock;
    char line[128], command[24];
    unsigned long long a, b;
    int fd;
    int64_t checkpoint = 0;
    size_t appended = 0;
    assert(argc == 5);
    memset(&o, 0, sizeof(o)); o.count_cap = QUEUE_MAX; o.byte_cap = QUEUE_BYTES;
    assert(strlen(argv[1]) < sizeof(credential.origin));
    strcpy(credential.origin, argv[1]); strcpy(credential.token, "aa~");
    strcpy(credential.subject_name, "Alice");
    strcpy(credential.subject_id, "12345678-1234-1234-1234-123456789abc");
    settings.http.origin = argv[1]; settings.http.ca_file = argv[2];
    settings.http.connect_ms = getenv("GIN_HISTORY_PREFIX") ? 10000 : 2000;
    settings.http.total_ms = getenv("GIN_HISTORY_PREFIX") ? 20000 : 4000;
    settings.policy.client_protocol = 63; settings.policy.required_server_protocol = 4;
    settings.join_ms = getenv("GIN_HISTORY_PREFIX") ? 10000 : 4000;
    o.scope.origin = argv[1]; o.scope.subject_id = credential.subject_id;
    o.scope.network_slug = "Net"; o.scope.target = !strcmp(argv[4], "query") ? "Bob" : "#Room";
    o.scope.chantypes = "#"; o.scope.own_nick = "Self"; o.scope.network_id = 7;
    o.scope.role = !strcmp(argv[4], "query") ? GIN_RENDER_QUERY : GIN_RENDER_CHANNEL;
    o.scope.encryption = GIN_RENDER_MODE_PLAIN;
    o.source = o.scope.role == GIN_RENDER_QUERY ? "OldSelf" : "#Room";
    plan.target = o.source;
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &base) == GIN_TOPOLOGY_OK);
    assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &topology) == GIN_TOPOLOGY_OK);
    assert(gin_root_acquire(argv[3], &root) == 0);
    assert(gin_root_init(&root, "Net", o.scope.target, "#") == 0);
    fd = gin_root_output_open(&root, "Net", o.scope.target, "#"); assert(fd >= 0);
    assert(fstat(fd, &initial) == 0);
    if (initial.st_size == 0) assert(write(fd, "existing\n", 9) == 9);
    assert(fstat(fd, &initial) == 0 && fstat(root.lockfd, &lock) == 0);
    assert(gin_connect_new(&settings, &credential, &plan, 1, frame, &o, &o.session) == GIN_CONNECT_OK);
    report(&o, recovery, GIN_HISTORY_OK, checkpoint, appended);
    while (fgets(line, sizeof(line), stdin)) {
        enum gin_history_code code = GIN_HISTORY_OK;
        a = b = 0; assert(sscanf(line, "%23s %llu %llu", command, &a, &b) >= 1);
        assert(!o.in_callback);
        if (!strcmp(command, "start")) {
            assert(gin_connect_start(o.session) == GIN_CONNECT_OK); pump(&o, 0);
            o.scope.socket_epoch = gin_connect_last_result(o.session).socket_epoch;
        } else if (!strcmp(command, "wait")) pump(&o, (unsigned)a);
        else if (!strcmp(command, "begin")) {
            gin_history_recovery_free(recovery); recovery = NULL;
            code = gin_history_recovery_new(&o.scope, (int64_t)a, 2, now_ms(), &recovery);
        } else if (!strcmp(command, "limits")) {
            assert(!o.count && a <= QUEUE_MAX && b <= QUEUE_BYTES);
            o.count_cap = (size_t)a; o.byte_cap = (size_t)b;
        } else if (!strcmp(command, "drain")) code = drain(&o, recovery);
        else if (!strcmp(command, "stale")) {
            struct gin_history_scope stale = o.scope;
            struct gin_history_recovery *other = NULL;
            assert(o.count); stale.socket_epoch++;
            assert(gin_history_recovery_new(&stale, 0, 2, now_ms(), &other) == GIN_HISTORY_OK);
            code = gin_history_recovery_live(other, o.queue[0], now_ms());
            assert(gin_history_recovery_status(other).rows == 0);
            gin_history_recovery_free(other);
        } else if (!strcmp(command, "fetch")) {
            struct gin_history_request request;
            struct gin_history_page *page = NULL;
            struct gin_history_fetch_result result;
            code = gin_history_recovery_next(recovery, now_ms(), &request);
            if (code == GIN_HISTORY_OK) {
                result = gin_history_fetch(&settings.http, &settings.policy, &credential,
                    topology, &o.scope, &request, &page);
                code = result.code;
                /* Synchronous HTTP can leave live data in the real socket. Drain
                 * client receipt AFTER fetch, BEFORE accepting a short REST page. */
                if (a) pump(&o, (unsigned)a);
                if (code == GIN_HISTORY_OK) code = drain(&o, recovery);
                if (code == GIN_HISTORY_OK) code = gin_history_recovery_page(recovery, page, now_ms());
                gin_history_page_free(page);
            }
        } else if (!strcmp(command, "append")) {
            const struct gin_history_page *view = gin_history_recovery_view(recovery);
            size_t i;
            assert(gin_history_recovery_status(recovery).state == GIN_HISTORY_VISIBLE_DRAINED);
            assert(!o.overflow_count && !o.overflow_bytes && o.callback_code == GIN_HISTORY_OK);
            assert(fstat(fd, &current) == 0 && current.st_dev == initial.st_dev && current.st_ino == initial.st_ino);
            assert(fstat(root.lockfd, &current) == 0 && current.st_dev == lock.st_dev && current.st_ino == lock.st_ino);
            for (i = 0; i < gin_history_page_count(view); i++) {
                struct gin_render_record *record = NULL;
                struct gin_phoenix_view bytes;
                size_t got = 0;
                const struct gin_history_row *row = gin_history_page_row(view, i);
                assert(row->id > checkpoint);
                assert(gin_history_render(view, i, &record) == GIN_RENDER_OK);
                bytes = gin_render_record_bytes(record);
                assert(gin_render_append(fd, record, &got) == GIN_RENDER_OK && got == bytes.length);
                appended += got; checkpoint = row->id;
                gin_render_record_free(record);
            }
        } else assert(!"unknown fixture command");
        {
            struct gin_query_result receipt = gin_connect_query_result(o.session);
            assert(receipt.outcome == GIN_QUERY_NONE && receipt.code == GIN_QUERY_OK);
            assert(receipt.operation_id == 0 && receipt.socket_epoch == 0);
            assert(!receipt.join_ref[0] && !receipt.request_ref[0]);
        }
        report(&o, recovery, code, checkpoint, appended);
    }
    while (o.count) gin_history_page_free(o.queue[--o.count]);
    gin_history_recovery_free(recovery); gin_connect_free(o.session);
    assert(close(fd) == 0); assert(gin_root_release(&root) == 0);
    gin_topology_free(topology); gin_topology_free(base);
    gin_credential_clear(&credential); gin_http_global_cleanup();
    return 0;
}
