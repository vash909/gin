#include "session_live_queue.h"
#include <stdlib.h>
#include <string.h>

struct entry {
    struct gin_live_view view;
    size_t alloc_size;
};

struct gin_live_queue {
    size_t rows;
    size_t bytes;
    size_t count;
    size_t bytes_used;
    size_t head;
    size_t tail;
    enum gin_live_code failure;
    struct entry *slots[GIN_LIVE_ROWS_MAX];
};

static size_t bounded_len(const char *s, size_t max)
{
    size_t i = 0;
    while (i <= max) {
        if (s[i] == '\0')
            return i;
        i++;
    }
    return max + 1;
}

static int safe_add(size_t a, size_t b, size_t *out)
{
    if (a > (size_t)-1 - b)
        return 0;
    *out = a + b;
    return 1;
}

static void wipe_mem(void *p, size_t n)
{
    volatile unsigned char *v = (volatile unsigned char *)p;
    while (n--) {
        *v++ = 0;
    }
}

enum gin_live_code gin_live_queue_new(size_t rows, size_t bytes,
    struct gin_live_queue **out)
{
    struct gin_live_queue *q;
    if (out == NULL || *out != NULL)
        return GIN_LIVE_INVALID;
    if (rows < 1u || rows > GIN_LIVE_ROWS_MAX)
        return GIN_LIVE_INVALID;
    if (bytes < 1u || bytes > GIN_LIVE_BYTES_MAX)
        return GIN_LIVE_INVALID;
    q = (struct gin_live_queue *)malloc(sizeof(*q));
    if (q == NULL)
        return GIN_LIVE_NOMEM;
    memset(q, 0, sizeof(*q));
    q->rows = rows;
    q->bytes = bytes;
    q->failure = GIN_LIVE_OK;
    *out = q;
    return GIN_LIVE_OK;
}

enum gin_live_code gin_live_queue_push(struct gin_live_queue *queue,
    const struct gin_live_view *view)
{
    size_t cost;
    size_t len;
    size_t add;
    struct entry *e;
    unsigned char *dst;
    const char *s;

    if (queue == NULL || view == NULL)
        return GIN_LIVE_INVALID;

    if (view->kind < GIN_LIVE_QUERY_SNAPSHOT || view->kind > GIN_LIVE_MALFORMED)
        return GIN_LIVE_INVALID;
    if (view->source_index >= GIN_LIVE_SOURCES_MAX)
        return GIN_LIVE_INVALID;
    if (view->observed_epoch == 0u)
        return GIN_LIVE_INVALID;

    if (view->source_join_ref == NULL)
        return GIN_LIVE_INVALID;
    len = bounded_len(view->source_join_ref, GIN_PHOENIX_REF_MAX);
    if (len < 1u || len > GIN_PHOENIX_REF_MAX)
        return GIN_LIVE_INVALID;

    if (view->fields.topic == NULL)
        return GIN_LIVE_INVALID;
    len = bounded_len(view->fields.topic, GIN_PHOENIX_TOPIC_MAX);
    if (len < 1u || len > GIN_PHOENIX_TOPIC_MAX)
        return GIN_LIVE_INVALID;

    if (view->fields.event == NULL)
        return GIN_LIVE_INVALID;
    len = bounded_len(view->fields.event, GIN_PHOENIX_EVENT_MAX);
    if (len < 1u || len > GIN_PHOENIX_EVENT_MAX)
        return GIN_LIVE_INVALID;

    if (view->fields.join_ref != NULL) {
        len = bounded_len(view->fields.join_ref, GIN_PHOENIX_REF_MAX);
        if (len > GIN_PHOENIX_REF_MAX)
            return GIN_LIVE_INVALID;
    }
    if (view->fields.ref != NULL) {
        len = bounded_len(view->fields.ref, GIN_PHOENIX_REF_MAX);
        if (len > GIN_PHOENIX_REF_MAX)
            return GIN_LIVE_INVALID;
    }

    if (view->fields.payload.data == NULL)
        return GIN_LIVE_INVALID;
    if (view->fields.payload.length < 1u || view->fields.payload.length > GIN_PHOENIX_BODY_MAX)
        return GIN_LIVE_INVALID;

    if (queue->failure != GIN_LIVE_OK)
        return queue->failure;

    if (queue->count >= queue->rows) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }

    cost = sizeof(struct entry);

    len = bounded_len(view->source_join_ref, GIN_PHOENIX_REF_MAX);
    add = len + 1u;
    if (!safe_add(cost, add, &cost)) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }
    len = bounded_len(view->fields.topic, GIN_PHOENIX_TOPIC_MAX);
    add = len + 1u;
    if (!safe_add(cost, add, &cost)) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }
    len = bounded_len(view->fields.event, GIN_PHOENIX_EVENT_MAX);
    add = len + 1u;
    if (!safe_add(cost, add, &cost)) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }
    if (view->fields.join_ref != NULL) {
        len = bounded_len(view->fields.join_ref, GIN_PHOENIX_REF_MAX);
        add = len + 1u;
        if (!safe_add(cost, add, &cost)) {
            queue->failure = GIN_LIVE_LIMIT;
            return GIN_LIVE_LIMIT;
        }
    }
    if (view->fields.ref != NULL) {
        len = bounded_len(view->fields.ref, GIN_PHOENIX_REF_MAX);
        add = len + 1u;
        if (!safe_add(cost, add, &cost)) {
            queue->failure = GIN_LIVE_LIMIT;
            return GIN_LIVE_LIMIT;
        }
    }
    if (!safe_add(cost, view->fields.payload.length, &cost)) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }

    if (cost > queue->bytes || queue->bytes_used > queue->bytes - cost) {
        queue->failure = GIN_LIVE_LIMIT;
        return GIN_LIVE_LIMIT;
    }

    e = (struct entry *)malloc(cost);
    if (e == NULL) {
        queue->failure = GIN_LIVE_NOMEM;
        return GIN_LIVE_NOMEM;
    }

    e->alloc_size = cost;
    e->view = *view;
    dst = (unsigned char *)(e + 1);

    s = view->source_join_ref;
    len = bounded_len(s, GIN_PHOENIX_REF_MAX);
    memcpy(dst, s, len + 1u);
    e->view.source_join_ref = (const char *)dst;
    dst += len + 1u;

    s = view->fields.topic;
    len = bounded_len(s, GIN_PHOENIX_TOPIC_MAX);
    memcpy(dst, s, len + 1u);
    e->view.fields.topic = (const char *)dst;
    dst += len + 1u;

    s = view->fields.event;
    len = bounded_len(s, GIN_PHOENIX_EVENT_MAX);
    memcpy(dst, s, len + 1u);
    e->view.fields.event = (const char *)dst;
    dst += len + 1u;

    if (view->fields.join_ref != NULL) {
        s = view->fields.join_ref;
        len = bounded_len(s, GIN_PHOENIX_REF_MAX);
        memcpy(dst, s, len + 1u);
        e->view.fields.join_ref = (const char *)dst;
        dst += len + 1u;
    } else {
        e->view.fields.join_ref = NULL;
    }

    if (view->fields.ref != NULL) {
        s = view->fields.ref;
        len = bounded_len(s, GIN_PHOENIX_REF_MAX);
        memcpy(dst, s, len + 1u);
        e->view.fields.ref = (const char *)dst;
        dst += len + 1u;
    } else {
        e->view.fields.ref = NULL;
    }

    if (view->fields.payload.length > 0u) {
        memcpy(dst, view->fields.payload.data, view->fields.payload.length);
        e->view.fields.payload.data = dst;
        dst += view->fields.payload.length;
    } else {
        e->view.fields.payload.data = NULL;
    }

    queue->slots[queue->tail] = e;
    queue->tail = (queue->tail + 1u) % GIN_LIVE_ROWS_MAX;
    queue->count++;
    queue->bytes_used += cost;
    return GIN_LIVE_OK;
}

