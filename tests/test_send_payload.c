#ifdef NDEBUG
#undef NDEBUG
#endif
#include "send_values.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>

int main(void)
{
    const struct gin_send_route route = {
        "Net", "#Grappa", "#", GIN_SEND_ROLE_CHANNEL, GIN_SEND_MODE_PLAIN
    };
    const unsigned char body[] = "/hello \"world\" \\ caf\303\251";
    char *path = NULL;
    unsigned char *json = NULL;
    size_t length = 0;
    json_t *value, *text;
    json_error_t error = {0};
    assert(gin_send_payload_build(&route, body, sizeof(body)-1,
        &path, &json, &length) == GIN_SEND_OK);
    assert(path != NULL && strcmp(path, "/networks/Net/channels/%23Grappa/messages") == 0);
    assert(json != NULL && length > 0 && length <= GIN_SEND_JSON_MAX);
    value = json_loadb((const char *)json, length, JSON_REJECT_DUPLICATES, &error);
    assert(json_is_object(value) && json_object_size(value) == 1);
    text = json_object_get(value, "body");
    assert(json_is_string(text) && json_string_length(text) == sizeof(body)-1);
    assert(memcmp(json_string_value(text), body, sizeof(body)-1) == 0);
    json_decref(value);
    free(path);
    free(json);
    path = NULL;
    json = NULL;
    length = 99;
    {
        struct gin_send_route refused = route;
        const unsigned char invalid[][4] = {
            {0, 0, 0, 0}, {13, 0, 0, 0}, {10, 0, 0, 0},
            {1, 0, 0, 0}, {127, 0, 0, 0}, {0xc0, 0x80, 0, 0},
            {0xed, 0xa0, 0x80, 0}, {0xf4, 0x90, 0x80, 0x80}
        };
        const size_t sizes[] = {1, 1, 1, 1, 1, 2, 3, 4};
        size_t i;
        for (i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++) {
            assert(gin_send_payload_build(&route, invalid[i], sizes[i],
                &path, &json, &length) == GIN_SEND_INVALID);
            assert(path == NULL && json == NULL && length == 99);
        }
        refused.mode = GIN_SEND_MODE_UNKNOWN;
        assert(gin_send_payload_build(&refused, body, sizeof(body)-1,
            &path, &json, &length) == GIN_SEND_UNSUPPORTED);
        refused.mode = GIN_SEND_MODE_ENCRYPTED;
        assert(gin_send_payload_build(&refused, body, sizeof(body)-1,
            &path, &json, &length) == GIN_SEND_UNSUPPORTED);
        refused.mode = GIN_SEND_MODE_PLAIN;
        refused.role = GIN_SEND_ROLE_QUERY;
        assert(gin_send_payload_build(&refused, body, sizeof(body)-1,
            &path, &json, &length) == GIN_SEND_INVALID);
        refused.wire_target = "$SERVER";
        assert(gin_send_payload_build(&refused, body, sizeof(body)-1,
            &path, &json, &length) == GIN_SEND_INVALID);
        assert(path == NULL && json == NULL && length == 99);
    }
    {
        struct gin_send_route query = route;
        query.role = GIN_SEND_ROLE_QUERY;
        query.wire_target = "Bob";
        assert(gin_send_payload_build(&query, body, sizeof(body)-1,
            &path, &json, &length) == GIN_SEND_OK);
        assert(strcmp(path, "/networks/Net/channels/Bob/messages") == 0);
        free(path);
        free(json);
        path = NULL;
        json = NULL;
        length = 99;
    }
    {
        unsigned char large[GIN_SEND_BODY_MAX + 1];
        memset(large, 'x', sizeof(large));
        assert(gin_send_payload_build(&route, large, GIN_SEND_BODY_MAX,
            &path, &json, &length) == GIN_SEND_OK);
        free(path);
        free(json);
        path = NULL;
        json = NULL;
        length = 99;
        assert(gin_send_payload_build(&route, large, sizeof(large),
            &path, &json, &length) == GIN_SEND_LIMIT);
        assert(path == NULL && json == NULL && length == 99);
    }
    return 0;
}
