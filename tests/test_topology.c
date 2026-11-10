#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_topology.h"
#include "../src/topology_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

/* Isolated native unit: inject only owned-allocation failures, without a
 * production hook or changing Jansson's allocator for objects that outlive it. */
static size_t alloc_call, fail_call;
static void *test_calloc(size_t count, size_t size)
{
    if (++alloc_call == fail_call) return NULL;
    return calloc(count, size);
}
#define calloc test_calloc
#include "../src/topology.c"
#undef calloc

static void empty_boot(void)
{
    const char body[] = "{\"networks\":[],\"channels\":{},\"heads\":{}}";
    struct gin_topology *t = NULL;
    assert(gin_topology_parse_boot((const unsigned char *)body, strlen(body), "#", &t) == GIN_TOPOLOGY_OK);
    assert(t != NULL);
    assert(gin_topology_network_count(t) == 0);
    assert(gin_topology_window_count(t) == 0);
    assert(!gin_topology_queries_known(t));
    gin_topology_free(t);
    gin_topology_free(NULL);
}

static void invalid_json(void)
{
    struct gin_topology *t = NULL;
    const char *bad[] = {"", "{}", "[]", "{\"networks\":[],\"networks\":[],\"channels\":{},\"heads\":{}}", "{\"networks\":[],\"channels\":{},\"heads\":{}}x"};
    size_t i;
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        assert(gin_topology_parse_boot((const unsigned char *)bad[i], strlen(bad[i]), "#", &t) == GIN_TOPOLOGY_MALFORMED);
        assert(t == NULL);
    }
    assert(gin_topology_parse_boot(NULL, 0, "#", &t) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_parse_boot((const unsigned char *)"{}", 2, "##", &t) == GIN_TOPOLOGY_INVALID);
}

static const char boot[] =
    "{\"networks\":[{\"id\":9223372036854775807,\"slug\":\"Net\",\"nick\":\"Self\"},"
    "{\"id\":1,\"slug\":\"net\",\"nick\":\"Other\"}],"
    "\"channels\":{\"Net\":[{\"name\":\"#Room\",\"joined\":false,\"source\":\"autojoin\"},"
    "{\"name\":\"&Else\",\"joined\":true,\"source\":\"joined\"}],"
    "\"net\":[{\"name\":\"#Room\",\"joined\":true,\"source\":\"joined\"}]},"
    "\"heads\":{\"Net\":{\"NotAPeer\":{\"arbitrary\":[true,null]}}}}";

static void networks_channels(void)
{
    struct gin_topology *t = NULL;
    const struct gin_topology_network *net;
    const struct gin_topology_window *w;
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &t) == GIN_TOPOLOGY_OK);
    assert(gin_topology_network_count(t) == 2);
    assert(gin_topology_window_count(t) == 3);
    net = gin_topology_network_at(t, 1);
    assert(net && net->id == INT64_MAX && !strcmp(net->slug, "Net"));
    assert(!strcmp(net->own_nick, "Self") && !strcmp(net->component, "%4Eet"));
    w = gin_topology_window_at(t, 1);
    assert(w && w->network_id == INT64_MAX && !strcmp(w->name.canonical_target, "#room"));
    assert(w->role == GIN_REMOTE_CHANNEL && !w->joined && w->source == GIN_CHANNEL_AUTOJOIN);
    assert(!w->opened_at && !strcmp(w->name.original_wire_target, "#Room"));
    w = gin_topology_window_at(t, 2);
    assert(w->role == GIN_REMOTE_CHANNEL && w->name.kind == GIN_WINDOW_NONCHANNEL);
    assert(!strcmp(w->name.window_component, "@%26else"));
    assert(!gin_topology_network_at(t, 2) && !gin_topology_window_at(t, 3));
    assert(!gin_topology_network_at(NULL, 0) && !gin_topology_window_at(NULL, 0));
    assert(!gin_topology_network_count(NULL) && !gin_topology_window_count(NULL));
    assert(!gin_topology_queries_known(NULL));
    assert(gin_topology_find_id(t, INT64_MAX) == net);
    assert(gin_topology_find_slug(t, "Net") == net);
    assert(!gin_topology_find_slug(t, "%4Eet"));
    assert(gin_topology_find_window(t, INT64_MAX, "&else") == w);
    assert(!gin_topology_find_window(t, INT64_MAX, "&Else"));
    gin_topology_free(t);
}