const struct gin_live_view *gin_live_queue_peek(const struct gin_live_queue *queue)
{
    if (queue == NULL)
        return NULL;
    if (queue->count == 0u)
        return NULL;
    return &queue->slots[queue->head]->view;
}

enum gin_live_code gin_live_queue_drop(struct gin_live_queue *queue)
{
    struct entry *e;
    if (queue == NULL)
        return GIN_LIVE_INVALID;
    if (queue->count == 0u)
        return GIN_LIVE_EMPTY;
    e = queue->slots[queue->head];
    queue->slots[queue->head] = NULL;
    queue->head = (queue->head + 1u) % GIN_LIVE_ROWS_MAX;
    queue->count--;
    queue->bytes_used -= e->alloc_size;
    wipe_mem(e, e->alloc_size);
    free(e);
    return GIN_LIVE_OK;
}

size_t gin_live_queue_count(const struct gin_live_queue *queue)
{
    if (queue == NULL)
        return 0u;
    return queue->count;
}

size_t gin_live_queue_bytes(const struct gin_live_queue *queue)
{
    if (queue == NULL)
        return 0u;
    return queue->bytes_used;
}

enum gin_live_code gin_live_queue_failure(const struct gin_live_queue *queue)
{
    if (queue == NULL)
        return GIN_LIVE_INVALID;
    return queue->failure;
}

void gin_live_queue_free(struct gin_live_queue *queue)
{
    size_t i;
    size_t n;
    if (queue == NULL)
        return;
    n = queue->count;
    for (i = 0u; i < n; i++) {
        size_t idx = (queue->head + i) % GIN_LIVE_ROWS_MAX;
        struct entry *e = queue->slots[idx];
        if (e != NULL) {
            wipe_mem(e, e->alloc_size);
            free(e);
        }
    }
    wipe_mem(queue, sizeof(*queue));
    free(queue);
}
