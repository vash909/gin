/* Pure protocol-policy qualification; no network, credentials or live server. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_protocol.h"
#include <assert.h>
#include <jansson.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

static struct gin_protocol_info verify_raw(const struct gin_protocol_policy *p,
    const unsigned char *body, size_t n, enum gin_protocol_code expected)
{
    struct gin_protocol_info out, zero;
    memset(&out, 0xa5, sizeof(out));
    memset(&zero, 0, sizeof(zero));
    assert(gin_protocol_parse(p, body, n, &out) == expected);
    if (expected != GIN_PROTOCOL_OK) assert(!memcmp(&out, &zero, sizeof(out)));
    checks++;
    return out;
}

static struct gin_protocol_info verify(const struct gin_protocol_policy *p,
    const char *body, enum gin_protocol_code expected)
{
    return verify_raw(p, (const unsigned char *)body, strlen(body), expected);
}

int main(void)
{
    static const struct {
        int client, required, server, minimum;
        enum gin_protocol_code expected;
    } matrix[] = {
        {4, 4, 4, 1, GIN_PROTOCOL_OK},
        {4, 4, 63, 1, GIN_PROTOCOL_OK},
        {63, 4, 4, 1, GIN_PROTOCOL_OK},
        {1, 4, 4, 1, GIN_PROTOCOL_OK},
        {3, 4, 63, 4, GIN_PROTOCOL_CLIENT_TOO_OLD},
        {99, 4, 3, 1, GIN_PROTOCOL_SERVER_TOO_OLD},
        {1, 6, 4, 3, GIN_PROTOCOL_CLIENT_TOO_OLD},
        {1, 6, 4, 1, GIN_PROTOCOL_SERVER_TOO_OLD},
        {0, 4, 63, 1, GIN_PROTOCOL_INVALID},
        {4, 0, 63, 1, GIN_PROTOCOL_INVALID},
        {-1, 4, 63, 1, GIN_PROTOCOL_INVALID},
        {4, 4, 0, 1, GIN_PROTOCOL_MALFORMED},
        {4, 4, 4, 0, GIN_PROTOCOL_MALFORMED},
        {4, 4, 4, 5, GIN_PROTOCOL_MALFORMED},
        {INT_MAX, 1, 1, 1, GIN_PROTOCOL_OK}
    };
    const char *bad[] = {
        "", "{}", "[]", "null", "{", "{} {}",
        "{\"server\":\"grappa\",\"protocol_version\":true,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":4.0,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":\"4\",\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":false}",
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":null}",
        "{\"server\":\"grappa\",\"protocol_version\":-1,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":4,\"protocol_version\":5,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":4,\"protocol_vers\\u0069on\":5,\"min_protocol_version\":1}",
        "{\"server\":42,\"protocol_version\":4,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\\u0000evil\",\"protocol_version\":4,\"min_protocol_version\":1}",
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"ignored\":\"\xff\"}"
    };
    const char *base = "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1}";
    struct gin_protocol_policy policy = {4, 4};
    struct gin_protocol_info info;
    char buffer[512];
    size_t i;
    for (i = 0; i < sizeof(matrix) / sizeof(matrix[0]); i++) {
        struct gin_protocol_policy p = {matrix[i].client, matrix[i].required};
        (void)snprintf(buffer, sizeof(buffer),
            "{\"server\":\"grappa\",\"protocol_version\":%d,\"min_protocol_version\":%d}",
            matrix[i].server, matrix[i].minimum);
        info = verify(&p, buffer, matrix[i].expected);
        if (matrix[i].expected == GIN_PROTOCOL_OK) {
            assert(info.server_protocol == matrix[i].server);
            assert(info.minimum_client_protocol == matrix[i].minimum);
            assert(info.software_version[0] == 0);
        }
    }
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
        (void)verify(&policy, bad[i], GIN_PROTOCOL_MALFORMED);
    (void)verify(&policy,
        "{\"server\":\"other\",\"protocol_version\":4,\"min_protocol_version\":1}",
        GIN_PROTOCOL_WRONG_SERVER);
    (void)snprintf(buffer, sizeof(buffer),
        "{\"server\":\"grappa\",\"protocol_version\":%llu,\"min_protocol_version\":1}",
        (unsigned long long)INT_MAX + 1ULL);
    (void)verify(&policy, buffer, GIN_PROTOCOL_MALFORMED);
    info = verify(&policy,
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"version\":\"999.0-future\",\"vsn\":\"999\",\"push_content_encoding\":\"not-e2ee\"}",
        GIN_PROTOCOL_OK);
    assert(!strcmp(info.software_version, "999.0-future"));
    info = verify(&policy,
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"version\":false,\"unknown\":{\"nested\":[true,null]}}",
        GIN_PROTOCOL_OK);
    assert(info.software_version[0] == 0);
    info = verify(&policy,
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"version\":\"unsafe\\u001b\\u0085\"}",
        GIN_PROTOCOL_OK);
    assert(info.software_version[0] == 0);
    (void)verify_raw(NULL, (const unsigned char *)base, strlen(base), GIN_PROTOCOL_INVALID);
    (void)verify_raw(&policy, NULL, 0, GIN_PROTOCOL_INVALID);
    assert(gin_protocol_parse(&policy, (const unsigned char *)base, strlen(base), NULL) == GIN_PROTOCOL_INVALID);
    for (i = 31; i <= 32; i++) {
        size_t j;
        json_t *nested = json_string("brackets [{}] quotes \" backslash \\");
        json_t *root;
        char *data;
        assert(nested != NULL);
        for (j = 0; j < i; j++) {
            json_t *array = json_array();
            assert(array && json_array_append_new(array, nested) == 0);
            nested = array;
        }
        root = json_pack("{s:s,s:i,s:i,s:o}", "server", "grappa",
            "protocol_version", 4, "min_protocol_version", 1, "ignored", nested);
        assert(root);
        data = json_dumps(root, JSON_COMPACT);
        assert(data);
        (void)verify(&policy, data, i == 31 ? GIN_PROTOCOL_OK : GIN_PROTOCOL_MALFORMED);
        free(data); json_decref(root);
    }
    for (i = GIN_PROTOCOL_BODY_MAX; i <= GIN_PROTOCOL_BODY_MAX + 1; i++) {
        const char *prefix = "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"ignored\":\"";
        const char *suffix = "\"}";
        size_t a = strlen(prefix), b = strlen(suffix);
        char *data = malloc(i + 1);
        assert(data && i > a + b);
        memcpy(data, prefix, a); memset(data + a, 'x', i - a - b);
        memcpy(data + i - b, suffix, b); data[i] = 0;
        (void)verify_raw(&policy, (const unsigned char *)data, i,
            i == GIN_PROTOCOL_BODY_MAX ? GIN_PROTOCOL_OK : GIN_PROTOCOL_LIMIT);
        free(data);
    }
    {
        unsigned char raw[512];
        size_t base_len = strlen(base);
        const char junk[] = "junk";
        size_t junk_len = sizeof(junk) - 1;
        assert(base_len + 1 + junk_len < sizeof(raw));
        memcpy(raw, base, base_len);
        raw[base_len] = '\0';
        memcpy(raw + base_len + 1, junk, junk_len);
        (void)verify_raw(&policy, raw, base_len + 1 + junk_len,
            GIN_PROTOCOL_MALFORMED);
    }
    (void)verify_raw(&policy, (const unsigned char *)base, strlen(base) - 1,
        GIN_PROTOCOL_MALFORMED);
    for (i = GIN_PROTOCOL_SOFTWARE_MAX; i <= GIN_PROTOCOL_SOFTWARE_MAX + 1; i++) {
        char version[GIN_PROTOCOL_SOFTWARE_MAX + 2];
        json_t *root;
        char *data;
        assert(i < sizeof(version));
        memset(version, 'v', i);
        version[i] = '\0';
        root = json_pack("{s:s,s:i,s:i,s:s}", "server", "grappa",
            "protocol_version", 4, "min_protocol_version", 1,
            "version", version);
        assert(root);
        data = json_dumps(root, JSON_COMPACT);
        assert(data);
        info = verify(&policy, data, GIN_PROTOCOL_OK);
        if (i == GIN_PROTOCOL_SOFTWARE_MAX)
            assert(strlen(info.software_version) == GIN_PROTOCOL_SOFTWARE_MAX);
        else
            assert(info.software_version[0] == 0);
        free(data);
        json_decref(root);
    }
    info = verify(&policy,
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"version\":\"x\\u001b\"}",
        GIN_PROTOCOL_OK);
    assert(info.software_version[0] == 0);
    info = verify(&policy,
        "{\"server\":\"grappa\",\"protocol_version\":4,\"min_protocol_version\":1,\"version\":\"x\\u0085\"}",
        GIN_PROTOCOL_OK);
    assert(info.software_version[0] == 0);
    info = verify(&policy, base, GIN_PROTOCOL_OK);
    assert(gin_protocol_server_at_least(&info, 4));
    assert(!gin_protocol_server_at_least(&info, 5));
    assert(!gin_protocol_server_at_least(&info, 0));
    assert(!gin_protocol_server_at_least(NULL, 1));
    info.minimum_client_protocol = 5;
    assert(!gin_protocol_server_at_least(&info, 1));
    assert(!strcmp(gin_protocol_error(GIN_PROTOCOL_CLIENT_TOO_OLD), "client-protocol-too-old"));
    assert(!strcmp(gin_protocol_error((enum gin_protocol_code)999), "unknown-protocol-error"));
    printf("protocol parser: %u cases passed\n", checks);
    return 0;
}
