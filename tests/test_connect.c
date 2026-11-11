/* Private service fault injection supplements, never replaces, native WSS. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <curl/curl.h>
#include "../include/gin_phoenix.h"
#include <assert.h>
#include <poll.h>
#include <string.h>
static unsigned sends, receives, polls;
static size_t transmitted;
static unsigned char wire[8192];
static CURLcode send_piece(CURL *c, const void *p, size_t n, size_t *sent, curl_off_t total, unsigned flags)
{
    size_t take = n < 3 ? n : 3;
    (void)c; assert(total == 0 && flags == CURLWS_TEXT);
    sends++;
    if (sends % 2 == 0) { *sent = 0; return CURLE_AGAIN; }
    assert(transmitted + take <= sizeof(wire));
    memcpy(wire + transmitted, p, take); transmitted += take; *sent = take;
    return CURLE_OK;
}
static int force_nomem;
static CURLcode receive_again(CURL *c, void *p, size_t n, size_t *got, const struct curl_ws_frame **meta)
{ static struct curl_ws_frame frame; (void)c; receives++; if (force_nomem) { assert(n >= 1); frame.flags = CURLWS_TEXT; frame.bytesleft = 0; ((unsigned char *)p)[0] = 'p'; *got = 1; *meta = &frame; return CURLE_OK; } (void)p; (void)n; (void)meta; *got = 0; return CURLE_AGAIN; }
static int poll_once(struct pollfd *p, nfds_t n, int ms)
{ (void)p; assert(n == 1 && ms >= 0 && ms <= 1000); polls++; return 0; }
static enum gin_phoenix_code feed_wrapper(struct gin_phoenix_stream *stream, const unsigned char *data, size_t length, int final, struct gin_phoenix_frame **out)
{ if (force_nomem) return GIN_PHOENIX_NOMEM; return gin_phoenix_stream_feed(stream, data, length, final, out); }
#define curl_ws_send send_piece
#define curl_ws_recv receive_again
#define poll poll_once
#define gin_phoenix_stream_feed feed_wrapper
#include "../src/connect.c"
#undef gin_phoenix_stream_feed
#undef curl_ws_send
#undef curl_ws_recv
#undef poll
int main(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,4}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    struct gin_connect_subscription plan[] = {{"Net", "#ROOM"}, {"Net", "#room"}};
    struct gin_connect *s = NULL;
    struct gin_phoenix_bytes expected = {NULL,0};
    unsigned previous;
    {
        struct gin_connect_subscription duplicates[65];
        size_t i;
        for (i = 0; i < 65; i++) { duplicates[i].network = "Net"; duplicates[i].target = "#ROOM"; }
        assert(gin_connect_new(&settings, &c, duplicates, 65, NULL, NULL, &s) == GIN_CONNECT_OK);
        assert(gin_connect_topic_count(s) == 3);
        gin_connect_free(s); s = NULL;
    }
    assert(gin_connect_new(&settings, &c, plan, 2, NULL, NULL, &s) == GIN_CONNECT_OK);
    assert(s->count == 3 && s->state == GIN_CONNECT_IDLE);
    assert(gin_connect_service(s, 0) == GIN_CONNECT_INVALID);
    assert(gin_connect_new(&settings, &c, plan, 2, NULL, NULL, &s) == GIN_CONNECT_INVALID);
    c.token[0] = 'z'; assert(s->credential.token[0] == 'a');
    s->state = GIN_CONNECT_JOINING;
    assert(queue_join(s) == GIN_CONNECT_OK);
    expected.data = malloc(s->outbound.length); assert(expected.data);
    expected.length = s->outbound.length; memcpy(expected.data, s->outbound.data, expected.length);
    while (s->outbound.data) {
        previous = sends + receives;
        assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
        assert(sends + receives - previous <= GIN_CONNECT_OPERATIONS_MAX);
        assert(polls == 0);
    }
    assert(transmitted == expected.length && !memcmp(wire, expected.data, expected.length));
    previous = sends + receives;
    assert(gin_connect_service(s, 100) == GIN_CONNECT_OK);
    assert(polls == 1 && sends + receives - previous == 1);
    assert(gin_connect_service(s, 1001) == GIN_CONNECT_INVALID);
    s->deadline.tv_sec = 0; s->deadline.tv_nsec = 0;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_TIMEOUT);
    assert(s->state == GIN_CONNECT_FAILED && !s->topics[0].status.join_ref);
    force_nomem = 1; s->state = GIN_CONNECT_READY;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_NOMEM);
    assert(s->state == GIN_CONNECT_FAILED && s->result.code == GIN_CONNECT_NOMEM && !s->topics[0].status.join_ref && !s->outbound.data);
    force_nomem = 0;
    s->result.socket_epoch = UINT64_MAX;
    assert(gin_connect_start(s) == GIN_CONNECT_LIMIT);
    gin_phoenix_bytes_free(&expected); gin_connect_free(s);
    assert(gin_connect_get_state(NULL) == GIN_CONNECT_STOPPED);
    assert(gin_connect_topic_count(NULL) == 0 && !gin_connect_topic_at(NULL, 0));
    gin_connect_close(NULL); gin_connect_free(NULL);
    return 0;
}