static void boot_conflicts(void)
{
    const char *bad[] = {
        "{\"networks\":[{\"id\":1,\"slug\":\"a\",\"nick\":\"n\"},{\"id\":1,\"slug\":\"b\",\"nick\":\"n\"}],\"channels\":{\"a\":[],\"b\":[]},\"heads\":{}}",
        "{\"networks\":[{\"id\":1,\"slug\":\"a\",\"nick\":\"n\"},{\"id\":2,\"slug\":\"a\",\"nick\":\"n\"}],\"channels\":{\"a\":[]},\"heads\":{}}",
        "{\"networks\":[{\"id\":1,\"slug\":\"a\",\"nick\":\"n\"}],\"channels\":{\"a\":[{\"name\":\"#A\",\"joined\":true,\"source\":\"joined\"},{\"name\":\"#a\",\"joined\":false,\"source\":\"autojoin\"}]},\"heads\":{}}"
    };
    size_t i;
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        struct gin_topology *t = NULL;
        assert(gin_topology_parse_boot((const unsigned char *)bad[i], strlen(bad[i]), "#", &t) == GIN_TOPOLOGY_CONFLICT);
        assert(!t);
    }
}

static const char queries[] =
    "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}],"
    "\"9223372036854775807\":[{\"network_id\":9223372036854775807,\"target_nick\":\"Peer\",\"opened_at\":\"later\"}]}}";
static void query_replacement(void)
{
    struct gin_topology *base = NULL, *a = NULL, *b = NULL;
    const struct gin_topology_window *borrowed;
    const char empty[] = "{\"kind\":\"query_windows_list\",\"windows\":{}}";
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &base) == GIN_TOPOLOGY_OK);
    borrowed = gin_topology_window_at(base, 0);
    assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &a) == GIN_TOPOLOGY_OK);
    assert(gin_topology_window_count(a) == 5 && gin_topology_queries_known(a));
    assert(!gin_topology_queries_known(base) && gin_topology_window_count(base) == 3);
    assert(borrowed == gin_topology_window_at(base, 0));
    gin_topology_free(base);
    borrowed = gin_topology_window_at(a, 1);
    assert(borrowed->role == GIN_REMOTE_QUERY && !strcmp(borrowed->name.canonical_target, "peer"));
    assert(!strcmp(borrowed->opened_at, "opaque") && borrowed->source == GIN_CHANNEL_NONE && !borrowed->joined);
    assert(gin_topology_apply_queries(a, (const unsigned char *)empty, strlen(empty), &b) == GIN_TOPOLOGY_OK);
    gin_topology_free(a);
    assert(gin_topology_queries_known(b) && gin_topology_window_count(b) == 3);
    assert(!strcmp(gin_topology_network_at(b, 1)->own_nick, "Self"));
    assert(!strcmp(gin_topology_window_at(b, 2)->name.original_wire_target, "&Else"));
    gin_topology_free(b);
}

static void http_statuses(void)
{
    const unsigned char scope[] = "{\"error\":\"client_token_scope\"}";
    assert(gin_topology_http_status(401, NULL, 0) == GIN_TOPOLOGY_UNAUTHORIZED);
    assert(gin_topology_http_status(429, NULL, 0) == GIN_TOPOLOGY_THROTTLED);
    assert(gin_topology_http_status(403, scope, sizeof(scope)-1) == GIN_TOPOLOGY_SCOPE);
    assert(gin_topology_http_status(403, scope, sizeof(scope)-2) == GIN_TOPOLOGY_FORBIDDEN);
    assert(gin_topology_http_status(403, NULL, 0) == GIN_TOPOLOGY_FORBIDDEN);
    assert(gin_topology_http_status(200, scope, sizeof(scope)-1) == GIN_TOPOLOGY_REMOTE);
}


static void expect_boot(const char *body, enum gin_topology_code expected)
{
    struct gin_topology *t = NULL;
    enum gin_topology_code actual = gin_topology_parse_boot((const unsigned char *)body, strlen(body), "#", &t);
    if (actual != expected) fprintf(stderr, "boot code: got %d expected %d\n", (int)actual, (int)expected);
    assert(actual == expected);
    if (expected != GIN_TOPOLOGY_OK) assert(!t);
    gin_topology_free(t);
}

