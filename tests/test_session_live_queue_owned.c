/* Private allocation oracle: compile this translation unit ALONE.
 * JSON/UTF-8 are caller preconditions. Raw boundary cases below exercise only
 * this private copying API, not decoder/wire admission or owner subcaps. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "session_live_queue.h"

static size_t checks, allocations, attempts, frees_seen, wiped_bytes;
static size_t fail_at, last_request, oom_cases, invalid_cases, pushes;
static size_t constructor_request, ring_cycles;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)
struct allocation { void *p; size_t n; };
static struct allocation live[128];
static void *owned_malloc(size_t n)
{
    size_t i;
    void *p;
    attempts++;
    last_request = n;
    if (fail_at != 0u && attempts == fail_at) return NULL;
    p = malloc(n);
    CHECK(p != NULL);
    memset(p, 0xa5, n);
    for (i = 0; i < 128u && live[i].p != NULL; i++) { }
    CHECK(i < 128u);
    live[i].p = p; live[i].n = n; allocations++;
    return p;
}
static void owned_free(void *p)
{
    size_t i, j;
    if (p == NULL) return;
    for (i = 0; i < 128u && live[i].p != p; i++) { }
    CHECK(i < 128u);
    /* Inspect the full requested allocation BEFORE real free, including every
     * entry-header byte/padding, every string terminator, and exact payload. */
    for (j = 0; j < live[i].n; j++) CHECK(((unsigned char *)p)[j] == 0u);
    wiped_bytes += live[i].n; frees_seen++;
    live[i].p = NULL; live[i].n = 0;
    free(p);
}
#define malloc owned_malloc
#define free owned_free
#include "../src/session_live_queue.c"
#undef malloc
#undef free

