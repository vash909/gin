#ifdef NDEBUG
#undef NDEBUG
#endif
#include "session_live_queue.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static struct gin_live_view sample(void)
{
    struct gin_live_view v;
    memset(&v, 0, sizeof(v));
    v.fields.join_ref = "";
    v.fields.ref = NULL;
    v.fields.topic = "topic";
    v.fields.event = "event";
    v.fields.payload.data = (const unsigned char *)"{}";
    v.fields.payload.length = 2;
    v.source_join_ref = "1";
    v.observed_epoch = UINT64_C(9);
    v.receive_ms = -1;
    v.source_index = 2;
    v.kind = GIN_LIVE_MESSAGE;
    return v;
}
int main(void)
{
    struct gin_live_queue *q = NULL, *occupied;
    struct gin_live_view v = sample();
    const struct gin_live_view *head;
    size_t cost;
    assert(gin_live_queue_count(NULL) == 0);
    assert(gin_live_queue_bytes(NULL) == 0);
    assert(gin_live_queue_peek(NULL) == NULL);
    assert(gin_live_queue_failure(NULL) == GIN_LIVE_INVALID);
    gin_live_queue_free(NULL);
    assert(gin_live_queue_new(0, 10, &q) == GIN_LIVE_INVALID && q == NULL);
    assert(gin_live_queue_new(65, 10, &q) == GIN_LIVE_INVALID && q == NULL);
    assert(gin_live_queue_new(2, 0, &q) == GIN_LIVE_INVALID && q == NULL);
    assert(gin_live_queue_new(2, GIN_LIVE_BYTES_MAX + 1u, &q) == GIN_LIVE_INVALID && q == NULL);
    assert(gin_live_queue_new(2, GIN_LIVE_BYTES_MAX, &q) == GIN_LIVE_OK);
    occupied = q;
    assert(gin_live_queue_new(2, 100, &q) == GIN_LIVE_INVALID && q == occupied);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_OK);
    cost = gin_live_queue_bytes(q);
    assert(cost >= sizeof(struct gin_live_view) + 6u + 6u + 1u + 2u + 2u);
    head = gin_live_queue_peek(q);
    assert(head && head->fields.join_ref && !*head->fields.join_ref && head->fields.ref == NULL);
    assert(head->observed_epoch == 9 && head->receive_ms == -1 && head->source_index == 2);
    assert(head->kind == GIN_LIVE_MESSAGE && !strcmp(head->source_join_ref, "1"));
    assert(head->fields.payload.length == 2 && !memcmp(head->fields.payload.data, "{}", 2));
    v.fields.join_ref = NULL; v.fields.ref = ""; v.observed_epoch = 10;
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_OK && gin_live_queue_count(q) == 2);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT);
    assert(gin_live_queue_failure(q) == GIN_LIVE_LIMIT && gin_live_queue_count(q) == 2);
    assert(gin_live_queue_drop(q) == GIN_LIVE_OK);
    head = gin_live_queue_peek(q);
    assert(head && head->fields.join_ref == NULL && head->fields.ref && !*head->fields.ref);
    assert(head->observed_epoch == 10);
    assert(gin_live_queue_drop(q) == GIN_LIVE_OK);
    assert(gin_live_queue_count(q) == 0 && gin_live_queue_bytes(q) == 0);
    assert(gin_live_queue_failure(q) == GIN_LIVE_LIMIT);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT);
    assert(gin_live_queue_drop(q) == GIN_LIVE_EMPTY);
    gin_live_queue_free(q); q = NULL;

    v = sample();
    assert(gin_live_queue_new(2, cost, &q) == GIN_LIVE_OK);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_OK && gin_live_queue_bytes(q) == cost);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && gin_live_queue_count(q) == 1);
    gin_live_queue_free(q); q = NULL;
    assert(gin_live_queue_new(2, cost - 1u, &q) == GIN_LIVE_OK);
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_LIMIT && gin_live_queue_count(q) == 0);
    gin_live_queue_free(q); q = NULL;

    assert(gin_live_queue_new(2, GIN_LIVE_BYTES_MAX, &q) == GIN_LIVE_OK);
    {
        char topic[] = "topic", event[] = "event", ref[] = "", source[] = "1";
        unsigned char payload[] = {'{', '}'};
        v = sample(); v.fields.topic = topic; v.fields.event = event;
        v.fields.join_ref = ref; v.fields.payload.data = payload; v.source_join_ref = source;
        assert(gin_live_queue_push(q, &v) == GIN_LIVE_OK);
        memset(topic, 'x', 5); memset(event, 'x', 5); source[0] = 'x'; payload[0] = 'x';
        head = gin_live_queue_peek(q);
        assert(!strcmp(head->fields.topic, "topic") && !strcmp(head->fields.event, "event"));
        assert(!strcmp(head->source_join_ref, "1") && !memcmp(head->fields.payload.data, "{}", 2));
        assert(gin_live_queue_drop(q) == GIN_LIVE_OK);
    }
    v = sample(); v.observed_epoch = 0;
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_INVALID);
    assert(gin_live_queue_failure(q) == GIN_LIVE_OK && gin_live_queue_count(q) == 0);
    v = sample(); v.source_index = 4;
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_INVALID);
    v = sample(); v.kind = GIN_LIVE_NONE;
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_INVALID);
    v = sample(); v.source_join_ref = "";
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_INVALID);
    v = sample(); v.fields.payload.data = NULL;
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_INVALID);
    v = sample();
    assert(gin_live_queue_push(q, &v) == GIN_LIVE_OK);
    gin_live_queue_free(q);
    puts("Live queue ownership/count/byte/latched-error tracer passed.");
    return 0;
}