static void malformed_matrix(void)
{
    const char *network_fields[] = {
        "\"id\":0,\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":-1,\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":true,\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":1.0,\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":\"1\",\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":9223372036854775808,\"slug\":\"n\",\"nick\":\"x\"",
        "\"id\":1,\"slug\":\"../n\",\"nick\":\"x\"",
        "\"id\":1,\"slug\":\"n\",\"nick\":\"bad nick\"",
        "\"id\":1,\"slug\":\"n\",\"nick\":null"
    };
    const char *channel_rows[] = {
        "\"#a\"", "null", "{}", "{\"name\":\"#a\",\"joined\":1,\"source\":\"joined\"}",
        "{\"name\":\"#a\",\"joined\":true,\"source\":\"unknown\"}",
        "{\"name\":\"../bad\",\"joined\":true,\"source\":\"joined\"}"
    };
    char b[2048];
    size_t i;
    for (i = 0; i < sizeof(network_fields)/sizeof(network_fields[0]); i++) {
        snprintf(b, sizeof(b), "{\"networks\":[{%s}],\"channels\":{\"n\":[]},\"heads\":{}}", network_fields[i]);
        expect_boot(b, GIN_TOPOLOGY_MALFORMED);
    }
    for (i = 0; i < sizeof(channel_rows)/sizeof(channel_rows[0]); i++) {
        snprintf(b, sizeof(b), "{\"networks\":[{\"id\":1,\"slug\":\"n\",\"nick\":\"x\"}],\"channels\":{\"n\":[%s]},\"heads\":{}}", channel_rows[i]);
        expect_boot(b, GIN_TOPOLOGY_MALFORMED);
    }
    expect_boot("{\"networks\":[],\"channels\":{\"unknown\":[]},\"heads\":{}}", GIN_TOPOLOGY_MALFORMED);
    expect_boot("{\"networks\":[{\"id\":1,\"slug\":\"n\",\"nick\":\"x\"}],\"channels\":{},\"heads\":{}}", GIN_TOPOLOGY_MALFORMED);
    expect_boot("{\"networks\":[],\"channels\":{},\"heads\":{},\"extra\":\"\\u0000\"}", GIN_TOPOLOGY_MALFORMED);
    expect_boot("{\"networks\":[],\"channels\":{},\"heads\":{},\"extra\":{\"x\":1,\"\\u0078\":2}}", GIN_TOPOLOGY_MALFORMED);
    expect_boot("{\"networks\":[],\"channels\":{},\"heads\":{},\"extra\":\"\300\200\"}", GIN_TOPOLOGY_MALFORMED);
    expect_boot("{\"heads\":{},\"extra\":[\"escaped\\\"[{\",42],\"channels\":{},\"networks\":[]}", GIN_TOPOLOGY_OK);
    expect_boot("{\"networks\":[{\"kind\":\"visitor\",\"id\":1,\"slug\":\"n\",\"nick\":\"x\"}],\"channels\":{\"n\":[]},\"heads\":{}}", GIN_TOPOLOGY_OK);
}