static size_t actual_bytes(const struct gin_live_queue *q)
{
    size_t i, total = 0;
    for (i = 0; i < 128u; i++)
        if (live[i].p != NULL && live[i].p != (const void *)q) total += live[i].n;
    return total;
}
static size_t actual_count(const struct gin_live_queue *q)
{
    size_t i, n = 0;
    for (i = 0; i < 128u; i++)
        if (live[i].p != NULL && live[i].p != (const void *)q) n++;
    return n;
}
static void accounting(const struct gin_live_queue *q)
{
    CHECK(gin_live_queue_bytes(q) == actual_bytes(q));
    CHECK(gin_live_queue_count(q) == actual_count(q));
}
static struct gin_live_view sample(void)
{
    struct gin_live_view v;
    memset(&v, 0, sizeof(v));
    v.source_join_ref = "1"; v.fields.topic = "t"; v.fields.event = "e";
    v.fields.payload.data = (const unsigned char *)"{}";
    v.fields.payload.length = 2; v.kind = GIN_LIVE_MESSAGE;
    v.observed_epoch = UINT64_MAX; v.receive_ms = INT64_MIN;
    return v;
}
static struct gin_live_queue *create(size_t rows, size_t bytes)
{
    struct gin_live_queue *q = NULL;
    size_t before = attempts;
    CHECK(gin_live_queue_new(rows, bytes, &q) == GIN_LIVE_OK);
    CHECK(attempts == before + 1u && q != NULL);
    if (constructor_request == 0u) constructor_request = last_request;
    CHECK(last_request == constructor_request);
    accounting(q); return q;
}
static void destroy(struct gin_live_queue *q)
{
    gin_live_queue_free(q);
    CHECK(actual_count(NULL) == 0u && actual_bytes(NULL) == 0u);
}
static void push_ok(struct gin_live_queue *q, const struct gin_live_view *v)
{
    size_t before = attempts, n = gin_live_queue_count(q);
    CHECK(gin_live_queue_push(q, v) == GIN_LIVE_OK);
    CHECK(attempts == before + 1u);
    CHECK(gin_live_queue_count(q) == n + 1u);
    pushes++; accounting(q);
}
static void invalid(struct gin_live_queue *q, const struct gin_live_view *v)
{
    size_t a = attempts, n = gin_live_queue_count(q), b = gin_live_queue_bytes(q);
    enum gin_live_code failure = gin_live_queue_failure(q);
    const struct gin_live_view *head = gin_live_queue_peek(q);
    CHECK(gin_live_queue_push(q, v) == GIN_LIVE_INVALID);
    CHECK(attempts == a && gin_live_queue_count(q) == n);
    CHECK(gin_live_queue_bytes(q) == b && gin_live_queue_failure(q) == failure);
    CHECK(gin_live_queue_peek(q) == head); accounting(q); invalid_cases++;
}
static void invalid_shapes(struct gin_live_queue *q)
{
    struct gin_live_view v;
    char ref[GIN_PHOENIX_REF_MAX + 2u], topic[GIN_PHOENIX_TOPIC_MAX + 2u];
    char event[GIN_PHOENIX_EVENT_MAX + 2u];
    memset(ref, 'r', sizeof(ref)); ref[sizeof(ref)-1u] = 0;
    memset(topic, 't', sizeof(topic)); topic[sizeof(topic)-1u] = 0;
    memset(event, 'e', sizeof(event)); event[sizeof(event)-1u] = 0;
    invalid(q, NULL);
#define BAD(field, value) do { v = sample(); v.field = (value); invalid(q, &v); } while (0)
    BAD(kind, GIN_LIVE_NONE); BAD(kind, (enum gin_live_kind)-1);
    BAD(kind, (enum gin_live_kind)7); BAD(source_index, 4u);
    BAD(source_index, (size_t)-1); BAD(observed_epoch, 0u);
    BAD(source_join_ref, NULL); BAD(source_join_ref, ""); BAD(source_join_ref, ref);
    BAD(fields.topic, NULL); BAD(fields.topic, ""); BAD(fields.topic, topic);
    BAD(fields.event, NULL); BAD(fields.event, ""); BAD(fields.event, event);
    BAD(fields.join_ref, ref); BAD(fields.ref, ref);
    BAD(fields.payload.data, NULL); BAD(fields.payload.length, 0u);
    BAD(fields.payload.length, GIN_PHOENIX_BODY_MAX + 1u);
    BAD(fields.payload.length, (size_t)-1);
#undef BAD
}
static void constructor_and_null(void)
{
    struct gin_live_queue *q = NULL, *old;
    struct gin_live_view v = sample();
    size_t a = attempts;
    CHECK(gin_live_queue_new(1, 1, NULL) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_new(0, 1, &q) == GIN_LIVE_INVALID && q == NULL);
    CHECK(gin_live_queue_new(65, 1, &q) == GIN_LIVE_INVALID && q == NULL);
    CHECK(gin_live_queue_new((size_t)-1, 1, &q) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_new(1, 0, &q) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_new(1, GIN_LIVE_BYTES_MAX + 1u, &q) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_new(1, (size_t)-1, &q) == GIN_LIVE_INVALID);
    CHECK(attempts == a);
    CHECK(gin_live_queue_push(NULL, &v) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_push(NULL, NULL) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_drop(NULL) == GIN_LIVE_INVALID);
    CHECK(gin_live_queue_count(NULL) == 0 && gin_live_queue_bytes(NULL) == 0);
    CHECK(gin_live_queue_peek(NULL) == NULL);
    CHECK(gin_live_queue_failure(NULL) == GIN_LIVE_INVALID);
    gin_live_queue_free(NULL);
    q = create(1, 1); old = q; a = attempts;
    CHECK(gin_live_queue_new(64, GIN_LIVE_BYTES_MAX, &q) == GIN_LIVE_INVALID);
    CHECK(q == old && attempts == a); destroy(q);
}
static void oom_exhaustion(void)
{
    size_t f, i;
    for (f = 1; f <= 65u; f++) {
        struct gin_live_queue *q = NULL;
        struct gin_live_view v = sample();
        size_t a = attempts;
        fail_at = a + f;
        if (f == 1u) {
            CHECK(gin_live_queue_new(64, GIN_LIVE_BYTES_MAX, &q) == GIN_LIVE_NOMEM);
            CHECK(q == NULL && attempts == a + 1u);
        } else {
            q = create(64, GIN_LIVE_BYTES_MAX);
            for (i = 0; i < f - 2u; i++) {
                v.observed_epoch = i + 1u; push_ok(q, &v);
            }
            CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_NOMEM);
            CHECK(attempts == a + f);
            CHECK(gin_live_queue_count(q) == f - 2u); accounting(q);
            invalid_shapes(q);
            a = attempts;
            CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_NOMEM && attempts == a);
            for (i = 0; i < f - 2u; i++) {
                CHECK(gin_live_queue_peek(q)->observed_epoch == i + 1u);
                CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK); accounting(q);
            }
            CHECK(gin_live_queue_failure(q) == GIN_LIVE_NOMEM);
            CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_NOMEM && attempts == a);
            CHECK(gin_live_queue_drop(q) == GIN_LIVE_EMPTY); destroy(q);
        }
        fail_at = 0; oom_cases++;
    }
}
static size_t measured_overhead(void)
{
    struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
    struct gin_live_view v = sample();
    size_t cost;
    push_ok(q, &v); cost = last_request;
    CHECK(cost > v.fields.payload.length);
    CHECK(gin_live_queue_bytes(q) == cost);
    destroy(q);
    /* Derived ONLY from observed malloc request, never sizeof opaque entry. */
    return cost - v.fields.payload.length;
}
static void limits(size_t overhead)
{
    struct gin_live_view v = sample();
    struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
    size_t i, a;
    for (i = 0; i < 64u; i++) push_ok(q, &v);
    CHECK(actual_bytes(q) < GIN_LIVE_BYTES_MAX);
    a = attempts;
    CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
    CHECK(gin_live_queue_count(q) == 64); invalid_shapes(q); accounting(q);
    while (gin_live_queue_count(q)) CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK);
    CHECK(gin_live_queue_failure(q) == GIN_LIVE_LIMIT);
    CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
    destroy(q);
    q = create(64, overhead + 2u); push_ok(q, &v); a = attempts;
    CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
    CHECK(gin_live_queue_count(q) == 1u);
    CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK); accounting(q);
    CHECK(gin_live_queue_failure(q) == GIN_LIVE_LIMIT);
    CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
    destroy(q);
    q = create(64, overhead + 1u); a = attempts;
    CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
    CHECK(gin_live_queue_count(q) == 0); destroy(q);
}
static unsigned char *json_payload(size_t n)
{
    unsigned char *p = malloc(n);
    CHECK(p != NULL && n >= 8u);
    memset(p, 'a', n); memcpy(p, "{\"p\":\"", 6u); memcpy(p + n - 2u, "\"}", 2u);
    return p;
}
static void byte_ceiling(size_t overhead)
{
    size_t first = (GIN_LIVE_BYTES_MAX - 2u * overhead) / 2u;
    size_t second = GIN_LIVE_BYTES_MAX - 2u * overhead - first;
    size_t delta, a;
    /* Compact valid JSON objects; conservative 64-byte Phoenix envelope budget
     * for these one-character topic/event, null refs. Not a decoder test. */
    CHECK(first + 64u <= GIN_PHOENIX_BODY_MAX);
    CHECK(second + 1u + 64u <= GIN_PHOENIX_BODY_MAX);
    for (delta = 0; delta <= 1u; delta++) {
        struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
        struct gin_live_view v = sample();
        unsigned char *p = json_payload(first), *r = json_payload(second + delta);
        v.fields.payload.data = p; v.fields.payload.length = first; push_ok(q, &v);
        CHECK(last_request == overhead + first);
        v.fields.payload.data = r; v.fields.payload.length = second + delta;
        a = attempts;
        if (delta == 0) {
            push_ok(q, &v); CHECK(last_request == overhead + second);
            CHECK(actual_bytes(q) == GIN_LIVE_BYTES_MAX);
            CHECK(gin_live_queue_count(q) == 2u);
            v = sample(); a = attempts;
            CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
        } else {
            CHECK(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && attempts == a);
            CHECK(gin_live_queue_count(q) == 1u);
        }
        accounting(q); free(p); free(r); destroy(q);
    }
}
static void boundaries(void)
{
    struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
    struct gin_live_view v = sample();
    char refs[GIN_PHOENIX_REF_MAX + 1u], topic[GIN_PHOENIX_TOPIC_MAX + 1u];
    char event[GIN_PHOENIX_EVENT_MAX + 1u];
    unsigned char *p = malloc(GIN_PHOENIX_BODY_MAX);
    CHECK(p != NULL);
    memset(refs, 'r', sizeof(refs)-1u); refs[sizeof(refs)-1u] = 0;
    memset(topic, 't', sizeof(topic)-1u); topic[sizeof(topic)-1u] = 0;
    memset(event, 'e', sizeof(event)-1u); event[sizeof(event)-1u] = 0;
    v.source_join_ref = refs; v.fields.join_ref = refs; v.fields.ref = refs;
    v.fields.topic = topic; v.fields.event = event;
    push_ok(q, &v);
    invalid_shapes(q);
    CHECK(!strcmp(gin_live_queue_peek(q)->source_join_ref, refs));
    CHECK(!strcmp(gin_live_queue_peek(q)->fields.topic, topic));
    CHECK(!strcmp(gin_live_queue_peek(q)->fields.event, event));
    CHECK(!strcmp(gin_live_queue_peek(q)->fields.join_ref, refs));
    CHECK(!strcmp(gin_live_queue_peek(q)->fields.ref, refs));
    CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK);
    invalid_shapes(q);
    /* Private raw-copy BODY_MAX and length-one cases intentionally need not
     * be valid JSON or reachable complete Phoenix wire messages. */
    memset(p, 0xff, GIN_PHOENIX_BODY_MAX); p[17] = 0;
    v = sample(); v.fields.payload.data = p; v.fields.payload.length = GIN_PHOENIX_BODY_MAX;
    push_ok(q, &v);
    CHECK(!memcmp(gin_live_queue_peek(q)->fields.payload.data, p, GIN_PHOENIX_BODY_MAX));
    CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK);
    v.fields.payload.length = 1; push_ok(q, &v);
    CHECK(gin_live_queue_peek(q)->fields.payload.data[0] == 0xffu);
    free(p); destroy(q);
}
static void deep_copy(void)
{
    size_t j;
    for (j = 0; j < 4u; j++) {
        struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
        struct gin_live_view v = sample();
        const struct gin_live_view *h;
        char *s = malloc(32u);
        unsigned char *p = malloc(3u);
        CHECK(s != NULL && p != NULL);
        memcpy(s, "source\0topic\0event\0join\0ref\0", 28u);
        memcpy(p, "{}", 3u);
        v.source_join_ref = s; v.fields.topic = s + 7; v.fields.event = s + 13;
        v.fields.join_ref = (j & 1u) ? NULL : s + 19;
        v.fields.ref = (j & 2u) ? "" : s + 24;
        v.fields.payload.data = p; v.source_index = j;
        push_ok(q, &v); h = gin_live_queue_peek(q);
        CHECK(h->source_join_ref != s && h->fields.topic != s + 7);
        CHECK(h->fields.event != s + 13 && h->fields.payload.data != p);
        if (v.fields.join_ref) CHECK(h->fields.join_ref != v.fields.join_ref);
        CHECK(h->fields.ref != v.fields.ref);
        memset(s, 'z', 32u); memset(p, 'x', 3u); free(s); free(p);
        memset(&v, 0, sizeof(v));
        CHECK(!strcmp(h->source_join_ref, "source") && !strcmp(h->fields.topic, "topic"));
        CHECK(!strcmp(h->fields.event, "event"));
        CHECK((j & 1u) ? h->fields.join_ref == NULL : !strcmp(h->fields.join_ref, "join"));
        CHECK(!strcmp(h->fields.ref, (j & 2u) ? "" : "ref"));
        CHECK(h->fields.payload.length == 2u && !memcmp(h->fields.payload.data, "{}", 2u));
        CHECK(h->source_index == j && h->observed_epoch == UINT64_MAX);
        CHECK(h->receive_ms == INT64_MIN && h->kind == GIN_LIVE_MESSAGE);
        destroy(q);
    }
}
static void fifo_wrap(void)
{
    struct gin_live_queue *q = create(64, GIN_LIVE_BYTES_MAX);
    size_t round, i;
    for (round = 0; round < 130u; round++) {
        for (i = 0; i < 64u; i++) {
            struct gin_live_view v = sample();
            uint64_t id = (uint64_t)(round * 64u + i + 1u);
            v.observed_epoch = id; v.receive_ms = (int64_t)id;
            v.source_index = i % 4u; v.kind = (enum gin_live_kind)(1u + i % 6u);
            v.fields.join_ref = (i % 2u) ? NULL : "";
            v.fields.ref = (i % 2u) ? "" : NULL;
            push_ok(q, &v);
        }
        for (i = 0; i < 64u; i++) {
            const struct gin_live_view *h = gin_live_queue_peek(q);
            uint64_t id = (uint64_t)(round * 64u + i + 1u);
            CHECK(h != NULL && h == gin_live_queue_peek(q));
            CHECK(h->observed_epoch == id && h->receive_ms == (int64_t)id);
            CHECK(h->source_index == i % 4u && h->kind == (enum gin_live_kind)(1u + i % 6u));
            CHECK((i % 2u) ? h->fields.join_ref == NULL : h->fields.join_ref != NULL && !*h->fields.join_ref);
            CHECK((i % 2u) ? h->fields.ref != NULL && !*h->fields.ref : h->fields.ref == NULL);
            CHECK(gin_live_queue_drop(q) == GIN_LIVE_OK); accounting(q);
        }
        CHECK(gin_live_queue_peek(q) == NULL && gin_live_queue_drop(q) == GIN_LIVE_EMPTY);
        CHECK(gin_live_queue_failure(q) == GIN_LIVE_OK);
        ring_cycles++;
    }
    /* free nonempty queue after >64 complete ring cycles. */
    { struct gin_live_view v = sample(); for (i = 0; i < 17u; i++) push_ok(q, &v); }
    destroy(q);
}
int main(void)
{
    size_t overhead;
    constructor_and_null(); oom_exhaustion(); overhead = measured_overhead();
    limits(overhead); byte_ceiling(overhead); boundaries(); deep_copy(); fifo_wrap();
    CHECK(allocations == frees_seen && actual_count(NULL) == 0u);
    printf("PASS checks=%zu allocations=%zu frees=%zu wiped_bytes=%zu attempts=%zu "
        "oom_cases=%zu invalid_cases=%zu successful_pushes=%zu observed_entry_overhead=%zu ring_cycles=%zu constructor_request=%zu\n",
        checks, allocations, frees_seen, wiped_bytes, attempts, oom_cases, invalid_cases, pushes, overhead, ring_cycles, constructor_request);
    return 0;
}
