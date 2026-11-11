#ifdef NDEBUG
#undef NDEBUG
#endif
#include "send_values.h"
#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <jansson.h>

static size_t allocation_calls;
static size_t allocation_failure;

static void *reply_fault_allocate(size_t size)
{
    allocation_calls++;
    if (allocation_failure && allocation_calls == allocation_failure) return NULL;
    return malloc(size);
}

static void allocation_case(long status, const char *body, const char *network, int should_accept)
{
    json_malloc_t old_allocate;
    json_free_t old_free;
    size_t total = 0, position;
    json_get_alloc_funcs(&old_allocate, &old_free);
    for (position = 0; position <= total; position++) {
        int64_t id = 771;
        enum gin_send_outcome outcome = GIN_SEND_NOT_ATTEMPTED;
        enum gin_send_code code;
        allocation_calls = 0;
        allocation_failure = position;
        json_set_alloc_funcs(reply_fault_allocate, free);
        code = gin_send_reply_parse(status, (const unsigned char *)body,
            strlen(body), network, &id, &outcome);
        json_set_alloc_funcs(old_allocate, old_free);
        if (position == 0) total = allocation_calls;
        if (code == GIN_SEND_OK) {
            assert(should_accept);
            assert(id == (status == 201 ? INT64_MAX : 0));
            assert(outcome == (status == 201 ? GIN_SEND_ACCEPTED_PERSISTED : GIN_SEND_ACCEPTED_NO_PERSIST));
        } else {
            assert(code == GIN_SEND_MALFORMED || code == GIN_SEND_NOMEM);
            assert(id == 771 && outcome == GIN_SEND_NOT_ATTEMPTED);
        }
    }
}

int main(void)
{
    const unsigned char reply[] = "{\"id\":9223372036854775807,\"network\":\"Net\",\"extra\":{\"kept\":true}}";
    int64_t id = 71;
    enum gin_send_outcome outcome = GIN_SEND_UNCERTAIN;
    assert(gin_send_reply_parse(201, reply, sizeof(reply)-1, "Net", &id, &outcome) == GIN_SEND_OK);
    assert(id == INT64_MAX && outcome == GIN_SEND_ACCEPTED_PERSISTED);
    {
        const unsigned char accepted[] = "{\"ok\":true,\"extra\":1}";
        const char *bad[] = {
            "[]", "{}", "{\"id\":true,\"network\":\"Net\"}",
            "{\"id\":1.0,\"network\":\"Net\"}",
            "{\"id\":0,\"network\":\"Net\"}",
            "{\"id\":1,\"network\":\"net\"}",
            "{\"id\":1,\"network\":\"Net\",\"id\":2}",
            "{\"id\":1,\"network\":\"Net\",\"\\u0069d\":2}",
            "{\"id\":1,\"network\":\"Net\",\"extra\":\"\\u0000\"}",
            "{\"id\":9223372036854775808,\"network\":\"Net\"}",
            "{\"id\":1,\"network\":\"Net\"} true"
        };
        size_t i;
        assert(gin_send_reply_parse(202, accepted, sizeof(accepted)-1,
            "Net", &id, &outcome) == GIN_SEND_OK);
        assert(id == 0 && outcome == GIN_SEND_ACCEPTED_NO_PERSIST);
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
            id = 71;
            outcome = GIN_SEND_NOT_ATTEMPTED;
            assert(gin_send_reply_parse(201, (const unsigned char *)bad[i],
                strlen(bad[i]), "Net", &id, &outcome) == GIN_SEND_MALFORMED);
            assert(id == 71 && outcome == GIN_SEND_NOT_ATTEMPTED);
        }
        assert(gin_send_reply_parse(204, accepted, sizeof(accepted)-1,
            "Net", &id, &outcome) == GIN_SEND_MALFORMED);
        assert(gin_send_reply_parse(202, (const unsigned char *)"{\"ok\":1}", 8,
            "Net", &id, &outcome) == GIN_SEND_MALFORMED);
        assert(id == 71 && outcome == GIN_SEND_NOT_ATTEMPTED);
    }
    {
        const unsigned char non_slug[] = "{\"id\":1,\"network\":\"café\"}";
        id = 71;
        outcome = GIN_SEND_NOT_ATTEMPTED;
        assert(gin_send_reply_parse(201, non_slug, sizeof(non_slug)-1,
            "café", &id, &outcome) == GIN_SEND_INVALID);
        assert(id == 71 && outcome == GIN_SEND_NOT_ATTEMPTED);
    }
    allocation_case(201, "{\"id\":9223372036854775807,\"network\":\"Net\",\"extra\":{\"x\":\"value\"}}", "Net", 1);
    allocation_case(201, "{\"extra\":{\"id\":3,\"network\":\"other\"},\"network\":\"N\\u0065t\",\"\\u0069d\":9223372036854775807}", "Net", 1);
    allocation_case(202, "{\"extra\":[{\"ok\":false,\"text\":\"long metadata to exercise allocation paths\"}],\"o\\u006b\":true}", "Net", 1);
    allocation_case(202, "{\"extra\":[{\"ok\":true}],\"okay\":true}", "Net", 0);
    {
        char source_network[36], expected_network[35], body[256];
        int written;
        memcpy(source_network, "Net", 3);
        memset(source_network + 3, 'x', sizeof(source_network)-4);
        source_network[sizeof(source_network)-1] = '\0';
        memcpy(expected_network, source_network, sizeof(expected_network)-1);
        expected_network[sizeof(expected_network)-1] = '\0';
        written = snprintf(body, sizeof(body),
            "{\"id\":9223372036854775807,\"network\":\"%s\"}", source_network);
        assert(written > 0 && (size_t)written < sizeof(body));
        allocation_case(201, body, expected_network, 0);
    }
    return 0;
}
