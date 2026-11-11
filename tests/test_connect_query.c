/* Private translation-unit fault injection; native WSS is tested separately. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <curl/curl.h>
#include <string.h>
static size_t transmitted;
static unsigned sends;
static int hold_suffix;
static unsigned char wire[2048];
static CURLcode send_piece(CURL *c, const void *p, size_t n, size_t *sent, curl_off_t total, unsigned flags)
{
    size_t take = n < 3 ? n : 3;
    (void)c; assert(total == 0 && flags == CURLWS_TEXT);
    if ((hold_suffix && transmitted) || ++sends % 2 == 0) { *sent = 0; return CURLE_AGAIN; }
    assert(transmitted + take <= sizeof(wire));
    memcpy(wire + transmitted, p, take); transmitted += take; *sent = take;
    return CURLE_OK;
}
static CURLcode recv_again(CURL *c, void *p, size_t n, size_t *got, const struct curl_ws_frame **meta)
{ (void)c; (void)p; (void)n; (void)meta; *got = 0; return CURLE_AGAIN; }
#define curl_ws_send send_piece
#define curl_ws_recv recv_again
#include "../src/connect.c"
#undef curl_ws_send
#undef curl_ws_recv
#ifdef GIN_QUERY_BOOTSTRAP
enum gin_query_code gin_connect_open_query(struct gin_connect *s, const struct gin_topology *t,
    const char *slug, const char *nick, const char *ct, enum gin_query_mode mode, uint64_t id, unsigned ms)
{ (void)s; (void)t; (void)slug; (void)nick; (void)ct; (void)mode; (void)id; (void)ms; return GIN_QUERY_INVALID; }
struct gin_query_result gin_connect_query_result(const struct gin_connect *s)
{ struct gin_query_result r = {0}; (void)s; return r; }
int gin_connect_query_take(struct gin_connect *s, struct gin_query_result *out)
{ (void)s; (void)out; return 0; }
#endif
int main(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,1}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    struct gin_connect *s = NULL;
    struct gin_query_result r;
    assert(gin_connect_query_result(NULL).outcome == GIN_QUERY_NONE);
    assert(gin_connect_query_take(NULL, &r) == -1);
    assert(gin_connect_new(&settings, &c, NULL, 0, NULL, NULL, &s) == GIN_CONNECT_OK);
    assert(gin_connect_open_query(s, NULL, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 1, 100) == GIN_QUERY_UNAVAILABLE);
    r = gin_connect_query_result(s); assert(r.outcome == GIN_QUERY_NONE);
    {
        const char boot[] = "{\"networks\":[{\"id\":7,\"slug\":\"Net\",\"nick\":\"Self\"}],\"channels\":{\"Net\":[]},\"heads\":{}}";
        struct gin_topology *t = NULL;
        struct gin_phoenix_frame *f = NULL;
        const struct gin_phoenix_fields *fields;
        assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &t) == GIN_TOPOLOGY_OK);
        s->state = GIN_CONNECT_READY;
        s->config_protocol = 7; s->root_protocol = 7; s->result.socket_epoch = 1;
        strcpy(s->topics[0].ref, "1"); s->refs.current = 1;
        s->topics[0].status.join_ref = s->topics[0].ref;
        s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 1, 1000) == GIN_QUERY_OK);
        assert(gin_phoenix_decode(s->outbound.data, s->outbound.length, &f) == GIN_PHOENIX_OK);
        fields = gin_phoenix_frame_fields(f);
        assert(!strcmp(fields->event, "open_query_window") && !strcmp(fields->join_ref, "1") && !strcmp(fields->ref, "2"));
        assert(fields->payload.length == strlen("{\"network_id\":7,\"target_nick\":\"Bob\"}"));
        assert(!memcmp(fields->payload.data, "{\"network_id\":7,\"target_nick\":\"Bob\"}", fields->payload.length));
        gin_phoenix_frame_free(f);
        assert(gin_connect_query_result(s).outcome == GIN_QUERY_PENDING);
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 2, 1000) == GIN_QUERY_BUSY);
        assert(s->refs.current == 2);
        {
            char reply[GIN_WS_TOPIC_CAP + 256];
            size_t length = s->outbound.length;
            unsigned char expected[2048];
            memcpy(expected, s->outbound.data, length);
            while (s->outbound.data) assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
            assert(transmitted == length && !memcmp(wire, expected, length));
            snprintf(reply, sizeof(reply), "[\"1\",\"2\",\"%s\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]", s->topics[0].text);
            f = NULL; assert(gin_phoenix_decode((const unsigned char *)reply, strlen(reply), &f) == GIN_PHOENIX_OK);
            s->result.socket_epoch = 2;
            assert(receive_frame(s, f) == GIN_CONNECT_OK && gin_connect_query_result(s).outcome == GIN_QUERY_PENDING);
            s->result.socket_epoch = 1; strcpy(s->topics[0].ref, "9");
            assert(receive_frame(s, f) == GIN_CONNECT_OK && gin_connect_query_result(s).outcome == GIN_QUERY_PENDING);
            strcpy(s->topics[0].ref, "1");
            assert(receive_frame(s, f) == GIN_CONNECT_OK); gin_phoenix_frame_free(f);
            assert(gin_connect_query_result(s).outcome == GIN_QUERY_ACCEPTED);
            assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 2, 1000) == GIN_QUERY_BUSY);
            assert(gin_connect_query_take(s, &r) == 1 && r.operation_id == 1 && !strcmp(r.request_ref, "2"));
            assert(gin_connect_query_take(s, &r) == 0 && r.operation_id == 1);
            assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 2, 1000) == GIN_QUERY_OK);
            gin_connect_close(s);
            assert(gin_connect_query_result(s).outcome == GIN_QUERY_NOT_SENT);
            assert(gin_connect_start(s) == GIN_CONNECT_INVALID);
            assert(gin_connect_query_take(s, &r) == 1 && r.code == GIN_QUERY_CLOSED);
        }
        s->state = GIN_CONNECT_READY; s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
        strcpy(s->topics[0].ref, "1"); s->topics[0].status.join_ref = s->topics[0].ref;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_UNKNOWN, 3, 100) == GIN_QUERY_UNSUPPORTED);
        s->config_protocol = 3;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 100) == GIN_QUERY_UNSUPPORTED);
        s->config_protocol = 7; s->root_protocol = 3;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 100) == GIN_QUERY_UNSUPPORTED);
        s->root_protocol = 7;
        assert(gin_connect_open_query(s, t, "7", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 100) == GIN_QUERY_INVALID);
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 0) == GIN_QUERY_INVALID);
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 60001) == GIN_QUERY_INVALID);
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 3, 1000) == GIN_QUERY_OK);
        s->query_deadline.tv_sec = 0; s->query_deadline.tv_nsec = 0;
        assert(gin_connect_service(s, 0) == GIN_CONNECT_TIMEOUT);
        assert(gin_connect_query_take(s, &r) == 1 && r.outcome == GIN_QUERY_NOT_SENT && r.code == GIN_QUERY_TIMEOUT);
        s->state = GIN_CONNECT_READY; s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
        strcpy(s->topics[0].ref, "1"); s->topics[0].status.join_ref = s->topics[0].ref;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 4, 1000) == GIN_QUERY_OK);
        {
            const char early[] = "[\"1\",\"5\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]";
            f = NULL;
            /* Force a genuine partial write, then prove early ACK cannot accept. */
            hold_suffix = 1; sends = 0; transmitted = 0;
            assert(gin_connect_service(s, 0) == GIN_CONNECT_OK && s->offset == 3 && transmitted == 3);
            assert(gin_phoenix_decode((const unsigned char *)early, strlen(early), &f) == GIN_PHOENIX_OK);
            assert(receive_frame(s, f) == GIN_CONNECT_MALFORMED);
            gin_phoenix_frame_free(f);
            assert(gin_connect_query_take(s, &r) == 1 && r.outcome == GIN_QUERY_UNCERTAIN);
        }
        s->state = GIN_CONNECT_READY; s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
        strcpy(s->topics[0].ref, "1"); s->topics[0].status.join_ref = s->topics[0].ref;
        s->refs.current = UINT64_MAX;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 5, 1000) == GIN_QUERY_LIMIT);
        s->refs.current = 5; hold_suffix = 1; sends = 0; transmitted = 0;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 5, 1000) == GIN_QUERY_OK);
        assert(gin_connect_service(s, 0) == GIN_CONNECT_OK && s->offset == 3);
        s->query_deadline.tv_sec = 0; s->query_deadline.tv_nsec = 0;
        assert(gin_connect_service(s, 0) == GIN_CONNECT_TIMEOUT);
        assert(gin_connect_query_take(s, &r) == 1 && r.outcome == GIN_QUERY_UNCERTAIN && r.code == GIN_QUERY_TIMEOUT);
        s->state = GIN_CONNECT_READY; s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
        strcpy(s->topics[0].ref, "1"); s->topics[0].status.join_ref = s->topics[0].ref;
        {
            const char *bad[] = {"", "$server", ":Bob", "B.ob", "B,ob", "B!ob", "B@ob", "B*ob", "B?ob", "B#ob", "B&ob", "B+ob", "B ob", "B\nob", "\xc0\xaf"};
            size_t i;
            for (i = 0; i < sizeof(bad)/sizeof(bad[0]); i++)
                assert(gin_connect_open_query(s, t, "Net", bad[i], "#", GIN_QUERY_MODE_PLAIN, 6, 1000) == GIN_QUERY_INVALID);
        }
        hold_suffix = 0; sends = 0; transmitted = 0;
        assert(gin_connect_open_query(s, t, "Net", "Bob", "#", GIN_QUERY_MODE_PLAIN, 6, 1000) == GIN_QUERY_OK);
        while (s->outbound.data) assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
        s->query_deadline.tv_sec = 0; s->query_deadline.tv_nsec = 0;
        {
            const char late[] = "[\"1\",\"7\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]";
            f = NULL;
            assert(gin_phoenix_decode((const unsigned char *)late, strlen(late), &f) == GIN_PHOENIX_OK);
            assert(receive_frame(s, f) == GIN_CONNECT_TIMEOUT);
            gin_phoenix_frame_free(f);
            assert(gin_connect_query_take(s, &r) == 1 && r.outcome == GIN_QUERY_UNCERTAIN && r.code == GIN_QUERY_TIMEOUT);
        }
        gin_topology_free(t);
    }
    gin_connect_free(s);
    return 0;
}
