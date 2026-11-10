/* White-box arithmetic boundaries complement real socket integration tests. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "../src/http.c"

int main(void)
{
    unsigned char body[4] = { 1, 2, 3, 4 };
    char byte = 'x';
    struct transfer t;
    struct gin_http_result r;
    struct gin_http_settings s = { "https://127.0.0.1/", NULL, 1, 1, 4, 0 };
    assert(gin_http_global_init() == GIN_HTTP_OK);
    r = gin_http_get(NULL, body, sizeof(body));
    assert(r.code == GIN_HTTP_INVALID && r.body_length == 0);
    r = gin_http_get(&s, NULL, sizeof(body));
    assert(r.code == GIN_HTTP_INVALID);
    s.url = NULL;
    r = gin_http_get(&s, body, sizeof(body));
    assert(r.code == GIN_HTTP_INVALID);
    memset(&t, 0, sizeof(t));
    t.body = body;
    t.capacity = sizeof(body);
    t.header_limit = sizeof(body);
    assert(receive_body(&byte, SIZE_MAX, 2, &t) == 0);
    assert(t.limited && t.length == 0 && body[0] == 1);
    t.limited = 0;
    assert(receive_header(&byte, SIZE_MAX, 2, &t) == 0);
    assert(t.limited && t.headers == 0);
    t.limited = 0;
    t.length = sizeof(body);
    assert(receive_body(&byte, 1, 1, &t) == 0 && t.limited);
    t.limited = 0;
    t.headers = sizeof(body);
    assert(receive_header(&byte, 1, 1, &t) == 0 && t.limited);
    assert(body[0] == 1 && body[3] == 4);
    gin_http_global_cleanup();
    return 0;
}
