#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gin_phoenix.h"
#include <jansson.h>

/* Translation-unit-only owned-allocation injection; no production hooks. */
static size_t fail_at = (size_t)-1, allocation_index, live_owned;
static struct { void *p; size_t n; } allocations[32];
static void *test_malloc(size_t n)
{
    size_t i;
    void *p;
    if (allocation_index++ == fail_at) return NULL;
    p = malloc(n);
    if (!p) return NULL;
    for (i = 0; i < 32; i++) if (!allocations[i].p) {
        allocations[i].p = p; allocations[i].n = n; live_owned++; return p;
    }
    assert(0); return NULL;
}
static void *test_calloc(size_t n, size_t size)
{
    void *p;
    assert(!n || size <= (size_t)-1 / n);
    p = test_malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}
static void test_free(void *p)
{
    size_t i, j;
    if (!p) return;
    for (i = 0; i < 32; i++) if (allocations[i].p == p) {
        for (j = 0; j < allocations[i].n; j++) assert(!((unsigned char *)p)[j]);
        allocations[i].p = NULL; live_owned--; free(p); return;
    }
    assert(0);
}
#define malloc test_malloc
#define calloc test_calloc
#define free test_free
#include "../src/phoenix.c"
#undef malloc
#undef calloc
#undef free

