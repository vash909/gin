/* Cold state-preservation qualification, not wire replay or WSS coverage. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static unsigned allocations;
static int allocation_fail;
static void *replan_calloc(size_t n, size_t z)
{ allocations++; return allocation_fail ? NULL : calloc(n, z); }
#define calloc replan_calloc
#include "../src/connect.c"
#undef calloc
/* The RED bridge exercises the old immutable-plan recipe, not a fake API.
 * Its failure is a recipe limitation, not a legacy constructor defect. */
static enum gin_connect_code replace_plan(struct gin_connect *s,
    const struct gin_connect_subscription *p, size_t n)
{
#ifdef REPLAN_LEGACY_RECIPE
    (void)p; (void)n; gin_connect_close(s); return GIN_CONNECT_OK;
#else
    return gin_connect_replan_closed(s, p, n);
#endif
}
static void unchanged(struct gin_connect *s,
    const struct gin_connect_subscription *p, size_t n, enum gin_connect_code code)
{
    struct gin_connect before;
    memcpy(&before, s, sizeof(before));
    assert(replace_plan(s, p, n) == code);
    assert(!memcmp(&before, s, sizeof(before)));
}
static void preserved(struct gin_connect *s,
    const struct gin_connect_subscription *p, size_t n)
{
    struct gin_connect before, after;
    memcpy(&before, s, sizeof(before));
    assert(replace_plan(s, p, n) == GIN_CONNECT_OK);
    memcpy(&after, s, sizeof(after));
    memcpy(after.topics, before.topics, sizeof(after.topics));
    after.count = before.count;
    assert(!memcmp(&before, &after, sizeof(before)));
}
int main(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,4}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    char net[] = "Net", room[] = "#ROOM";
    struct gin_connect_subscription plan[] = {{net, room}, {"Net", "#room"}, {"net", "Bob"}, {"Net", NULL}};
    struct gin_connect_subscription invalid[] = {{NULL, "#x"}};
    struct gin_connect_subscription many[64];
    char targets[63][16];
    struct gin_connect *s = NULL;
    struct gin_query_result receipt;
    struct gin_connect snapshot;
    const struct gin_connect_topic_status *borrowed;
    size_t i;
    unsigned a;
    alarm(15);
    assert(gin_connect_new(&settings, &c, NULL, 0, NULL, &settings, &s) == GIN_CONNECT_OK);
    assert(gin_connect_get_state(s) == GIN_CONNECT_IDLE);
    assert(gin_connect_topic_count(s) == 1);
    gin_connect_close(s);
    assert(gin_connect_get_state(s) == GIN_CONNECT_STOPPED);
    assert(replace_plan(s, plan, 4) == GIN_CONNECT_OK);
    if (gin_connect_topic_count(s) != 5) {
        fprintf(stderr, "semantic RED: old immutable-plan RECIPE still has %zu topic; complete replacement requires 5 (legacy constructor is correct)\n", gin_connect_topic_count(s));
        gin_connect_free(s); return 23;
    }
    assert(!strcmp(gin_connect_topic_at(s, 0)->topic, "grappa:user:Alice"));
    assert(!strcmp(gin_connect_topic_at(s, 1)->topic, "grappa:user:Alice/network:Net"));
    assert(!strcmp(gin_connect_topic_at(s, 2)->topic, "grappa:user:Alice/network:Net/channel:#room"));
    assert(!strcmp(gin_connect_topic_at(s, 3)->topic, "grappa:user:Alice/network:net"));
    assert(!strcmp(gin_connect_topic_at(s, 4)->topic, "grappa:user:Alice/network:net/channel:bob"));
    net[0] = 'Z'; room[1] = 'Z';
    assert(!strcmp(s->topics[1].network, "Net"));
    assert(!strcmp(s->topics[2].target, "#ROOM"));
    for (i = 0; i < s->count; i++) {
        assert(gin_connect_topic_at(s, i) == &s->topics[i].status);
        assert(s->topics[i].status.topic == s->topics[i].text);
        assert(!s->topics[i].status.join_ref);
        assert(s->topics[i].status.state == GIN_CONNECT_TOPIC_DESIRED);
    }
    borrowed = gin_connect_topic_at(s, 2);
    a = allocations;
    unchanged(s, NULL, 1, GIN_CONNECT_INVALID);
    /* Zero count never dereferences a non-NULL unused pointer. */
    unchanged(s, plan, 65, GIN_CONNECT_LIMIT);
    unchanged(s, NULL, 65, GIN_CONNECT_INVALID);
    assert(allocations == a);
    unchanged(s, invalid, 1, GIN_CONNECT_INVALID);
    allocation_fail = 1;
    unchanged(s, plan, 4, GIN_CONNECT_NOMEM);
    allocation_fail = 0;
    assert(borrowed == gin_connect_topic_at(s, 2));
    for (i = GIN_CONNECT_CONNECTING; i <= GIN_CONNECT_FAILED; i++) {
        s->state = (enum gin_connect_state)i;
        unchanged(s, NULL, 0, GIN_CONNECT_INVALID);
    }
    s->state = GIN_CONNECT_STOPPED;
