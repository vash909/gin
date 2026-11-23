/* Private fault characterization against the frozen implementation. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "gin_phoenix.h"
static int injected_encode, injected_clock;
static unsigned encode_calls, clock_calls, staged_frees;
static unsigned char *staged_bytes;
static enum gin_phoenix_code probe_encode(const struct gin_phoenix_fields *f,
    struct gin_phoenix_bytes *out)
{
    enum gin_phoenix_code code;
    encode_calls++;
    if (injected_encode) return injected_encode == 1 ? GIN_PHOENIX_NOMEM : GIN_PHOENIX_LIMIT;
    code = gin_phoenix_encode(f, out);
    if (code == GIN_PHOENIX_OK) staged_bytes = out->data;
    return code;
}
static void probe_bytes_free(struct gin_phoenix_bytes *bytes)
{
    if (bytes && bytes->data && bytes->data == staged_bytes) staged_frees++;
    gin_phoenix_bytes_free(bytes);
}
static int probe_clock(clockid_t id, struct timespec *out)
{
    clock_calls++;
    if (injected_clock) { errno = EIO; return -1; }
    return clock_gettime(id, out);
}
#define gin_phoenix_encode probe_encode
#define gin_phoenix_bytes_free probe_bytes_free
#define clock_gettime probe_clock
#include "../src/connect.c"
#undef gin_phoenix_encode
#undef gin_phoenix_bytes_free
#undef clock_gettime
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAULT CHECK line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static unsigned char before[sizeof(struct gin_connect)];
static struct gin_connect *fresh(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4, 1}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    struct gin_connect_subscription plan[] = {{"Net", "Own"}};
    struct gin_connect *s = NULL;
    size_t i;
    injected_encode = injected_clock = 0;
    encode_calls = clock_calls = staged_frees = 0;
    staged_bytes = NULL;
    CHECK(gin_connect_new(&settings, &c, plan, 1, NULL, NULL, &s) == GIN_CONNECT_OK);
    s->state = GIN_CONNECT_READY; s->result.socket_epoch = 1;
    for (i = 0; i < s->count; i++) {
        (void)snprintf(s->topics[i].ref, sizeof(s->topics[i].ref), "%zu", i+1);
        s->topics[i].status.join_ref = s->topics[i].ref;
        s->topics[i].status.state = GIN_CONNECT_TOPIC_JOINED;
    }
    s->refs.current = 3;
    return s;
}
int main(void)
{
    struct gin_connect *s;
    size_t out;
    unsigned mode;
    for (mode = 1; mode <= 2; mode++) {
        s = fresh(); out = 999;
        memcpy(before, s, sizeof(*s)); injected_encode = (int)mode;
        CHECK(gin_connect_subscribe_query_live(s, "Net", "NewPeer", "#&", 1, &out) ==
            (mode == 1 ? GIN_CONNECT_NOMEM : GIN_CONNECT_LIMIT));
        CHECK(out == 999 && !memcmp(before, s, sizeof(*s)));
        CHECK(encode_calls == 1 && clock_calls == 0 && !staged_frees && !staged_bytes);
        injected_encode = 0; gin_connect_free(s);
        puts(mode == 1 ? "PASS encoder-NOMEM atomic" : "PASS encoder-LIMIT atomic");
    }
    s = fresh(); out = 999; injected_clock = 1;
    CHECK(gin_connect_subscribe_query_live(s, "Net", "NewPeer", "#&", 1, &out) == GIN_CONNECT_CLOCK);
    CHECK(out == 999 && s->count == 3 && s->refs.current == 3);
    CHECK(s->state == GIN_CONNECT_FAILED && s->result.code == GIN_CONNECT_CLOCK);
    CHECK(encode_calls == 1 && clock_calls == 1 && staged_frees == 1 && !s->outbound.data);
    injected_clock = 0; gin_connect_free(s);
    puts("PASS post-encode-clock fail-closed staging-freed");
    s = fresh(); out = 999; s->configured = 1; s->heartbeat = GIN_HEARTBEAT_SCHEDULED; injected_clock = 1;
    CHECK(gin_connect_subscribe_query_live(s, "Net", "NewPeer", "#&", 1, &out) == GIN_CONNECT_CLOCK);
    CHECK(out == 999 && s->count == 3 && s->refs.current == 3);
    CHECK(s->state == GIN_CONNECT_FAILED && s->result.code == GIN_CONNECT_CLOCK);
    CHECK(!encode_calls && clock_calls == 1 && !staged_frees && !staged_bytes);
    CHECK(!s->transient && s->reason == GIN_LIVENESS_REASON_CLOCK);
    injected_clock = 0; gin_connect_free(s);
    puts("PASS heartbeat-clock fail-closed no-encoding");
    return 0;
}