static const char join[] = "[\"1\",\"1\",\"grappa:user:fixture\",\"phx_join\",{}]";
static void golden_join(void)
{
    struct gin_phoenix_frame *frame = NULL;
    const struct gin_phoenix_fields *fields;
    assert(gin_phoenix_decode((const unsigned char *)join, strlen(join), &frame) == GIN_PHOENIX_OK);
    fields = gin_phoenix_frame_fields(frame);
    assert(fields && !strcmp(fields->join_ref, "1") && !strcmp(fields->ref, "1"));
    assert(!strcmp(fields->topic, "grappa:user:fixture"));
    assert(!strcmp(fields->event, "phx_join"));
    assert(fields->payload.length == 2 && !memcmp(fields->payload.data, "{}", 2));
    gin_phoenix_frame_free(frame);
}
static void expect(const char *text, enum gin_phoenix_code code)
{
    struct gin_phoenix_frame *frame = NULL;
    enum gin_phoenix_code actual = gin_phoenix_decode((const unsigned char *)text, strlen(text), &frame);
    if (actual != code) fprintf(stderr, "expected code %d, got %d\n", (int)code, (int)actual);
    assert(actual == code);
    if (code != GIN_PHOENIX_OK) assert(frame == NULL);
    gin_phoenix_frame_free(frame);
}
static void schema(void)
{
    expect("[1,\"1\",\"t\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,true,\"t\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,{},\"t\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",[]]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{} , {\"extra\":1}]", GIN_PHOENIX_OK);
    expect("[null,null,\"t\\u0085\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}, {\"a\":1,\"\\u0061\":2}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}, {\"a\":\"\\u0000\"}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}] [null,null,\"t\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("{}", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\"]", GIN_PHOENIX_MALFORMED);
    expect("", GIN_PHOENIX_MALFORMED);
}
static void golden_encode(void)
{
    struct gin_phoenix_fields fields = { "1", "1", "grappa:user:fixture", "phx_join", { (const unsigned char *)"{}", 2 } };
    struct gin_phoenix_bytes bytes = { NULL, 0 };
    assert(gin_phoenix_encode(&fields, &bytes) == GIN_PHOENIX_OK);
    assert(bytes.length == strlen(join) && !memcmp(bytes.data, join, bytes.length));
    gin_phoenix_bytes_free(&bytes);
    assert(!bytes.data && !bytes.length);
    gin_phoenix_bytes_free(&bytes);
}
static void bounds(void)
{
    size_t depth;
    char text[512];
    for (depth = 31; depth <= 32; depth++) {
        size_t i, n = 0;
        struct gin_phoenix_bytes bytes = { NULL, 0 };
        struct gin_phoenix_fields fields = { NULL, "3", "phoenix", "heartbeat", { NULL, 0 } };
        for (i = 1; i < depth; i++) { memcpy(text + n, "{\"x\":", 5); n += 5; }
        memcpy(text + n, "{}", 2); n += 2;
        for (i = 1; i < depth; i++) text[n++] = '}';
        text[n] = 0;
        fields.payload.data = (const unsigned char *)text; fields.payload.length = n;
        assert(gin_phoenix_encode(&fields, &bytes) == (depth == 31 ? GIN_PHOENIX_OK : GIN_PHOENIX_LIMIT));
        if (depth == 31) {
            struct gin_phoenix_frame *f = NULL;
            assert(gin_phoenix_decode(bytes.data, bytes.length, &f) == GIN_PHOENIX_OK);
            gin_phoenix_frame_free(f);
        }
        gin_phoenix_bytes_free(&bytes);
    }
    {
        char *body = malloc(GIN_PHOENIX_BODY_MAX + 2u);
        size_t n = strlen(join);
        struct gin_phoenix_frame *f = NULL;
        assert(body);
        memcpy(body, join, n); memset(body+n, ' ', GIN_PHOENIX_BODY_MAX + 1u-n);
        assert(gin_phoenix_decode((unsigned char *)body, GIN_PHOENIX_BODY_MAX, &f) == GIN_PHOENIX_OK);
        gin_phoenix_frame_free(f); f = NULL;
        assert(gin_phoenix_decode((unsigned char *)body, GIN_PHOENIX_BODY_MAX + 1u, &f) == GIN_PHOENIX_LIMIT);
        assert(!f); free(body);
    }
    {
        size_t i, n = 0;
        memcpy(text, "[null,null,\"t\",\"e\",{},", 22); n = strlen("[null,null,\"t\",\"e\",{},");
        for (i = 0; i < 32; i++) text[n++] = '[';
        for (i = 0; i < 32; i++) text[n++] = ']';
        text[n++] = ']'; text[n] = 0;
        expect(text, GIN_PHOENIX_LIMIT);
    }
}
static void replies(void)
{
    const char *statuses[] = { "ok", "error", "future" };
    const char *refs[] = { NULL, "", "null", "0", "01", "1" };
    size_t i, j;
    for (i = 0; i < 3; i++) {
        char text[256];
        struct gin_phoenix_frame *f = NULL;
        const struct gin_phoenix_reply *r;
        snprintf(text, sizeof(text), "[\"1\",\"2\",\"t\",\"phx_reply\",{\"status\":\"%s\",\"response\":{},\"future\":1}]", statuses[i]);
        assert(gin_phoenix_decode((unsigned char *)text, strlen(text), &f) == GIN_PHOENIX_OK);
        r = gin_phoenix_frame_reply(f);
        assert(r && r->status == (enum gin_phoenix_reply_status)i);
        assert(!strcmp(r->status_text, statuses[i]));
        assert(r->response.length == 2 && !memcmp(r->response.data, "{}", 2));
        assert(gin_phoenix_reply_matches(f, "t", "1", "2"));
        assert(!gin_phoenix_reply_matches(f, "T", "1", "2"));
        assert(!gin_phoenix_reply_matches(f, "t", "01", "2"));
        assert(!gin_phoenix_reply_matches(f, "t", "1", "02"));
        gin_phoenix_frame_free(f);
    }
    for (i = 0; i < 6; i++) for (j = 0; j < 6; j++) {
        struct gin_phoenix_fields fields = { refs[i], refs[j], "t", "phx_reply", { (const unsigned char *)"{\"status\":\"ok\",\"response\":{}}", 29 } };
        struct gin_phoenix_bytes b = { NULL, 0 };
        struct gin_phoenix_frame *f = NULL;
        size_t k;
        fields.payload.length = strlen((const char *)fields.payload.data);
        assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK);
        assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
        for (k = 0; k < 6; k++) {
            assert(gin_phoenix_reply_matches(f, "t", refs[k], refs[j]) == (i == k));
            assert(gin_phoenix_reply_matches(f, "t", refs[i], refs[k]) == (j == k));
        }
        gin_phoenix_frame_free(f); gin_phoenix_bytes_free(&b);
    }
    expect("[null,null,\"t\",\"phx_reply\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"phx_reply\",{\"status\":\"\",\"response\":{}}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"phx_reply\",{\"status\":true,\"response\":{}}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"phx_reply\",{\"status\":\"ok\",\"response\":[]}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"phx_reply\",{\"status\":\"ok\"}]", GIN_PHOENIX_MALFORMED);
    {
        struct gin_phoenix_frame *f = NULL;
        assert(gin_phoenix_decode((const unsigned char *)join, strlen(join), &f) == GIN_PHOENIX_OK);
        assert(!gin_phoenix_frame_reply(f));
        assert(!gin_phoenix_reply_matches(f, "grappa:user:fixture", "1", "1"));
        gin_phoenix_frame_free(f);
    }
}
static void arguments_and_lifetime(void)
{
    unsigned char input[sizeof(join)];
    struct gin_phoenix_frame *f = NULL, *same;
    struct gin_phoenix_bytes b = { NULL, 1 };
    struct gin_phoenix_fields fields = { NULL, "3", "phoenix", "heartbeat", { (const unsigned char *)"{}", 2 } };
    assert(!gin_phoenix_frame_fields(NULL) && !gin_phoenix_frame_reply(NULL));
    assert(!gin_phoenix_reply_matches(NULL, "t", NULL, NULL));
    assert(gin_phoenix_decode(NULL, 1, &f) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_decode(NULL, 0, &f) == GIN_PHOENIX_MALFORMED);
    assert(gin_phoenix_decode((const unsigned char *)join, strlen(join), NULL) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID && !b.data && b.length == 1);
    b.length = 0;
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK);
    assert(b.length == strlen("[null,\"3\",\"phoenix\",\"heartbeat\",{}]"));
    assert(!memcmp(b.data, "[null,\"3\",\"phoenix\",\"heartbeat\",{}]", b.length));
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID);
    assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
    gin_phoenix_bytes_free(&b);
    assert(!strcmp(gin_phoenix_frame_fields(f)->topic, "phoenix"));
    same = f;
    assert(gin_phoenix_decode(NULL, 0, &f) == GIN_PHOENIX_INVALID && f == same);
    gin_phoenix_frame_free(f); f = NULL;
    memcpy(input, join, sizeof(join));
    assert(gin_phoenix_decode(input, strlen(join), &f) == GIN_PHOENIX_OK);
    memset(input, 'x', sizeof(input));
    assert(!strcmp(gin_phoenix_frame_fields(f)->topic, "grappa:user:fixture"));
    gin_phoenix_frame_free(f);
    gin_phoenix_frame_free(NULL); gin_phoenix_bytes_free(NULL);
    assert(!strcmp(gin_phoenix_error(GIN_PHOENIX_OK), "ok"));
    assert(gin_phoenix_error((enum gin_phoenix_code)999));
}
static void allocation_failures(void)
{
    const char *text = "[\"1\",\"2\",\"t\",\"phx_reply\",{\"status\":\"ok\",\"response\":{\"x\":1}}]";
    size_t i;
    int succeeded = 0;
    for (i = 0; i < 32; i++) {
        struct gin_phoenix_frame *f = NULL;
        enum gin_phoenix_code code;
        fail_at = i; allocation_index = 0;
        code = gin_phoenix_decode((const unsigned char *)text, strlen(text), &f);
        fail_at = (size_t)-1;
        if (code == GIN_PHOENIX_OK) { gin_phoenix_frame_free(f); succeeded = 1; break; }
        assert(code == GIN_PHOENIX_NOMEM && !f && !live_owned);
    }
    assert(succeeded && !live_owned);
    for (i = 0; i < 3; i++) {
        struct gin_phoenix_fields fields = { NULL, NULL, "t", "e", { (const unsigned char *)"{}", 2 } };
        struct gin_phoenix_bytes b = { NULL, 0 };
        enum gin_phoenix_code code;
        fail_at = i; allocation_index = 0;
        code = gin_phoenix_encode(&fields, &b);
        fail_at = (size_t)-1;
        if (code == GIN_PHOENIX_OK) { gin_phoenix_bytes_free(&b); break; }
        assert(code == GIN_PHOENIX_NOMEM && !b.data && !b.length && !live_owned);
    }
    assert(!live_owned);
}
static void field_limits_and_utf8(void)
{
    const size_t maxima[] = { GIN_PHOENIX_REF_MAX, GIN_PHOENIX_REF_MAX, GIN_PHOENIX_TOPIC_MAX, GIN_PHOENIX_EVENT_MAX };
    size_t i, extra;
    for (i = 0; i < 4; i++) for (extra = 0; extra < 2; extra++) {
        struct gin_phoenix_fields fields = { "1", "2", "t", "e", { (const unsigned char *)"{}", 2 } };
        const char **slots[4] = { &fields.join_ref, &fields.ref, &fields.topic, &fields.event };
        struct gin_phoenix_bytes b = { NULL, 0 };
        struct gin_phoenix_frame *f = NULL;
        char value[GIN_PHOENIX_TOPIC_MAX + 2u], remote[GIN_PHOENIX_TOPIC_MAX + 100u];
        size_t n = maxima[i] + extra;
        memset(value, 'x', n); value[n] = 0; *slots[i] = value;
        assert(gin_phoenix_encode(&fields, &b) == (extra ? GIN_PHOENIX_LIMIT : GIN_PHOENIX_OK));
        if (!extra) {
            assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
            gin_phoenix_frame_free(f); gin_phoenix_bytes_free(&b);
        }
        snprintf(remote, sizeof(remote), "[\"%s\",\"%s\",\"%s\",\"%s\",{}]", fields.join_ref, fields.ref, fields.topic, fields.event);
        expect(remote, extra ? GIN_PHOENIX_LIMIT : GIN_PHOENIX_OK);
    }
    for (extra = 0; extra < 2; extra++) {
        char status[GIN_PHOENIX_STATUS_MAX + 2u], remote[256];
        memset(status, 'x', GIN_PHOENIX_STATUS_MAX+extra); status[GIN_PHOENIX_STATUS_MAX+extra] = 0;
        snprintf(remote, sizeof(remote), "[null,null,\"t\",\"phx_reply\",{\"status\":\"%s\",\"response\":{}}]", status);
        expect(remote, extra ? GIN_PHOENIX_LIMIT : GIN_PHOENIX_OK);
    }
    expect("[null,null,\"t\",\"e\",{\"x\":\"[{}]\\\"\\\\\"}]", GIN_PHOENIX_OK);
    expect("[null,null,\"t\",\"e\",{}, {\"x\":\"\xc0\xaf\"}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}, {\"\xed\xa0\x80\":1}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}, {\"x\":\"\\uD800\"}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{}, {\"x\":1,}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{\"x\":1,\"\\u0078\":2}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\",\"e\",{\"\\u0000\":1}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\\u001f\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    expect("[null,null,\"t\\u007f\",\"e\",{}]", GIN_PHOENIX_MALFORMED);
    {
        const char *invalid[] = { "", "\x01", "\x7f", "\xc2\x85", "\xc0\xaf", "\xed\xa0\x80", "\xf4\x90\x80\x80", "\xe2\x82", "\x80" };
        for (i = 0; i < sizeof(invalid)/sizeof(invalid[0]); i++) {
            struct gin_phoenix_fields fields = { NULL, NULL, invalid[i], "e", { (const unsigned char *)"{}", 2 } };
            struct gin_phoenix_bytes b = { NULL, 0 };
            assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID && !b.data && !b.length);
        }
    }
    {
        unsigned char literal_nul[] = "[null,null,\"t\",\"e\",{\"x\":\"aXb\"}]";
        struct gin_phoenix_frame *f = NULL;
        *strchr((char *)literal_nul, 'X') = 0;
        assert(gin_phoenix_decode(literal_nul, sizeof(literal_nul)-1, &f) == GIN_PHOENIX_MALFORMED && !f);
    }
}
static void encoded_body_limit(void)
{
    size_t overhead = strlen("[null,null,\"t\",\"e\",]"), n = GIN_PHOENIX_BODY_MAX - overhead;
    unsigned char *payload = malloc(n + 2u);
    struct gin_phoenix_fields fields = { NULL, NULL, "t", "e", { NULL, 0 } };
    struct gin_phoenix_bytes b = { NULL, 0 };
    struct gin_phoenix_frame *f = NULL;
    assert(payload);
    memcpy(payload, "{\"x\":\"", 6); memset(payload+6, 'a', n-8); memcpy(payload+n-2, "\"}", 2);
    fields.payload.data = payload; fields.payload.length = n;
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK && b.length == GIN_PHOENIX_BODY_MAX);
    assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
    gin_phoenix_frame_free(f); gin_phoenix_bytes_free(&b);
    payload[n-2] = 'a'; memcpy(payload+n-1, "\"}", 2); fields.payload.length++;
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_LIMIT && !b.data && !b.length);
    free(payload);
}
static size_t library_index, library_fail_at;
static json_malloc_t library_malloc;
static void *failing_library_malloc(size_t n)
{ return library_index++ == library_fail_at ? NULL : library_malloc(n); }
static void jansson_failures(void)
{
    json_free_t library_free;
    size_t i, calls;
    struct gin_phoenix_frame *f = NULL;
    json_get_alloc_funcs(&library_malloc, &library_free);
    library_fail_at = (size_t)-1; library_index = 0;
    json_set_alloc_funcs(failing_library_malloc, library_free);
    assert(gin_phoenix_decode((const unsigned char *)join, strlen(join), &f) == GIN_PHOENIX_OK);
    gin_phoenix_frame_free(f); calls = library_index;
    json_set_alloc_funcs(library_malloc, library_free);
    for (i = 0; i < calls; i++) {
        enum gin_phoenix_code code;
        f = NULL; library_fail_at = i; library_index = 0;
        json_set_alloc_funcs(failing_library_malloc, library_free);
        code = gin_phoenix_decode((const unsigned char *)join, strlen(join), &f);
        assert(code == GIN_PHOENIX_OK || code == GIN_PHOENIX_NOMEM || code == GIN_PHOENIX_MALFORMED);
        if (code == GIN_PHOENIX_OK) { assert(f); gin_phoenix_frame_free(f); }
        else assert(!f);
        json_set_alloc_funcs(library_malloc, library_free);
        assert(!live_owned);
    }
    {
        struct gin_phoenix_fields fields = { "1", "1", "t", "e", { (const unsigned char *)"{}", 2 } };
        struct gin_phoenix_bytes b = { NULL, 0 };
        library_fail_at = (size_t)-1; library_index = 0;
        json_set_alloc_funcs(failing_library_malloc, library_free);
        assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK);
        gin_phoenix_bytes_free(&b); calls = library_index;
        json_set_alloc_funcs(library_malloc, library_free);
        for (i = 0; i < calls; i++) {
            enum gin_phoenix_code code;
            library_fail_at = i; library_index = 0;
            json_set_alloc_funcs(failing_library_malloc, library_free);
            code = gin_phoenix_encode(&fields, &b);
            assert(code == GIN_PHOENIX_OK || code == GIN_PHOENIX_NOMEM || code == GIN_PHOENIX_INVALID);
            if (code == GIN_PHOENIX_OK) gin_phoenix_bytes_free(&b);
            else assert(!b.data && !b.length);
            json_set_alloc_funcs(library_malloc, library_free);
            assert(!live_owned);
        }
    }
}
static void structured_encoding(void)
{
    static const unsigned char payload[] = "{\"text\":\"quote\\\" slash\\\\ line\\n \\uD83D\\uDE00\"}";
    struct gin_phoenix_fields fields = { "join\"\\", "", "topic:\"\\\xc3\xa9", "event\"\\", { payload, sizeof(payload)-1 } };
    struct gin_phoenix_bytes b = { NULL, 0 };
    struct gin_phoenix_frame *f = NULL;
    json_t *root;
    json_error_t error;
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK);
    memset(&error, 0, sizeof(error));
    root = json_loadb((const char *)b.data, b.length, JSON_REJECT_DUPLICATES, &error);
    assert(root && json_is_array(root) && json_array_size(root) == 5);
    assert(!strcmp(json_string_value(json_array_get(root, 2)), fields.topic));
    assert(!strcmp(json_string_value(json_object_get(json_array_get(root, 4), "text")), "quote\" slash\\ line\n \xf0\x9f\x98\x80"));
    json_decref(root);
    assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
    gin_phoenix_bytes_free(&b);
    assert(!strcmp(gin_phoenix_frame_fields(f)->join_ref, fields.join_ref));
    assert(!strcmp(gin_phoenix_frame_fields(f)->event, fields.event));
    gin_phoenix_frame_free(f);
    {
        const char *bad[] = { "[]", "", "{\"x\":1,\"x\":2}", "{\"x\":\"\\u0000\"}", "{}{}" };
        size_t i;
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            fields.payload.data = (const unsigned char *)bad[i]; fields.payload.length = strlen(bad[i]);
            assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID && !b.data && !b.length);
        }
        fields.payload.data = NULL; fields.payload.length = 1;
        assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID);
    }
    fields.event = "phx_reply";
    fields.payload.data = (const unsigned char *)"{}"; fields.payload.length = 2;
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_INVALID);
    fields.payload.data = (const unsigned char *)"{\"status\":\"future\",\"response\":{}}";
    fields.payload.length = strlen((const char *)fields.payload.data);
    assert(gin_phoenix_encode(&fields, &b) == GIN_PHOENIX_OK);
    f = NULL;
    assert(gin_phoenix_decode(b.data, b.length, &f) == GIN_PHOENIX_OK);
    gin_phoenix_bytes_free(&b);
    assert(gin_phoenix_frame_reply(f)->status == GIN_PHOENIX_REPLY_UNKNOWN);
    assert(!strcmp(gin_phoenix_frame_reply(f)->status_text, "future"));
    assert(gin_phoenix_frame_reply(f)->response.length == 2);
    assert(!memcmp(gin_phoenix_frame_reply(f)->response.data, "{}", 2));
    gin_phoenix_frame_free(f);
}
int main(void)
{
    golden_join();
    schema();
    golden_encode();
    bounds();
    replies();
    arguments_and_lifetime();
    allocation_failures();
    field_limits_and_utf8();
    encoded_body_limit();
    jansson_failures();
    structured_encoding();
    assert(!live_owned);
    puts("Phoenix core tests passed.");
    return 0;
}