#define BLOCK(field, value) do { memcpy(&snapshot, s, sizeof(snapshot)); s->field = (value); unchanged(s, NULL, 0, GIN_CONNECT_INVALID); memcpy(s, &snapshot, sizeof(snapshot)); } while (0)
    BLOCK(in_callback, 1); BLOCK(cleanup_bad, 1); BLOCK(quarantined, 1);
    BLOCK(curl, (CURL *)s); BLOCK(upgrade_multi, (CURLM *)s);
    BLOCK(headers, (struct curl_slist *)s); BLOCK(multi_added, 1); BLOCK(socket, 3);
    BLOCK(query.outcome, GIN_QUERY_PENDING);
    for (i = GIN_QUERY_NOT_SENT; i <= GIN_QUERY_UNCERTAIN; i++)
        BLOCK(query.outcome, (enum gin_query_outcome)i);
#undef BLOCK
    s->query.outcome = GIN_QUERY_PENDING; s->query.operation_id = 123;
    gin_connect_close(s);
    assert(s->query.outcome == GIN_QUERY_NOT_SENT);
    unchanged(s, NULL, 0, GIN_CONNECT_INVALID);
    assert(gin_connect_query_take(s, &receipt) == 1 && receipt.operation_id == 123);
    /* Synthetic markers prove byte preservation only, not live retry behavior. */
    s->result.socket_epoch = 81; memset(&s->refs, 7, sizeof(s->refs));
    s->configured = 1; s->started = 1; s->cancelled = 1; s->retries_used = 9;
    s->clock_bad = 1; s->reason = GIN_LIVENESS_REASON_EXPLICIT_CLOSE;
    s->retry_due.tv_sec = 444; s->deadline.tv_sec = 555; s->query_deadline.tv_sec = 666;
    s->query.operation_id = 999; s->query_written = 1;
    s->pending = 17; s->offset = 19; s->header_bytes = 21;
    s->service_budget_exhausted = 1; s->receive_drained = 1;
    preserved(s, NULL, 0);
    assert(gin_connect_topic_count(s) == 1);
    for (i = 0; i < 63; i++) {
        snprintf(targets[i], sizeof(targets[i]), "#c%zu", i);
        many[i].network = "Net"; many[i].target = targets[i];
    }
    preserved(s, many, 62); assert(gin_connect_topic_count(s) == 64);
    unchanged(s, many, 63, GIN_CONNECT_LIMIT);
    for (i = 0; i < 64; i++) { many[i].network = "Net"; many[i].target = "#x"; }
    for (i = 1; i <= 63; i++) {
        preserved(s, many, i); assert(gin_connect_topic_count(s) == 3);
    }
    preserved(s, many, 64); assert(gin_connect_topic_count(s) == 3);
    allocation_fail = 1; unchanged(s, many, 64, GIN_CONNECT_NOMEM); allocation_fail = 0;
    unchanged(s, invalid, 1, GIN_CONNECT_INVALID);
    s->state = GIN_CONNECT_IDLE; preserved(s, NULL, 0);
    preserved(s, many, 0); assert(gin_connect_topic_count(s) == 1);
    assert(replace_plan(NULL, NULL, 0) == GIN_CONNECT_INVALID);
    gin_connect_free(s);
    puts("cold replan: PASS (state preservation only; no wire/WSS qualification)");
    return 0;
}
