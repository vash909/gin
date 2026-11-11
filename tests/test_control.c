#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_control.h"
#include "gin_fs.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void local(struct gin_root *root, const struct gin_send_settings *s,
    const struct gin_credential *c, const struct gin_send_route *route,
    uint64_t id, enum gin_control_code expected)
{
    unsigned i;
    for (i = 0; i < 2; i++) {
        struct gin_control_result r = (i ? gin_control_part : gin_control_join)(root, s, c, route, id);
        assert(r.code == expected && r.operation_id == id);
        assert(r.protocol == GIN_PROTOCOL_INVALID && r.auth == GIN_AUTH_INVALID);
        assert(r.outcome == GIN_CONTROL_NOT_ATTEMPTED && !r.attempted && !r.http_status);
    }
}
int main(void)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_send_settings s = {{"https://127.0.0.1:1", NULL, 5000, 10000}, {4, 1}};
    struct gin_credential c = {0};
    struct gin_send_route route = {"Net", "#ROOM", "#", GIN_SEND_ROLE_CHANNEL, GIN_SEND_MODE_ENCRYPTED};
    const char *bad[] = {"", "Nick", "#a,b", "#a b", "#a\r", "#\xc0\xaf", "#\xed\xa0\x80", "+Room", "$server",
        "#xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
    size_t i;
    local(NULL, NULL, NULL, NULL, 73, GIN_CONTROL_INVALID);
    strcpy(c.origin, s.auth.origin); strcpy(c.token, "aa~");
    strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    local(NULL, &s, &c, &route, 74, GIN_CONTROL_UNSUPPORTED);
    route.mode = GIN_SEND_MODE_UNKNOWN;
    local(NULL, &s, &c, &route, 75, GIN_CONTROL_UNSUPPORTED);
    route.mode = (enum gin_send_mode)99;
    local(NULL, &s, &c, &route, 76, GIN_CONTROL_INVALID);
    route.mode = GIN_SEND_MODE_PLAIN;
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++) {
        route.wire_target = bad[i]; local(&root, &s, &c, &route, 100 + i, GIN_CONTROL_INVALID);
    }
    route.wire_target = "#ROOM";
    route.role = GIN_SEND_ROLE_QUERY; local(&root, &s, &c, &route, 200, GIN_CONTROL_INVALID);
    route.role = GIN_SEND_ROLE_UNKNOWN; local(&root, &s, &c, &route, 201, GIN_CONTROL_INVALID);
    route.role = (enum gin_send_role)99; local(&root, &s, &c, &route, 202, GIN_CONTROL_INVALID);
    route.role = GIN_SEND_ROLE_CHANNEL;
    route.chantypes = "##"; local(&root, &s, &c, &route, 203, GIN_CONTROL_INVALID);
    route.chantypes = "#"; route.network_slug = "Net/evil";
    local(&root, &s, &c, &route, 204, GIN_CONTROL_INVALID);
    route.network_slug = "Net";
    local(&root, &s, &c, &route, 0, GIN_CONTROL_INVALID);
    local(&root, &s, &c, &route, UINT64_MAX, GIN_CONTROL_INVALID);
    local(&root, &s, NULL, &route, 205, GIN_CONTROL_INVALID);
    for (i = 0; i <= GIN_CONTROL_MALFORMED; i++) assert(strcmp(gin_control_error((enum gin_control_code)i), "unknown-control-error"));
    assert(!strcmp(gin_control_error((enum gin_control_code)99), "unknown-control-error"));
    puts("control local contract passed");
    return 0;
}
