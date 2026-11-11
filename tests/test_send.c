#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_send.h"
#include "gin_fs.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    struct gin_send_result r = gin_send_plain(NULL, NULL, NULL, NULL, 73, NULL, 0);
    assert(r.request_id == 73 && r.code == GIN_SEND_INVALID);
    assert(r.outcome == GIN_SEND_NOT_ATTEMPTED && !r.auth_attempted && !r.post_attempted);
    assert(r.message_id == 0 && r.http_status == 0);
    assert(r.protocol == GIN_PROTOCOL_INVALID && r.auth == GIN_AUTH_INVALID);
    assert(strcmp(gin_send_error(GIN_SEND_INVALID), "invalid") == 0);
    {
        struct gin_root root = GIN_ROOT_INITIALIZER;
        struct gin_send_settings s = {{"https://127.0.0.1:1", NULL, 100, 200}, {4, 4}};
        struct gin_send_route route = {"Net", "#ROOM", "#", GIN_SEND_ROLE_CHANNEL, GIN_SEND_MODE_PLAIN};
        struct gin_credential c;
        memset(&c, 0, sizeof(c));
        strcpy(c.origin, s.auth.origin); strcpy(c.token, "aa~");
        strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
        r = gin_send_plain(&root, &s, &c, &route, 74, (const unsigned char *)"hello", 5);
        assert(r.code == GIN_SEND_INVALID && !r.auth_attempted && !r.post_attempted && r.request_id == 74);
        route.mode = GIN_SEND_MODE_ENCRYPTED;
        r = gin_send_plain(&root, &s, &c, &route, 75, (const unsigned char *)"hello", 5);
        assert(r.code == GIN_SEND_UNSUPPORTED && !r.auth_attempted && !r.post_attempted && r.request_id == 75);
        route.mode = GIN_SEND_MODE_PLAIN;
        r = gin_send_plain(&root, &s, &c, &route, 76, (const unsigned char *)"\300\257", 2);
        assert(r.code == GIN_SEND_INVALID && r.outcome == GIN_SEND_NOT_ATTEMPTED);
        gin_credential_clear(&c);
    }
    return 0;
}
