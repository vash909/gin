/* Private TU injection: no production clock, curl or poll hooks. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <curl/curl.h>
#include <time.h>
#include <string.h>
#include <poll.h>
#include <stdarg.h>
#include <errno.h>
#include "gin_liveness.h"
static struct timespec tick = {100, 0};
static int bad_clock;
static int fixture_clock(clockid_t id, struct timespec *out)
{ assert(id == CLOCK_MONOTONIC); if (bad_clock) return -1; *out = tick; return 0; }
static char sent[2048];
static size_t sent_length, send_limit = 2048;
static CURLcode send_code = CURLE_OK;
static int alternating_again, send_calls, overreport;
/* Model libcurl's retained encoder, not just reported payload bytes. */
static int buffered_send, buffered_blocks;
static unsigned char buffered_payload[2048];
static size_t buffered_length, buffered_offset;
static int poll_wait = -1;
static int poll_interrupts;
static unsigned poll_advance_ms;
static short poll_events;
static CURLcode recv_code=CURLE_AGAIN;
static CURLcode fixture_send(CURL *c, const void *p, size_t n, size_t *out,
    curl_off_t total, unsigned flags)
{
    (void)c; (void)total; assert(flags == CURLWS_TEXT);
    send_calls++;
    if (buffered_send) {
        if (!buffered_length) {
            assert(n && n < sizeof(buffered_payload));
            memcpy(buffered_payload, p, n); buffered_length = n; buffered_offset = 0;
        }
        assert(n == buffered_length - buffered_offset);
        assert(!memcmp(p, buffered_payload + buffered_offset, n));
        if (buffered_blocks) { buffered_blocks--; *out = 0; return CURLE_AGAIN; }
    }
    if (overreport) { *out = n + 1; return CURLE_OK; }
    if (alternating_again && send_calls % 2 == 0) { *out = 0; return CURLE_AGAIN; }
    *out = n < send_limit ? n : send_limit;
    assert(sent_length + *out < sizeof(sent));
    memcpy(sent + sent_length, p, *out); sent_length += *out; sent[sent_length] = 0;
    if (buffered_send) {
        buffered_offset += *out;
        if (buffered_offset == buffered_length) buffered_length = buffered_offset = 0;
    }
    return send_code;
}
static CURLcode fixture_recv(CURL *c, void *p, size_t cap, size_t *out,
    const struct curl_ws_frame **meta)
{ (void)c; (void)p; (void)cap; *meta=NULL; *out = 0; return recv_code; }
static int fixture_poll(struct pollfd *p, nfds_t n, int ms)
{
    assert(n == 1); poll_wait = ms; p->revents=poll_events;
    tick.tv_nsec += (long)poll_advance_ms * 1000000;
    if (tick.tv_nsec >= 1000000000) { tick.tv_sec++; tick.tv_nsec -= 1000000000; }
    if (poll_interrupts) { poll_interrupts--; errno = EINTR; return -1; }
    return poll_events ? 1 : 0;
}
static unsigned fetches, identities, upgrades;
static long fetch_rewind_ns;
static long upgrade_status = 403;
static CURLcode upgrade_code = CURLE_OPERATION_TIMEDOUT;
static struct gin_connect *upgrade_session;
static enum gin_protocol_code fixture_fetch(const struct gin_auth_settings *h,
    const struct gin_protocol_policy *p, struct gin_protocol_info *out)
{ (void)h; (void)p; fetches++; tick.tv_nsec-=fetch_rewind_ns; memset(out,0,sizeof(*out)); out->server_protocol=7; return GIN_PROTOCOL_OK; }
static enum gin_auth_code fixture_identity(const struct gin_auth_settings *h,
    const struct gin_credential *c, struct gin_credential *out)
{ (void)h; identities++; *out = *c; return GIN_AUTH_OK; }
static CURLcode fixture_perform(CURL *c);
static CURLcode fixture_getinfo(CURL *c, CURLINFO key, ...)
{
    va_list ap; (void)c; va_start(ap,key);
    if (key == CURLINFO_RESPONSE_CODE) *va_arg(ap,long *) = upgrade_status;
    else if (key == CURLINFO_ACTIVESOCKET) *va_arg(ap,curl_socket_t *) = 123;
    else assert(0);
    va_end(ap); return CURLE_OK;
}
#define clock_gettime fixture_clock
#define curl_ws_send fixture_send
#define curl_ws_recv fixture_recv
#define poll fixture_poll
#define gin_protocol_fetch fixture_fetch
#define gin_auth_identity fixture_identity
#define curl_easy_perform fixture_perform
#undef curl_easy_getinfo
#define curl_easy_getinfo fixture_getinfo
#include "../src/connect.c"
#undef clock_gettime
#undef curl_ws_send
#undef curl_ws_recv
#undef poll
#undef gin_protocol_fetch
#undef gin_auth_identity
#undef curl_easy_perform
#undef curl_easy_getinfo
static CURLcode fixture_perform(CURL *c)
{
    char status[] = "HTTP/1.1 403 Forbidden\r\n", blank[] = "\r\n";
    (void)c; upgrades++;
    if (upgrade_session) {
        header(status,1,strlen(status),upgrade_session);
        header(blank,1,2,upgrade_session);
    }
    return upgrade_code;
}
static struct gin_connect *session(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,1}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    struct gin_connect *s = NULL;
    assert(gin_connect_new(&settings, &c, NULL, 0, NULL, NULL, &s) == GIN_CONNECT_OK);
    return s;
}
static void ready(struct gin_connect *s)
{
    s->state = GIN_CONNECT_READY; s->config_protocol = 7; s->root_protocol = 7;
    s->result.socket_epoch = 1; s->refs.current = 1;
    strcpy(s->topics[0].ref, "1"); s->topics[0].status.join_ref = s->topics[0].ref;
    s->topics[0].status.state = GIN_CONNECT_TOPIC_JOINED;
    assert(heartbeat_arm(s) == GIN_CONNECT_OK);
}
static enum gin_connect_code frame(struct gin_connect *s, const char *text)
{
    struct gin_phoenix_frame *f = NULL;
    enum gin_connect_code c;
    assert(gin_phoenix_decode((const unsigned char *)text, strlen(text), &f) == GIN_PHOENIX_OK);
    c = receive_frame(s, f); gin_phoenix_frame_free(f); return c;
}
static void severance(void)
{
    struct gin_connect *s = session();
    ready(s);
    assert(frame(s, "[null,null,\"grappa:user:Alice\",\"event\",{\"kind\":\"web_session_severed\",\"code\":\"rate_limit_flood\"}]") == GIN_CONNECT_SESSION_EXPIRED);
    assert(gin_connect_start(s) == GIN_CONNECT_INVALID);
    gin_connect_close(s);
    assert(gin_connect_start(s) == GIN_CONNECT_INVALID);
    gin_connect_free(s);
}
static void retry_policy(void)
{
    struct gin_connect *s = session();
    struct gin_liveness_settings policy = {100, 50, 10, 40, 2};
    assert(gin_connect_set_liveness(s, &policy) == GIN_CONNECT_OK);
    assert(gin_connect_set_liveness(s, &policy) == GIN_CONNECT_INVALID);
    ready(s);
    assert(fail(s, GIN_CONNECT_TRANSPORT) == GIN_CONNECT_TRANSPORT);
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_WAITING);
    assert(gin_connect_liveness_status(s).retry_wait_ms == 10);
    assert(gin_connect_retry(s) == GIN_RETRY_NOT_DUE);
    tick.tv_nsec = 10000000;
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_DUE);
    gin_connect_close(s);
    assert(gin_connect_retry(s) == GIN_RETRY_CANCELLED);
    assert(gin_connect_start(s) == GIN_CONNECT_INVALID);
    gin_connect_free(s);
    tick.tv_nsec = 0;
}
static void clock_failure(void)
{
    struct gin_connect *s = session();
    struct gin_liveness_settings p = {100,50,10,40,2};
    assert(gin_connect_set_liveness(s, &p) == GIN_CONNECT_OK);
    ready(s); fail(s, GIN_CONNECT_TRANSPORT);
    bad_clock = 1;
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_CLOCK_ERROR);
    assert(gin_connect_retry(s) == GIN_RETRY_CLOCK_ERROR);
    assert(s->result.code == GIN_CONNECT_CLOCK);
    bad_clock = 0; gin_connect_free(s);
    s = session(); assert(gin_connect_set_liveness(s,&p) == GIN_CONNECT_OK);
    ready(s); fail(s, GIN_CONNECT_TRANSPORT);
    tick.tv_sec--;
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_CLOCK_ERROR);
    assert(gin_connect_retry(s) == GIN_RETRY_CLOCK_ERROR);
    tick.tv_sec++; gin_connect_free(s);
}
static void heartbeat_wire(void)
{
    struct gin_connect *s = session();
    struct gin_liveness_settings p = {100,50,10,40,2};
    assert(gin_connect_set_liveness(s,&p) == GIN_CONNECT_OK); ready(s);
    sent_length = 0;
    assert(gin_connect_service(s,0) == GIN_CONNECT_OK);
    tick.tv_nsec = 100000000;
    assert(gin_connect_service(s,0) == GIN_CONNECT_OK);
    assert(!strcmp(sent,"[null,\"2\",\"phoenix\",\"heartbeat\",{}]"));
    assert(gin_connect_liveness_status(s).heartbeat == GIN_HEARTBEAT_WAITING_REPLY);
    assert(frame(s,"[null,\"9\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]") == GIN_CONNECT_OK);
    assert(gin_connect_liveness_status(s).heartbeat == GIN_HEARTBEAT_WAITING_REPLY);
    assert(frame(s,"[null,\"2\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]") == GIN_CONNECT_OK);
    assert(gin_connect_liveness_status(s).heartbeat == GIN_HEARTBEAT_SCHEDULED);
    tick.tv_nsec = 250000000;
    assert(gin_connect_service(s,0) == GIN_CONNECT_TIMEOUT);
    assert(gin_connect_liveness_status(s).reason == GIN_LIVENESS_REASON_HEARTBEAT_TIMEOUT);
    gin_connect_free(s); tick.tv_nsec = 0;
}
static void failure_classifier(void)
{
    struct gin_liveness_settings p = {100,50,10,40,2};
    struct gin_connect *s = session();
    int code;
    assert(gin_connect_set_liveness(s,&p) == GIN_CONNECT_OK); ready(s);
    s->result.protocol = GIN_PROTOCOL_TIMEOUT;
    fail(s, GIN_CONNECT_PREFLIGHT);
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_WAITING);
    assert(gin_connect_liveness_status(s).reason == GIN_LIVENESS_REASON_PREFLIGHT);
    gin_connect_free(s);
    s = session(); assert(gin_connect_set_liveness(s,&p) == GIN_CONNECT_OK); ready(s);
    s->result.auth_attempted = 1; s->result.auth = GIN_AUTH_TRANSPORT;
    fail(s, GIN_CONNECT_UNAUTHORIZED);
    assert(gin_connect_liveness_status(s).state == GIN_LIVENESS_WAITING);
    gin_connect_free(s);
    s = session(); assert(gin_connect_set_liveness(s,&p) == GIN_CONNECT_OK); ready(s);
    assert(frame(s,"[null,null,\"grappa:user:Alice\",\"phx_error\",{}]") == GIN_CONNECT_CLOSED);
    assert(gin_connect_retry(s) == GIN_RETRY_TERMINAL);
    gin_connect_free(s);
    assert(curl_error(CURLE_OUT_OF_MEMORY) == GIN_CONNECT_NOMEM);
    assert(curl_error(CURLE_BAD_FUNCTION_ARGUMENT) != GIN_CONNECT_TRANSPORT);
    for (code=GIN_PROTOCOL_OK; code<=GIN_PROTOCOL_REMOTE; code++) {
        int transient=code==GIN_PROTOCOL_TIMEOUT || code==GIN_PROTOCOL_TRANSPORT;
        s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
        s->result.protocol=(enum gin_protocol_code)code;
        fail(s,GIN_CONNECT_PREFLIGHT);
        assert(gin_connect_liveness_status(s).state == (transient ? GIN_LIVENESS_WAITING : GIN_LIVENESS_TERMINAL));
        gin_connect_free(s);
    }
    for (code=GIN_AUTH_INVALID; code<=GIN_AUTH_LIMIT; code++) {
        int transient=code==GIN_AUTH_TIMEOUT || code==GIN_AUTH_TRANSPORT;
        s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
        s->result.auth_attempted=1; s->result.auth=(enum gin_auth_code)code;
        fail(s,GIN_CONNECT_UNAUTHORIZED);
        assert(gin_connect_liveness_status(s).state == (transient ? GIN_LIVENESS_WAITING : GIN_LIVENESS_TERMINAL));
        gin_connect_free(s);
    }
}
static void upgrade_precedence(void)
{
    const CURLcode codes[] = {CURLE_OPERATION_TIMEDOUT, CURLE_PEER_FAILED_VERIFICATION,
        CURLE_PARTIAL_FILE, CURLE_OUT_OF_MEMORY, CURLE_HTTP_RETURNED_ERROR};
    const enum gin_connect_code expected[] = {GIN_CONNECT_TIMEOUT, GIN_CONNECT_TLS,
        GIN_CONNECT_TRANSPORT, GIN_CONNECT_NOMEM, GIN_CONNECT_FORBIDDEN};
    size_t i;
    for (i=0; i<sizeof(codes)/sizeof(codes[0]); i++) {
        struct gin_connect *s=session(); upgrade_session=s; upgrade_code=codes[i];
        assert(gin_connect_start(s) == expected[i]);
        gin_connect_free(s);
    }
    upgrade_session=NULL; upgrade_code=CURLE_OPERATION_TIMEDOUT;
}
static struct gin_topology *topology(void)
{
    const char boot[] = "{\"networks\":[{\"id\":7,\"slug\":\"Net\",\"nick\":\"Self\"}],\"channels\":{\"Net\":[]},\"heads\":{}}";
    struct gin_topology *t = NULL;
    assert(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&t) == GIN_TOPOLOGY_OK);
    return t;
}
static void independent_frames(void)
{
    struct gin_connect *s=session();
    struct gin_topology *t=topology();
    struct gin_liveness_settings p={100,50,10,40,2};
    char expected[2048]; size_t query_length;
    struct gin_query_result receipt;
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_OK);
    query_length=s->outbound.length; memcpy(expected,s->outbound.data,query_length);
    sent_length=0; send_limit=1; alternating_again=1; send_calls=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->offset > 0 && s->outbound.data);
    tick.tv_nsec=100000000;
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,2,1000)==GIN_QUERY_BUSY);
    assert(s->refs.current==2);
    while(s->outbound.data) assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    /* Continue only until one byte of heartbeat has been sent. */
    while(s->heartbeat_offset==0) assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->heartbeat==GIN_HEARTBEAT_WRITING);
    assert(frame(s,"[\"1\",\"2\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]")==GIN_CONNECT_OK);
    assert(s->query.outcome==GIN_QUERY_ACCEPTED);
    while(s->heartbeat!=GIN_HEARTBEAT_WAITING_REPLY) assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    strcpy(expected+query_length,"[null,\"3\",\"phoenix\",\"heartbeat\",{}]");
    assert(!strcmp(expected,sent));
    assert(gin_connect_query_take(s,&receipt)==1 && receipt.outcome==GIN_QUERY_ACCEPTED);
    gin_connect_free(s); gin_topology_free(t); tick.tv_nsec=0;
    send_limit=2048; alternating_again=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    gin_connect_service(s,0); t=topology();
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_OK);
    tick.tv_nsec=100000000; sent_length=0; send_limit=1; send_code=CURLE_SEND_ERROR;
    assert(gin_connect_service(s,0)==GIN_CONNECT_TRANSPORT);
    assert(s->query.outcome==GIN_QUERY_NOT_SENT && !s->query_written);
    assert(gin_connect_retry(s)==GIN_RETRY_BLOCKED);
    gin_connect_free(s); gin_topology_free(t); tick.tv_nsec=0;
    send_limit=2048; send_code=CURLE_OK;
}
static void buffered_application_owns_wire(void)
{
    struct gin_connect *s=session();
    struct gin_topology *t=topology();
    struct gin_liveness_settings p={100,50,10,40,2};
    char expected[2048];
    size_t query_length;
    unsigned i;
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_OK);
    query_length=s->outbound.length; memcpy(expected,s->outbound.data,query_length);
    buffered_send=1; buffered_blocks=2; sent_length=0; send_calls=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(send_calls==1 && buffered_length==query_length);
    assert(!s->offset && !s->query_written && !sent_length);
    tick.tv_nsec=100000000;
    /* Due heartbeat must not replace the encoder's buffered application bytes. */
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(send_calls==2 && !s->offset && !s->query_written);
    assert(s->heartbeat==GIN_HEARTBEAT_QUEUED);
    send_limit=1; alternating_again=1;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->offset==1 && s->query_written && sent_length==1);
    for (i=0; i<sizeof(sent) && s->heartbeat!=GIN_HEARTBEAT_WAITING_REPLY; i++)
        assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->heartbeat==GIN_HEARTBEAT_WAITING_REPLY && !s->outbound.data);
    strcpy(expected+query_length,"[null,\"3\",\"phoenix\",\"heartbeat\",{}]");
    assert(sent_length==strlen(expected) && !memcmp(sent,expected,sent_length));
    assert(!buffered_length && s->query.outcome==GIN_QUERY_PENDING);
    assert(frame(s,"[\"1\",\"2\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]")==GIN_CONNECT_OK);
    assert(s->query.outcome==GIN_QUERY_ACCEPTED);
    gin_connect_free(s); gin_topology_free(t); tick.tv_nsec=0;
    buffered_send=0; send_limit=2048; alternating_again=0;
}
static void buffered_same_length_payload(void)
{
    struct gin_connect *s=session();
    struct gin_liveness_settings p={100,50,10,40,2};
    struct gin_phoenix_fields f={NULL,"1","fixture","heartbeat",{(const unsigned char *)"{}",2}};
    const char heartbeat[]="[null,\"2\",\"phoenix\",\"heartbeat\",{}]";
    char expected[2048];
    size_t length;
    unsigned i;
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    assert(gin_phoenix_encode(&f,&s->outbound)==GIN_PHOENIX_OK);
    length=s->outbound.length;
    assert(length==strlen(heartbeat) && memcmp(s->outbound.data,heartbeat,length));
    memcpy(expected,s->outbound.data,length); strcpy(expected+length,heartbeat);
    buffered_send=1; buffered_blocks=1; sent_length=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK && !sent_length);
    tick.tv_nsec=100000000;
    for(i=0; i<sizeof(sent) && s->heartbeat!=GIN_HEARTBEAT_WAITING_REPLY; i++)
        assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->heartbeat==GIN_HEARTBEAT_WAITING_REPLY);
    assert(sent_length==strlen(expected) && !memcmp(sent,expected,sent_length));
    assert(!buffered_length);
    gin_connect_free(s); tick.tv_nsec=0; buffered_send=0;
}
static void buffered_heartbeat_preempts_unstarted_application(void)
{
    struct gin_connect *s=session();
    struct gin_topology *t=topology();
    struct gin_liveness_settings p={100,50,10,40,2};
    const char heartbeat[]="[null,\"3\",\"phoenix\",\"heartbeat\",{}]";
    char expected[2048];
    size_t query_length;
    unsigned i;
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_OK);
    query_length=s->outbound.length; strcpy(expected,heartbeat);
    memcpy(expected+strlen(heartbeat),s->outbound.data,query_length);
    tick.tv_nsec=100000000; buffered_send=1; buffered_blocks=2; sent_length=0; send_calls=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(buffered_length==strlen(heartbeat) && !memcmp(buffered_payload,heartbeat,buffered_length));
    assert(!s->heartbeat_offset && !s->offset && !s->query_written);
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(send_calls==2 && !s->query_written && !sent_length);
    send_limit=1; alternating_again=1;
    for(i=0; i<sizeof(sent) && (s->outbound.data || s->heartbeat!=GIN_HEARTBEAT_WAITING_REPLY); i++) {
        assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
        if(s->heartbeat!=GIN_HEARTBEAT_WAITING_REPLY) assert(!s->query_written && !s->offset);
    }
    assert(s->heartbeat==GIN_HEARTBEAT_WAITING_REPLY && !s->outbound.data && s->query_written);
    assert(sent_length==strlen(heartbeat)+query_length && !memcmp(sent,expected,sent_length));
    assert(!buffered_length);
    gin_connect_free(s); gin_topology_free(t); tick.tv_nsec=0;
    buffered_send=0; send_limit=2048; alternating_again=0;
}
static void buffered_writer_abort_resets(void)
{
    struct gin_liveness_settings p={100,50,10,40,2};
    unsigned heartbeat, termination, partial;
    for(heartbeat=0; heartbeat<2; heartbeat++) for(termination=0; termination<3; termination++)
        for(partial=0; partial<2; partial++) {
        struct gin_connect *s=session();
        struct gin_topology *t=topology();
        struct gin_query_result receipt;
        assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
        assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_OK);
        if(heartbeat) tick.tv_nsec=100000000;
        buffered_send=1; buffered_blocks=1; sent_length=0; send_calls=0;
        assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
        assert(buffered_length && !sent_length && !s->query_written);
        if(partial) {
            send_limit=1; alternating_again=1; send_calls=0;
            assert(gin_connect_service(s,0)==GIN_CONNECT_OK && sent_length==1);
            alternating_again=0;
        }
        if(termination==0) gin_connect_close(s);
        else if(termination==1) {
            send_limit=0; send_code=CURLE_SEND_ERROR;
            assert(gin_connect_service(s,0)==GIN_CONNECT_TRANSPORT);
        } else {
            buffered_blocks=1; recv_code=CURLE_GOT_NOTHING;
            assert(gin_connect_service(s,0)==GIN_CONNECT_CLOSED);
        }
        assert(s->writer==WIRE_IDLE && !s->outbound.data && !s->offset);
        assert(s->heartbeat==GIN_HEARTBEAT_OFF && !s->heartbeat_offset && !s->heartbeat_length);
        assert(gin_connect_query_take(s,&receipt)==1);
        assert(receipt.outcome==(!heartbeat && partial ? GIN_QUERY_UNCERTAIN : GIN_QUERY_NOT_SENT));
        /* The mock transport's retained encoder is destroyed with the socket. */
        buffered_length=buffered_offset=0; buffered_blocks=0;
        buffered_send=0; send_limit=2048; send_code=CURLE_OK; recv_code=CURLE_AGAIN;
        gin_connect_free(s); gin_topology_free(t); tick.tv_nsec=0;
    }
}
static void query_clock_terminal(void)
{
    struct gin_connect *s=session(); struct gin_topology *t=topology();
    ready(s); bad_clock=1;
    assert(gin_connect_open_query(s,t,"Net","Bob","#",GIN_QUERY_MODE_PLAIN,1,1000)==GIN_QUERY_TRANSPORT);
    assert(gin_connect_get_state(s)==GIN_CONNECT_FAILED && s->result.code==GIN_CONNECT_CLOCK);
    assert(s->refs.current==1 && s->query.outcome==GIN_QUERY_NONE);
    bad_clock=0; gin_connect_free(s); gin_topology_free(t);
}
static void timer_boundaries(void)
{
    struct gin_liveness_settings p={100,50,10,40,2};
    struct gin_connect *s=session();
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    poll_wait=-1;
    assert(gin_connect_service(s,1000)==GIN_CONNECT_OK && poll_wait==100);
    tick.tv_nsec=99999999;
    assert(gin_connect_service(s,1000)==GIN_CONNECT_OK && poll_wait==1);
    tick.tv_nsec=100000000; send_limit=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(s->heartbeat==GIN_HEARTBEAT_QUEUED);
    assert(frame(s,"[null,\"2\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]")==GIN_CONNECT_MALFORMED);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    gin_connect_free(s); tick.tv_nsec=0; send_limit=2048;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    tick.tv_nsec=100000000; sent_length=0;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    tick.tv_nsec=150000000;
    assert(frame(s,"[null,\"2\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]")==GIN_CONNECT_TIMEOUT);
    gin_connect_free(s); tick.tv_nsec=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    s->query.outcome=GIN_QUERY_PENDING; s->query_deadline.tv_sec=100; s->query_deadline.tv_nsec=150000000;
    tick.tv_nsec=150000000;
    assert(gin_connect_service(s,0)==GIN_CONNECT_TIMEOUT);
    assert(s->reason==GIN_LIVENESS_REASON_QUERY_TIMEOUT);
    gin_connect_free(s); tick.tv_nsec=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    poll_interrupts=1; poll_advance_ms=150;
    assert(gin_connect_service(s,1000)==GIN_CONNECT_TIMEOUT);
    assert(s->reason==GIN_LIVENESS_REASON_HEARTBEAT_TIMEOUT && !poll_interrupts);
    poll_advance_ms=0; gin_connect_free(s); tick.tv_nsec=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    tick.tv_nsec=100000000; overreport=1;
    assert(gin_connect_service(s,0)==GIN_CONNECT_TRANSPORT);
    overreport=0; gin_connect_free(s); tick.tv_nsec=0;
}
static void unconfigured_clock_restart(void)
{
    struct gin_connect *s=session(); ready(s); bad_clock=1;
    assert(gin_connect_service(s,0)==GIN_CONNECT_CLOCK);
    bad_clock=0;
    assert(gin_connect_start(s)==GIN_CONNECT_TIMEOUT);
    gin_connect_free(s);
}
static void finite_retry_scope(void)
{
    struct gin_connect *s=session();
    struct gin_liveness_settings p={100,50,10,40,2}, invalid=p;
    struct gin_connect_result before;
    unsigned start_fetches=fetches, start_identities=identities, start_upgrades=upgrades;
    invalid.retry_cap_ms=0;
    assert(gin_connect_set_liveness(s,&invalid)==GIN_CONNECT_INVALID && !s->configured);
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK);
    assert(gin_connect_start(s)==GIN_CONNECT_TIMEOUT);
    assert(s->retries_used==0 && s->result.socket_epoch==1);
    before=s->result;
    assert(gin_connect_start(s)==GIN_CONNECT_INVALID);
    assert(!memcmp(&before,&s->result,sizeof(before)) && s->retries_used==0);
    tick.tv_nsec=10000000;
    assert(gin_connect_retry(s)==GIN_RETRY_ATTEMPTED);
    assert(s->retries_used==1 && s->result.socket_epoch==2);
    assert(gin_connect_liveness_status(s).retry_wait_ms==20);
    tick.tv_nsec=30000000;
    assert(gin_connect_start(s)==GIN_CONNECT_TIMEOUT);
    assert(s->retries_used==2 && s->result.socket_epoch==3);
    before=s->result;
    assert(gin_connect_retry(s)==GIN_RETRY_EXHAUSTED);
    assert(!memcmp(&before,&s->result,sizeof(before)));
    assert(fetches-start_fetches==3 && identities-start_identities==3 && upgrades-start_upgrades==3);
    gin_connect_free(s); tick.tv_nsec=0;
}
static void arithmetic_limits(void)
{
    struct timespec t={100,999999999}, out={0,0};
    uintmax_t maximum=(time_t)-1 > 0 ? (uintmax_t)(time_t)-1 :
        UINTMAX_MAX >> (sizeof(uintmax_t)*CHAR_BIT-sizeof(time_t)*CHAR_BIT+1);
    struct gin_connect *s=session();
    struct gin_liveness_settings p={100,50,10,40,2};
    assert(time_add(&out,&t,1) && out.tv_sec==101 && out.tv_nsec==999999);
    t.tv_sec=(time_t)maximum;
    assert(!time_add(&out,&t,1));
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    tick.tv_sec=(time_t)maximum; tick.tv_nsec=999999999;
    assert(gin_connect_service(s,1)==GIN_CONNECT_CLOCK);
    assert(s->refs.current==1);
    gin_connect_free(s); tick.tv_sec=100; tick.tv_nsec=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    s->refs.current=UINT64_MAX; tick.tv_nsec=100000000;
    assert(gin_connect_service(s,0)==GIN_CONNECT_LIMIT && s->refs.current==UINT64_MAX);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    gin_connect_free(s); tick.tv_nsec=0;
}
static void local_io_faults_terminal(void)
{
    struct gin_liveness_settings p={100,50,10,40,2};
    struct gin_connect *s=session();
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    tick.tv_nsec=100000000; overreport=1;
    assert(gin_connect_service(s,0)==GIN_CONNECT_TRANSPORT);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    assert(s->reason==GIN_LIVENESS_REASON_LOCAL);
    overreport=0; gin_connect_free(s); tick.tv_nsec=0;
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    recv_code=CURLE_OK;
    assert(gin_connect_service(s,0)==GIN_CONNECT_TRANSPORT);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    recv_code=CURLE_AGAIN; gin_connect_free(s);
    s=session(); assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    poll_events=POLLNVAL;
    assert(gin_connect_service(s,10)==GIN_CONNECT_TRANSPORT);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    poll_events=0; gin_connect_free(s);
    assert(curl_error(CURLE_SSL_CERTPROBLEM)==GIN_CONNECT_TLS);
    assert(curl_error(CURLE_SSL_INVALIDCERTSTATUS)==GIN_CONNECT_TLS);
}
static void heartbeat_matching_and_budget(void)
{
    struct gin_liveness_settings p={100,50,10,40,2};
    struct gin_connect *s=session();
    struct gin_connect before;
    const char *foreign[]={
        "[\"1\",\"2\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]",
        "[null,\"2\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]",
        "[null,\"3\",\"phoenix\",\"phx_reply\",{\"status\":\"ok\",\"response\":{}}]"
    };
    size_t i;
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK); ready(s);
    sent_length=0; send_limit=1; send_calls=0; tick.tv_nsec=100000000;
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK);
    assert(send_calls==GIN_CONNECT_OPERATIONS_MAX/2 && s->heartbeat==GIN_HEARTBEAT_WRITING);
    assert(sent_length==GIN_CONNECT_OPERATIONS_MAX/2);
    assert(gin_connect_service(s,0)==GIN_CONNECT_OK && s->heartbeat==GIN_HEARTBEAT_WAITING_REPLY);
    send_limit=2048;
    for(i=0;i<sizeof(foreign)/sizeof(foreign[0]);i++) {
        assert(frame(s,foreign[i])==GIN_CONNECT_OK);
        assert(s->heartbeat==GIN_HEARTBEAT_WAITING_REPLY);
    }
    s->in_callback=1; before=*s;
    assert(gin_connect_liveness_status(s).heartbeat==GIN_HEARTBEAT_WAITING_REPLY);
    assert(gin_connect_retry(s)==GIN_RETRY_INVALID);
    assert(gin_connect_start(s)==GIN_CONNECT_INVALID);
    gin_connect_close(s);
    assert(!memcmp(&before,s,sizeof(before)));
    s->in_callback=0;
    assert(frame(s,"[null,\"2\",\"phoenix\",\"phx_reply\",{\"status\":\"unknown\",\"response\":{}}]")==GIN_CONNECT_MALFORMED);
    assert(gin_connect_retry(s)==GIN_RETRY_TERMINAL);
    gin_connect_free(s); tick.tv_nsec=0;
}
static void retry_observes_clock(void)
{
    struct gin_connect *s=session(); struct gin_liveness_settings p={100,50,10,40,2};
    assert(gin_connect_set_liveness(s,&p)==GIN_CONNECT_OK);
    assert(gin_connect_start(s)==GIN_CONNECT_TIMEOUT);
    tick.tv_nsec=20000000; fetch_rewind_ns=10000000;
    assert(gin_connect_retry(s)==GIN_RETRY_ATTEMPTED);
    assert(s->result.code==GIN_CONNECT_CLOCK);
    fetch_rewind_ns=0; gin_connect_free(s); tick.tv_nsec=0;
}
int main(void)
{
    severance(); retry_policy(); clock_failure(); heartbeat_wire(); failure_classifier(); upgrade_precedence();
    independent_frames(); buffered_application_owns_wire();
    buffered_same_length_payload(); buffered_heartbeat_preempts_unstarted_application();
    buffered_writer_abort_resets(); query_clock_terminal();
    timer_boundaries(); unconfigured_clock_restart();
    finite_retry_scope(); arithmetic_limits();
    local_io_faults_terminal();
    heartbeat_matching_and_budget();
    retry_observes_clock();
    return 0;
}