static void queries_invalid_and_lifetimes(void)
{
    struct gin_topology *base = NULL, *t = NULL, *out = NULL;
    const struct gin_topology_window *borrowed;
    const char *keys[] = {"0", "01", "+1", "-1", " 1", "1 ", "9223372036854775808", "2"};
    const char *rows[] = {
        "{\"network_id\":2,\"target_nick\":\"p\",\"opened_at\":\"x\"}",
        "{\"network_id\":true,\"target_nick\":\"p\",\"opened_at\":\"x\"}",
        "{\"network_id\":1,\"target_nick\":\"p\",\"opened_at\":\"\"}",
        "{\"network_id\":1,\"target_nick\":null,\"opened_at\":\"x\"}",
        "{\"network_id\":1,\"target_nick\":\"bad nick\",\"opened_at\":\"x\"}"
    };
    char b[2048];
    size_t i;
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &base) == GIN_TOPOLOGY_OK);
    assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &t) == GIN_TOPOLOGY_OK);
    borrowed = gin_topology_find_window(t, 1, "peer");
    for (i = 0; i < sizeof(keys)/sizeof(keys[0]); i++) {
        snprintf(b, sizeof(b), "{\"kind\":\"query_windows_list\",\"windows\":{\"%s\":[]}}", keys[i]);
        assert(gin_topology_apply_queries(t, (const unsigned char *)b, strlen(b), &out) == GIN_TOPOLOGY_MALFORMED);
        assert(!out && borrowed == gin_topology_find_window(t, 1, "peer"));
    }
    for (i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        snprintf(b, sizeof(b), "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[%s]}}", rows[i]);
        assert(gin_topology_apply_queries(t, (const unsigned char *)b, strlen(b), &out) == GIN_TOPOLOGY_MALFORMED);
        assert(!out && !strcmp(borrowed->opened_at, "opaque"));
    }
    strcpy(b, "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"#ROOM\",\"opened_at\":\"x\"}]}}");
    assert(gin_topology_apply_queries(t, (const unsigned char *)b, strlen(b), &out) == GIN_TOPOLOGY_CONFLICT);
    assert(!out);
    strcpy(b, "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":1,\"target_nick\":\"PEER\",\"opened_at\":\"y\"}]}}");
    assert(gin_topology_apply_queries(t, (const unsigned char *)b, strlen(b), &out) == GIN_TOPOLOGY_CONFLICT);
    assert(!out);
    assert(gin_topology_apply_queries(t, (const unsigned char *)queries, strlen(queries), &t) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &t) == GIN_TOPOLOGY_INVALID);
    assert(borrowed == gin_topology_find_window(t, 1, "peer"));
    assert(gin_topology_apply_queries(NULL, (const unsigned char *)queries, strlen(queries), &out) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_apply_queries(t, NULL, 0, &out) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_apply_queries(t, (const unsigned char *)queries, strlen(queries), NULL) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), NULL, &out) == GIN_TOPOLOGY_INVALID);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", NULL) == GIN_TOPOLOGY_INVALID);
    strcpy(b, "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[]}}");
    assert(gin_topology_apply_queries(t, (const unsigned char *)b, strlen(b), &out) == GIN_TOPOLOGY_OK);
    assert(gin_topology_window_count(out) == 3 && gin_topology_queries_known(out));
    gin_topology_free(out); out = NULL;
    assert(gin_topology_apply_queries(t, (const unsigned char *)queries, strlen(queries), &out) == GIN_TOPOLOGY_OK);
    gin_topology_free(base); gin_topology_free(t);
    assert(!strcmp(gin_topology_find_window(out, INT64_MAX, "peer")->opened_at, "later"));
    gin_topology_free(out);
}

static void byte_and_depth_bounds(void)
{
    struct gin_topology *t = NULL;
    unsigned char *b = malloc(GIN_TOPOLOGY_BODY_MAX + 1);
    size_t i, j, n;
    const char *prefix = "{\"networks\":[],\"channels\":{},\"heads\":{},\"extra\":";
    assert(b);
    for (i = 0; i < strlen(boot); i++) {
        assert(gin_topology_parse_boot((const unsigned char *)boot, i, "#", &t) != GIN_TOPOLOGY_OK);
        assert(!t);
    }
    memcpy(b, boot, strlen(boot)); b[20] = 0;
    assert(gin_topology_parse_boot(b, strlen(boot), "#", &t) == GIN_TOPOLOGY_MALFORMED);
    for (i = 31; i <= 32; i++) {
        n = strlen(prefix); memcpy(b, prefix, n);
        for (j = 0; j < i; j++) b[n++] = '[';
        b[n++] = '0';
        for (j = 0; j < i; j++) b[n++] = ']';
        b[n++] = '}';
        assert(gin_topology_parse_boot(b, n, "#", &t) == (i == 31 ? GIN_TOPOLOGY_OK : GIN_TOPOLOGY_LIMIT));
        gin_topology_free(t); t = NULL;
    }
    n = strlen(prefix); memcpy(b, prefix, n); b[n++] = '0'; b[n++] = '}';
    memset(b + n, ' ', GIN_TOPOLOGY_BODY_MAX + 1 - n);
    assert(gin_topology_parse_boot(b, GIN_TOPOLOGY_BODY_MAX, "#", &t) == GIN_TOPOLOGY_OK);
    gin_topology_free(t); t = NULL;
    assert(gin_topology_parse_boot(b, GIN_TOPOLOGY_BODY_MAX + 1, "#", &t) == GIN_TOPOLOGY_LIMIT);
    assert(gin_topology_http_status(403, b, GIN_TOPOLOGY_BODY_MAX + 1) == GIN_TOPOLOGY_FORBIDDEN);
    free(b);
}

