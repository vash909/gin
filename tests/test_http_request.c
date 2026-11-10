#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_http.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    unsigned char body[32];
    struct gin_http_settings s = {"http://127.0.0.1/", NULL, 100, 200, 1024, 1};
    struct gin_http_request q = {&s, GIN_HTTP_GET, "synthetic-token", "https://127.0.0.1", NULL, 0};
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://127.0.0.1/me";
    q.bearer_origin = "https://other.invalid";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    q.bearer = "synthetic-bearer";
    s.url = "https://example.invalid.attacker.invalid/me";
    q.bearer_origin = "https://example.invalid";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://example.invalid:8443/me";
    q.bearer_origin = "https://example.invalid:9443";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://EXAMPLE.invalid/me";
    q.bearer_origin = "https://example.invalid";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://example.invalid/me";
    q.bearer_origin = "https://example.invalid:443";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://example.invalid:443/me";
    q.bearer_origin = "https://example.invalid";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    s.url = "https://127.0.0.1/me";
    q.bearer_origin = "https://other.invalid";
    q.bearer = "synthetic-token";
    assert(gin_http_origin_valid("https://example.org:443"));
    assert(!gin_http_origin_valid("https://example.org/"));
    assert(!gin_http_origin_valid("https://example.org?x"));
    assert(!gin_http_origin_valid("https://user@example.org"));
    q.bearer_origin = "https://127.0.0.1";
    q.bearer = "bad\r\nHeader: injected";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    q.bearer = "a=b";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    q.bearer = "=";
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    q.bearer = NULL; q.bearer_origin = NULL;
    q.method = GIN_HTTP_POST_JSON; q.json_body = (const unsigned char *)"{}";
    q.json_length = GIN_HTTP_REQUEST_MAX + 1;
    assert(gin_http_request(&q, body, sizeof(body)).code == GIN_HTTP_INVALID);
    gin_http_global_cleanup();
    return 0;
}
