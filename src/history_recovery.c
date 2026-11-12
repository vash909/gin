#define _POSIX_C_SOURCE 200809L
#include <stdlib.h>
#include "history_values.h"

struct gin_history_recovery {
    struct gin_history_page *view;
    struct gin_history_status status;
    unsigned page_limit;
    uint64_t last_ms;
    uint64_t deadline;
    int64_t live_ids[GIN_HISTORY_LIVE_ROWS_MAX];
};

enum gin_history_code gin_history_recovery_new(const struct gin_history_scope *scope,
    int64_t anchor, unsigned page_limit, uint64_t now_ms,
    struct gin_history_recovery **out)
{
    struct gin_history_recovery *r;
    enum gin_history_code code;
    if (!out || *out || anchor < 0 || !page_limit ||
        page_limit > GIN_HISTORY_PAGE_ROWS_MAX ||
        now_ms > UINT64_MAX - GIN_HISTORY_ATTEMPT_MS_MAX)
        return GIN_HISTORY_INVALID;
    code = gin_history_scope_check(scope);
    if (code != GIN_HISTORY_OK) return code;
    r = calloc(1, sizeof(*r));
    if (!r) return GIN_HISTORY_NOMEM;
    code = gin_history_copy_rows(scope, NULL, 0, GIN_HISTORY_MERGED, NULL, &r->view);
    if (code != GIN_HISTORY_OK) { free(r); return code; }
    r->status.state = GIN_HISTORY_RECOVERING;
    r->status.fetch_after = anchor;
    r->status.unknown_before = anchor == 0;
    r->status.owned_bytes = gin_history_page_owned_bytes(r->view);
    r->page_limit = page_limit;
    r->last_ms = now_ms;
    r->deadline = now_ms + GIN_HISTORY_ATTEMPT_MS_MAX;
    *out = r;
    return GIN_HISTORY_OK;
}
void gin_history_recovery_free(struct gin_history_recovery *r)
{
    if (!r) return;
    gin_history_page_free(r->view);
    free(r);
}
struct gin_history_status gin_history_recovery_status(const struct gin_history_recovery *r)
{
    struct gin_history_status empty = { GIN_HISTORY_RECOVERING, 0, 0, 0, 0, 0, 0 };
    return r ? r->status : empty;
}
static enum gin_history_code available(struct gin_history_recovery *r, uint64_t now)
{
    if (!r || now < r->last_ms) return GIN_HISTORY_INVALID;
    if (r->status.state == GIN_HISTORY_INCOMPLETE_DEADLINE) return GIN_HISTORY_DEADLINE;
    if (r->status.state == GIN_HISTORY_INCOMPLETE_LIMIT) return GIN_HISTORY_LIMIT;
    if (r->status.state == GIN_HISTORY_VISIBLE_DRAINED) return GIN_HISTORY_INVALID;
    if (now >= r->deadline) {
        r->status.state = GIN_HISTORY_INCOMPLETE_DEADLINE;
        r->last_ms = now;
        return GIN_HISTORY_DEADLINE;
    }
    return GIN_HISTORY_OK;
}
enum gin_history_code gin_history_recovery_next(struct gin_history_recovery *r,
    uint64_t now_ms, struct gin_history_request *out)
{
    enum gin_history_code code;
    if (!out) return GIN_HISTORY_INVALID;
    code = available(r, now_ms);
    if (code != GIN_HISTORY_OK) return code;
    out->cursor = GIN_HISTORY_AFTER;
    out->anchor = r->status.fetch_after;
    out->limit = r->page_limit;
    r->last_ms = now_ms;
    return GIN_HISTORY_OK;
}
/* Build a sorted borrowed candidate, then replace ownership only after the
 * immutable constructor succeeds. No rejected feed invalidates borrowed views. */
