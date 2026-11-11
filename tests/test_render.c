#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_render.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Compile only this translation unit with syscall substitution. */
static unsigned calls, fault;
static ssize_t fixture_write(int fd, const void *p, size_t n)
{
    calls++;
    if (fault == 1 && calls == 1) { errno = EINTR; return -1; }
    if (fault == 2) return write(fd, p, n > 3 ? 3 : n);
    if (fault == 3) return 0;
    if ((fault == 4 || fault == 7) && calls > 1) { errno = fault == 4 ? ENOSPC : EIO; return -1; }
    if (fault == 4 || fault == 7) return write(fd, p, 2);
    if (fault == 5) { errno = EINTR; return -1; }
    if (fault == 6) return write(fd, p, 1);
    return write(fd, p, n);
}
static int fail_alloc;
static void *fixture_malloc(size_t n) { return fail_alloc ? NULL : malloc(n); }
#define malloc fixture_malloc
#define write fixture_write
#include "../src/render.c"
#undef write
#undef malloc

static struct gin_render_context ctx = {
    "Alice", "Net", "#Room", "#ROOM", "#", "3", 1,
    GIN_RENDER_CHANNEL, GIN_RENDER_MODE_PLAIN
};
static struct gin_render_record *render(const char *payload, enum gin_render_code expected)
{
    struct gin_phoenix_frame *f = NULL;
    struct gin_render_record *r = NULL;
    char wire[100000], topic[GIN_PHOENIX_TOPIC_MAX + 1u];
    size_t topic_length;
    int n;
    assert(gin_ws_topic(ctx.subject, ctx.source_target ? ctx.network : NULL, ctx.source_target, topic, sizeof(topic), &topic_length) == GIN_WS_VALUE_OK);
    n = snprintf(wire, sizeof(wire), "[null,null,\"%s\",\"event\",%s]", topic, payload);
    enum gin_render_code code;
    assert(n > 0 && (size_t)n < sizeof(wire));
    assert(gin_phoenix_decode((const unsigned char *)wire, (size_t)n, &f) == GIN_PHOENIX_OK);
    code = gin_render_event(f, &ctx, 1, -1, &r);
    if (code != expected) fprintf(stderr, "expected %d got %d: %s\n", expected, code, payload);
    assert(code == expected);
    gin_phoenix_frame_free(f);
    assert((r != NULL) == (expected == GIN_RENDER_OK));
    return r;
}
static void golden(const char *payload, const char *line)
{
    struct gin_render_record *r = render(payload, GIN_RENDER_OK);
    struct gin_phoenix_view b = gin_render_record_bytes(r);
    assert(b.length == strlen(line) && !memcmp(b.data, line, b.length));
    gin_render_record_free(r);
}
#define ROW "\"network\":\"Net\",\"channel\":\"#room\",\"sender\":\"N\\\"ick\",\"server_time\":0,"
int main(void)
{
    struct gin_render_record *r;
    struct gin_phoenix_view b;
    char path[4096];
    int fd;
    size_t n;
    unsigned mode;
    golden("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":\"caf\u00e9\\n\\u001b\\\"\\\\\"}}",
        "[1970-01-01T00:00:00.000Z] privmsg sender=\"N\\\"ick\" body=\"caf\u00e9\\x0A\\x1B\\\"\\\\\"\n");
    golden("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"notice\",\"body\":null}}",
        "[1970-01-01T00:00:00.000Z] notice sender=\"N\\\"ick\" body=\"\"\n");
    golden("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"action\",\"body\":\"\\u0001ACTION waves\"}}",
        "[1970-01-01T00:00:00.000Z] action sender=\"N\\\"ick\" body=\"waves\"\n");
    golden("{\"kind\":\"joined\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"joined\"}",
        "[1969-12-31T23:59:59.999Z recv] joined target=\"#Room\" state=\"joined\"\n");
    render("{\"kind\":\"future\",\"secret\":\"never dump\"}", GIN_RENDER_IGNORE);
    render("{\"kind\":\"e2e_future\"}", GIN_RENDER_UNSUPPORTED_ENCRYPTION);
    render("{\"kind\":\"message\",\"message\":{}}", GIN_RENDER_MALFORMED);
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"future\"}}", GIN_RENDER_IGNORE);
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\"}}", GIN_RENDER_MALFORMED);
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":\"\\u0001E2Ebad\"}}", GIN_RENDER_UNSUPPORTED_ENCRYPTION);
    ctx.encryption = GIN_RENDER_MODE_UNKNOWN;
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":\"hidden\"}}", GIN_RENDER_UNSUPPORTED_ENCRYPTION);
    ctx.encryption = GIN_RENDER_MODE_PLAIN;
    ctx.target = "Peer"; ctx.source_target = "OldNick"; ctx.role = GIN_RENDER_QUERY;
    golden("{\"kind\":\"message\",\"message\":{\"network\":\"Net\",\"channel\":\"oldnick\",\"sender\":\"N\\\"ick\",\"server_time\":0," "\"dm_with\":\"pEER\",\"kind\":\"privmsg\",\"body\":\"old own nick source\"}}",
        "[1970-01-01T00:00:00.000Z] privmsg sender=\"N\\\"ick\" body=\"old own nick source\"\n");
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":null}}", GIN_RENDER_ROUTE_MISMATCH);
    ctx.target = "#Room"; ctx.source_target = "#ROOM"; ctx.role = GIN_RENDER_CHANNEL;
    r = render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":\"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz\"}}", GIN_RENDER_OK);
    b = gin_render_record_bytes(r);
    assert(snprintf(path, sizeof(path), "%s/render-core-XXXXXX", getenv("TMPDIR") ? getenv("TMPDIR") : ".build") > 0);
    fd = mkstemp(path); assert(fd >= 0);
    assert(fcntl(fd, F_SETFL, O_APPEND) == 0);
    for (mode = 0; mode <= 7; mode++) {
        unsigned char actual[1000]; ssize_t got;
        assert(ftruncate(fd, 0) == 0);
        fault = mode; calls = 0; n = 999;
        if (mode <= 2) { assert(gin_render_append(fd, r, &n) == GIN_RENDER_OK); assert(n == b.length); }
        else { assert(gin_render_append(fd, r, &n) == GIN_RENDER_IO);
            if (mode == 3) assert(n == 0 && errno == EIO);
            if (mode == 4) assert(n == 2 && errno == ENOSPC);
            if (mode == 5) assert(n == 0 && calls == 64);
            if (mode == 6) assert(n == 64 && calls == 64);
            if (mode == 7) assert(n == 2 && errno == EIO);
        }
        got = pread(fd, actual, sizeof(actual), 0);
        assert(got >= 0 && (size_t)got == n && !memcmp(actual, b.data, n));
    }
    {
        char other[4100]; int ro, pipes[2];
        assert(snprintf(other, sizeof(other), "%s.lnk", path) > 0);
        assert(link(path, other) == 0); calls = 0;
        assert(gin_render_append(fd, r, &n) == GIN_RENDER_IO && n == 0 && calls == 0);
        assert(unlink(other) == 0);
        ro = open(path, O_RDONLY | O_APPEND); assert(ro >= 0);
        assert(gin_render_append(ro, r, &n) == GIN_RENDER_IO && n == 0 && calls == 0);
        assert(close(ro) == 0 && pipe(pipes) == 0);
        assert(gin_render_append(pipes[1], r, &n) == GIN_RENDER_IO && n == 0 && calls == 0);
        assert(close(pipes[0]) == 0 && close(pipes[1]) == 0);
    }
    calls = 0; assert(fchmod(fd, 0640) == 0);
    assert(gin_render_append(fd, r, &n) == GIN_RENDER_IO && n == 0 && calls == 0);
    assert(fchmod(fd, 0600) == 0 && fcntl(fd, F_SETFL, 0) == 0);
    assert(gin_render_append(fd, r, &n) == GIN_RENDER_IO && calls == 0);
    assert(close(fd) == 0 && unlink(path) == 0); gin_render_record_free(r);
    gin_render_record_free(NULL);
    b = gin_render_record_bytes(NULL); assert(!b.data && !b.length);
    {
        struct gin_phoenix_frame *f = NULL;
        struct gin_render_record *owned = NULL, *occupied;
        const char *wire = "[\"3\",null,\"grappa:user:Alice/network:Net/channel:#room\",\"event\",{\"kind\":\"joined\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"joined\"}]";
        assert(gin_phoenix_decode((const unsigned char *)wire, strlen(wire), &f) == GIN_PHOENIX_OK);
        assert(gin_render_event(f, &ctx, 2, 0, &owned) == GIN_RENDER_ROUTE_MISMATCH && !owned);
        ctx.join_ref = "stale";
        assert(gin_render_event(f, &ctx, 1, 0, &owned) == GIN_RENDER_ROUTE_MISMATCH && !owned);
        ctx.join_ref = "3";
        assert(gin_render_event(f, &ctx, 1, 0, &owned) == GIN_RENDER_OK);
        occupied = owned;
        assert(gin_render_event(f, &ctx, 1, 0, &owned) == GIN_RENDER_INVALID && owned == occupied);
        gin_phoenix_frame_free(f); gin_render_record_free(owned);
    }
    fail_alloc = 1;
    render("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"privmsg\",\"body\":null}}", GIN_RENDER_NOMEM);
    fail_alloc = 0;
    ctx.source_target = NULL;
    golden("{\"kind\":\"window_pending\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"pending\"}",
        "[1969-12-31T23:59:59.999Z recv] window_pending target=\"#Room\" state=\"pending\"\n");
    ctx.source_target = "#ROOM";
    golden("{\"kind\":\"join_failed\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"failed\",\"reason\":\"no\\nentry\",\"numeric\":473}",
        "[1969-12-31T23:59:59.999Z recv] join_failed target=\"#Room\" state=\"failed\" reason=\"no\\x0Aentry\" numeric=473\n");
    golden("{\"kind\":\"window_invite_declined\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":42}",
        "[1969-12-31T23:59:59.999Z recv] window_invite_declined target=\"#Room\"\n");
    render("{\"kind\":\"joined\",\"network\":\"Other\",\"channel\":\"#room\",\"state\":\"joined\"}", GIN_RENDER_ROUTE_MISMATCH);
    render("{\"kind\":\"joined\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"pending\"}", GIN_RENDER_MALFORMED);
    {
        char body[GIN_RENDER_BODY_MAX + 2u];
        json_t *row = json_object(), *event = json_object();
        char *dump;
        memset(body, 'a', sizeof(body)); body[sizeof(body)-1u] = 0;
        assert(json_object_set_new(row, "network", json_string("Net")) == 0);
        assert(json_object_set_new(row, "channel", json_string("#room")) == 0);
        assert(json_object_set_new(row, "kind", json_string("privmsg")) == 0);
        assert(json_object_set_new(row, "sender", json_string("Nick")) == 0);
        assert(json_object_set_new(row, "server_time", json_integer(0)) == 0);
        assert(json_object_set_new(row, "body", json_string(body)) == 0);
        assert(json_object_set_new(event, "kind", json_string("message")) == 0);
        assert(json_object_set(event, "message", row) == 0);
        dump = json_dumps(event, JSON_COMPACT); assert(dump);
        render(dump, GIN_RENDER_LIMIT); free(dump);
        body[GIN_RENDER_BODY_MAX] = 0;
        assert(json_object_set_new(row, "body", json_string(body)) == 0);
        dump = json_dumps(event, JSON_COMPACT); assert(dump);
        r = render(dump, GIN_RENDER_OK); gin_render_record_free(r); free(dump);
        assert(json_object_set_new(row, "server_time", json_integer(INT64_C(253402300800000))) == 0);
        dump = json_dumps(event, JSON_COMPACT); assert(dump);
        render(dump, GIN_RENDER_RANGE); free(dump);
        json_decref(row); json_decref(event);
    }
    golden("{\"kind\":\"kicked\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"kicked\",\"by\":\"Op\",\"reason\":\"bye\"}",
        "[1969-12-31T23:59:59.999Z recv] kicked target=\"#Room\" state=\"kicked\" by=\"Op\" reason=\"bye\"\n");
    golden("{\"kind\":\"window_invited\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"invited\",\"inviter\":\"Op\"}",
        "[1969-12-31T23:59:59.999Z recv] window_invited target=\"#Room\" state=\"invited\" inviter=\"Op\"\n");
    golden("{\"kind\":\"message\",\"message\":{" ROW "\"kind\":\"action\",\"body\":\"\\u0001ACTION waves\\u0001\"}}",
        "[1970-01-01T00:00:00.000Z] action sender=\"N\\\"ick\" body=\"waves\"\n");
    {
        char target[] = "#Room";
        const char *line = "[1969-12-31T23:59:59.999Z recv] joined target=\"#Room\" state=\"joined\"\n";
        ctx.target = target;
        r = render("{\"kind\":\"joined\",\"network\":\"Net\",\"channel\":\"#room\",\"state\":\"joined\"}", GIN_RENDER_OK);
        memset(target, 'X', sizeof(target)-1u); ctx.target = "#Room";
        b = gin_render_record_bytes(r);
        assert(b.length == strlen(line) && !memcmp(b.data, line, b.length));
        gin_render_record_free(r);
    }
    puts("render core passed"); return 0;
}