static void allocation_failures(void)
{
    struct gin_topology *base = NULL, *out = NULL;
    size_t i, calls;
    alloc_call = 0;
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &base) == GIN_TOPOLOGY_OK);
    calls = alloc_call;
    for (i = 1; i <= calls; i++) {
        alloc_call = 0; fail_call = i;
        assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &out) == GIN_TOPOLOGY_NOMEM);
        assert(!out);
    }
    fail_call = 0; alloc_call = 0;
    assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &out) == GIN_TOPOLOGY_OK);
    calls = alloc_call; gin_topology_free(out); out = NULL;
    for (i = 1; i <= calls; i++) {
        alloc_call = 0; fail_call = i;
        assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &out) == GIN_TOPOLOGY_NOMEM);
        assert(!out && gin_topology_window_count(base) == 3);
    }
    fail_call = 0; gin_topology_free(base);
}

static void count_and_name_bounds(void)
{
    json_t *root = json_object(), *nets = json_array(), *channels = json_object();
    json_t *rows = json_array();
    char key[32], target[300];
    char *body;
    size_t i;
    struct gin_topology *t = NULL;
    assert(root && nets && channels && rows);
    assert(!json_object_set_new(root, "networks", nets));
    assert(!json_object_set_new(root, "channels", channels));
    assert(!json_object_set_new(root, "heads", json_object()));
    for (i = 0; i <= GIN_TOPOLOGY_NETWORK_MAX; i++) {
        snprintf(key, sizeof(key), "n%lu", (unsigned long)i);
        assert(!json_array_append_new(nets, json_pack("{s:I,s:s,s:s}", "id", (json_int_t)i+1, "slug", key, "nick", "n")));
        assert(!json_object_set_new(channels, key, json_array()));
        if (i + 1 >= GIN_TOPOLOGY_NETWORK_MAX) {
            body = json_dumps(root, JSON_COMPACT); assert(body);
            expect_boot(body, i + 1 == GIN_TOPOLOGY_NETWORK_MAX ? GIN_TOPOLOGY_OK : GIN_TOPOLOGY_LIMIT);
            free(body);
        }
    }
    assert(!json_array_clear(nets)); assert(!json_object_clear(channels));
    assert(!json_array_append_new(nets, json_pack("{s:i,s:s,s:s}", "id", 1, "slug", "n", "nick", "x")));
    assert(!json_object_set_new(channels, "n", rows));
    for (i = 0; i <= GIN_TOPOLOGY_WINDOW_MAX; i++) {
        snprintf(target, sizeof(target), "#w%lu", (unsigned long)i);
        assert(!json_array_append_new(rows, json_pack("{s:s,s:b,s:s}", "name", target, "joined", 1, "source", "joined")));
        if (i + 1 >= GIN_TOPOLOGY_WINDOW_MAX) {
            body = json_dumps(root, JSON_COMPACT); assert(body);
            expect_boot(body, i + 1 == GIN_TOPOLOGY_WINDOW_MAX ? GIN_TOPOLOGY_OK : GIN_TOPOLOGY_LIMIT);
            free(body);
        }
    }
    assert(!json_array_clear(rows));
    memset(target, 'a', 256); target[256] = 0;
    assert(!json_array_append_new(rows, json_pack("{s:s,s:b,s:s}", "name", target, "joined", 1, "source", "joined")));
    body = json_dumps(root, JSON_COMPACT); assert(body);
    expect_boot(body, GIN_TOPOLOGY_MALFORMED); free(body); /* @ + 256 > component cap */
    target[0] = '#';
    assert(!json_array_set_new(rows, 0, json_pack("{s:s,s:b,s:s}", "name", target, "joined", 1, "source", "joined")));
    body = json_dumps(root, JSON_COMPACT); assert(body);
    assert(gin_topology_parse_boot((const unsigned char *)body, strlen(body), "#", &t) == GIN_TOPOLOGY_OK);
    {
        const struct gin_topology_window *w = gin_topology_window_at(t, 0);
        assert(w != NULL);
        assert(strlen(w->name.canonical_target) == 256);
    }
    gin_topology_free(t); free(body);
    target[256] = 'a'; target[257] = 0;
    assert(!json_array_set_new(rows, 0, json_pack("{s:s,s:b,s:s}", "name", target, "joined", 1, "source", "joined")));
    body = json_dumps(root, JSON_COMPACT); assert(body);
    expect_boot(body, GIN_TOPOLOGY_MALFORMED); free(body);
    json_decref(root);
    {
        struct gin_topology dummy;
        void *p = NULL;
        memset(&dummy, 0, sizeof(dummy));
        dummy.owned = GIN_TOPOLOGY_OWNED_MAX - 1;
        assert(owned_alloc(&dummy, SIZE_MAX, 2, &p) == GIN_TOPOLOGY_LIMIT && !p);
        assert(owned_alloc(&dummy, 1, 2, &p) == GIN_TOPOLOGY_LIMIT && !p);
        assert(owned_alloc(&dummy, 1, 1, &p) == GIN_TOPOLOGY_OK && p);
        free(p);
    }
}