static enum gin_history_code ingest(struct gin_history_recovery *r,
    const struct gin_history_page *page, uint64_t now, int live)
{
    const struct gin_history_row *refs[GIN_HISTORY_ROWS_MAX + GIN_HISTORY_PAGE_ROWS_MAX];
    const struct gin_history_request *request;
    const struct gin_history_row *row;
    struct gin_history_page *replacement = NULL;
    enum gin_history_code code;
    size_t n, old_count, i, j, count = 0;
    int new_live = 0;
    code = available(r, now);
    if (code != GIN_HISTORY_OK) return code;
    if (!page || gin_history_page_source(page) !=
        (live ? GIN_HISTORY_LIVE : GIN_HISTORY_REST)) return GIN_HISTORY_INVALID;
    if (!gin_history_scope_equal(gin_history_page_scope(r->view),
        gin_history_page_scope(page))) return GIN_HISTORY_STALE;
    n = gin_history_page_count(page);
    if ((live && n != 1) || (!live && n > r->page_limit)) return GIN_HISTORY_INVALID;
    request = gin_history_page_request(page);
    if (!live && (!request || request->cursor != GIN_HISTORY_AFTER ||
        request->anchor != r->status.fetch_after || request->limit != r->page_limit))
        return GIN_HISTORY_INVALID;
    for (i = 0; i < n; i++) {
        row = gin_history_page_row(page, i);
        if (!row || row->id <= 0 || (!live && row->id <= r->status.fetch_after) ||
            (i && row->id <= gin_history_page_row(page, i - 1)->id))
            return GIN_HISTORY_INVALID;
    }
    if (live) {
        new_live = 1;
        for (i = 0; i < r->status.live_rows; i++)
            if (r->live_ids[i] == gin_history_page_row(page, 0)->id) new_live = 0;
    }
    old_count = gin_history_page_count(r->view);
    i = 0; j = 0;
    while (i < old_count || j < n) {
        const struct gin_history_row *a = gin_history_page_row(r->view, i);
        const struct gin_history_row *b = gin_history_page_row(page, j);
        if (!b || (a && a->id < b->id)) { refs[count++] = a; i++; }
        else if (!a || b->id < a->id) { refs[count++] = b; j++; }
        else {
            if (!gin_history_row_equal(a, b)) return GIN_HISTORY_CONFLICT;
            refs[count++] = a; i++; j++;
        }
    }
    if (count > GIN_HISTORY_ROWS_MAX ||
        (new_live && r->status.live_rows == GIN_HISTORY_LIVE_ROWS_MAX)) {
        r->status.state = GIN_HISTORY_INCOMPLETE_LIMIT;
        r->last_ms = now;
        return GIN_HISTORY_LIMIT;
    }
    code = gin_history_copy_rows(gin_history_page_scope(r->view), refs, count,
        GIN_HISTORY_MERGED, NULL, &replacement);
    if (code != GIN_HISTORY_OK) {
        if (code == GIN_HISTORY_LIMIT) {
            r->status.state = GIN_HISTORY_INCOMPLETE_LIMIT;
            r->last_ms = now;
        }
        return code;
    }
    gin_history_page_free(r->view);
    r->view = replacement;
    r->status.rows = count;
    r->status.owned_bytes = gin_history_page_owned_bytes(replacement);
    r->last_ms = now;
    if (live) {
        if (new_live) r->live_ids[r->status.live_rows++] = gin_history_page_row(page, 0)->id;
    } else {
        r->status.pages++;
        if (n) r->status.fetch_after = gin_history_page_row(page, n - 1)->id;
        if (n < r->page_limit) r->status.state = GIN_HISTORY_VISIBLE_DRAINED;
        else if (r->status.pages == GIN_HISTORY_PAGES_MAX)
            r->status.state = GIN_HISTORY_INCOMPLETE_LIMIT;
    }
    return GIN_HISTORY_OK;
}
enum gin_history_code gin_history_recovery_page(struct gin_history_recovery *r,
    const struct gin_history_page *page, uint64_t now_ms)
{
    return ingest(r, page, now_ms, 0);
}
enum gin_history_code gin_history_recovery_live(struct gin_history_recovery *r,
    const struct gin_history_page *page, uint64_t now_ms)
{
    return ingest(r, page, now_ms, 1);
}
const struct gin_history_page *gin_history_recovery_view(const struct gin_history_recovery *r)
{
    return r ? r->view : NULL;
}
