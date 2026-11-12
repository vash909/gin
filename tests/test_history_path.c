#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_history.h"
#include <assert.h>
#include <string.h>
#include <stdint.h>
/* Path helper isolation only. Real validators are exercised by page and native
 * integration tests; these facades make no JSON/routing qualification claim. */
static enum gin_history_code scope_code = GIN_HISTORY_OK;
static enum gin_history_code request_code = GIN_HISTORY_OK;
enum gin_history_code gin_history_scope_check(const struct gin_history_scope *s)
{ return s ? scope_code : GIN_HISTORY_INVALID; }
enum gin_history_code gin_history_request_check(const struct gin_history_request *r)
{ return r ? request_code : GIN_HISTORY_INVALID; }
int main(void)
{
    struct gin_history_scope s = {0};
    struct gin_history_request q = { GIN_HISTORY_LATEST, 0, 50 };
    char out[GIN_HISTORY_PATH_MAX + 1u], saved[sizeof(out)];
    size_t n = 77;
    s.origin = "https://example.test";
    s.subject_id = "12345678-1234-1234-1234-123456789abc";
    s.network_slug = "Net"; s.target = "#Caf\303\251+%";
    s.chantypes = "#"; s.own_nick = "Self";
    s.network_id = 7; s.socket_epoch = 1;
    s.role = GIN_RENDER_CHANNEL; s.encryption = GIN_RENDER_MODE_PLAIN;
    memset(out, 'X', sizeof(out));
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_OK);
    assert(!strcmp(out, "/networks/Net/channels/%23Caf%C3%A9%2B%25/messages?limit=50"));
    assert(n == strlen(out));
    q.cursor = GIN_HISTORY_AFTER; q.anchor = INT64_MAX; q.limit = 200;
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_OK);
    assert(strstr(out, "?limit=200&after=9223372036854775807"));
    q.anchor = 0;
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_OK);
    assert(strstr(out, "&after=0"));
    q.cursor = GIN_HISTORY_BEFORE; q.anchor = 1; q.limit = 1;
    s.network_slug = "AZ.az-09_~"; s.target = "$server";
    s.role = GIN_RENDER_STATUS;
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_OK);
    assert(!strcmp(out, "/networks/AZ.az-09_~/channels/%24server/messages?limit=1&before=1"));
    n = 19; memset(out, 'Q', sizeof(out)); memcpy(saved, out, sizeof(out));
    assert(gin_history_path(&s, &q, out, 2, &n) == GIN_HISTORY_LIMIT);
    assert(n == 19 && !memcmp(out, saved, sizeof(out)));
    scope_code = GIN_HISTORY_UNSUPPORTED;
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_UNSUPPORTED);
    assert(n == 19 && !memcmp(out, saved, sizeof(out)));
    scope_code = GIN_HISTORY_OK; request_code = GIN_HISTORY_INVALID;
    assert(gin_history_path(&s, &q, out, sizeof(out), &n) == GIN_HISTORY_INVALID);
    assert(n == 19 && !memcmp(out, saved, sizeof(out)));
    assert(gin_history_path(&s, &q, NULL, sizeof(out), &n) == GIN_HISTORY_INVALID);
    assert(gin_history_path(&s, &q, out, sizeof(out), NULL) == GIN_HISTORY_INVALID);
    return 0;
}