static size_t json_alloc_call, json_fail_call;
static void *test_json_malloc(size_t n)
{
    if (++json_alloc_call == json_fail_call) return NULL;
    return malloc(n);
}
static void json_allocation_failures(void)
{
    json_malloc_t original_malloc;
    json_free_t original_free;
    size_t i, calls;
    struct gin_topology *t = NULL;
    json_get_alloc_funcs(&original_malloc, &original_free);
    json_alloc_call = 0; json_fail_call = 0;
    json_set_alloc_funcs(test_json_malloc, free);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &t) == GIN_TOPOLOGY_OK);
    calls = json_alloc_call;
    gin_topology_free(t); t = NULL;
    for (i = 1; i <= calls; i++) {
        enum gin_topology_code code;
        json_alloc_call = 0; json_fail_call = i;
        code = gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &t);
        /* Jansson 2.14/2.15 may label a failed string allocation as syntax
         * error. Do not install a process-global production allocator just to
         * override its error reporting. Every failure must still clean up. */
        if (code == GIN_TOPOLOGY_OK) {
            /* Decoder scratch growth can recover from one failed allocation. */
            assert(t && gin_topology_network_count(t) == 2 && gin_topology_window_count(t) == 3);
            assert(!strcmp(gin_topology_network_at(t, 1)->slug, "Net"));
            gin_topology_free(t); t = NULL;
        } else assert((code == GIN_TOPOLOGY_NOMEM || code == GIN_TOPOLOGY_MALFORMED) && !t);
        if (i == 1) assert(code == GIN_TOPOLOGY_NOMEM);
    }
    json_set_alloc_funcs(original_malloc, original_free);
    json_fail_call = 0;
}

static void query_metadata_and_copied_chantypes(void)
{
    struct gin_topology *base = NULL, *t = NULL;
    char chantypes[] = "#&";
    char stamp[66], body[512];
    size_t i;
    const struct gin_topology_window *w;
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), chantypes, &base) == GIN_TOPOLOGY_OK);
    chantypes[0] = 0;
    for (i = 64; i <= 65; i++) {
        memset(stamp, 'x', i); stamp[i] = 0;
        snprintf(body, sizeof(body), "{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"&Foo\",\"opened_at\":\"%s\"}]}}", stamp);
        assert(gin_topology_apply_queries(base, (const unsigned char *)body, strlen(body), &t) ==
            (i == 64 ? GIN_TOPOLOGY_OK : GIN_TOPOLOGY_MALFORMED));
        if (t) {
            w = gin_topology_find_window(t, 1, "&foo");
            assert(w && w->role == GIN_REMOTE_QUERY && w->name.kind == GIN_WINDOW_CHANNEL);
            assert(!strcmp(w->name.window_component, "+26foo"));
            gin_topology_free(t); t = NULL;
        }
    }
    gin_topology_free(base);
}

int main(void)
{
    empty_boot();
    invalid_json();
    networks_channels();
    boot_conflicts();
    query_replacement();
    http_statuses();
    malformed_matrix();
    queries_invalid_and_lifetimes();
    byte_and_depth_bounds();
    allocation_failures();
    count_and_name_bounds();
    json_allocation_failures();
    query_metadata_and_copied_chantypes();
    assert(!strcmp(gin_topology_error(GIN_TOPOLOGY_OK), "ok"));
    assert(!strcmp(gin_topology_error(GIN_TOPOLOGY_NOMEM), "out-of-memory"));
    assert(!strcmp(gin_topology_error((enum gin_topology_code)999), "unknown-topology-error"));
    puts("topology: ok");
    return 0;
}
