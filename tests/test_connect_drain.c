/* Private-TU I/O faults only; production has no test switches. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <curl/curl.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include "gin_connect.h"
static struct timespec tick = {100, 0};
static unsigned reads, writes, frames, polls;
static int read_flags = CURLWS_PING, read_final = 1, ready_poll;
static size_t send_step, read_size = 1;
static CURLcode receive_code = CURLE_AGAIN;
static struct gin_connect *observed_session;
/* Also lets the pre-API bootstrap fail at the linker, not implicit declaration. */
struct gin_connect;
extern int gin_connect_receive_drained(const struct gin_connect *);
static int fixture_clock(clockid_t id, struct timespec *out)
{ assert(id == CLOCK_MONOTONIC); *out = tick; return 0; }
static CURLcode fixture_recv(CURL *c, void *p, size_t cap, size_t *out,
    const struct curl_ws_frame **meta)
{
    static struct curl_ws_frame m;
    static const char text[] = "[null,null,\"unrelated\",\"event\",{}]";
    (void)c; reads++;
    assert(!gin_connect_receive_drained(observed_session));
    *out = 0; *meta = NULL;
    if (!frames) return receive_code;
    frames--;
    memset(&m, 0, sizeof(m)); m.flags = read_flags;
    m.bytesleft = read_final ? 0 : 1;
    *out = read_flags & CURLWS_TEXT ? sizeof(text)-1 : read_size;
    assert(*out <= cap);
    if (read_flags & CURLWS_TEXT) memcpy(p, text, *out);
    else memset(p, 'x', *out);
    *meta = &m; return CURLE_OK;
}
static CURLcode fixture_send(CURL *c, const void *p, size_t cap, size_t *out,
    curl_off_t total, unsigned flags)
{
    (void)c; (void)p; assert(total == 0 && flags == CURLWS_TEXT);
    assert(!gin_connect_receive_drained(observed_session));
    writes++; *out = cap < send_step ? cap : send_step;
    return *out ? CURLE_OK : CURLE_AGAIN;
}
static int fixture_poll(struct pollfd *p, nfds_t n, int ms)
{
    (void)ms; assert(n == 1); polls++;
    if (ready_poll) { p->revents = POLLIN; frames = GIN_CONNECT_OPERATIONS_MAX; return 1; }
    p->revents = 0; return 0;
}
#define clock_gettime fixture_clock
#define curl_ws_recv fixture_recv
#define curl_ws_send fixture_send
#define poll fixture_poll
#include "../src/connect.c"
#undef clock_gettime
#undef curl_ws_recv
#undef curl_ws_send
#undef poll
static struct gin_connect *session(void)
{
    struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,1}, 1000};
    struct gin_credential c = {"https://example.invalid", "aa~", "12345678-1234-1234-1234-123456789abc", "Alice"};
    struct gin_connect *s = NULL;
    assert(gin_connect_new(&settings, &c, NULL, 0, NULL, NULL, &s) == GIN_CONNECT_OK);
    assert(!gin_connect_receive_drained(s));
    s->state = GIN_CONNECT_READY; s->result.socket_epoch = 1;
    observed_session = s;
    reads = writes = frames = polls = 0; ready_poll = 0;
    read_flags = CURLWS_PING; read_final = 1; read_size = 1;
    send_step = 0; receive_code = CURLE_AGAIN;
    return s;
}
static void quiet_and_reset(void)
{
    struct gin_connect *s = session();
    assert(!gin_connect_receive_drained(NULL));
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(reads == 1 && gin_connect_receive_drained(s));
    assert(gin_connect_service(s, GIN_CONNECT_SERVICE_MAX_MS+1) == GIN_CONNECT_INVALID);
    assert(!gin_connect_receive_drained(s));
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    s->in_callback = 1;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_INVALID);
    assert(!gin_connect_receive_drained(s));
    s->in_callback = 0;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    gin_connect_close(s); assert(!gin_connect_receive_drained(s));
    s->state = GIN_CONNECT_READY;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    receive_code = CURLE_RECV_ERROR;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_TRANSPORT);
    assert(!gin_connect_receive_drained(s));
    gin_connect_free(s);
}
static void operation_cap_and_poll(void)
{
    struct gin_connect *s = session();
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    frames = GIN_CONNECT_OPERATIONS_MAX;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(reads == 1+GIN_CONNECT_OPERATIONS_MAX && !gin_connect_receive_drained(s));
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    /* AGAIN at precisely the final operation is genuine evidence. */
    frames = GIN_CONNECT_OPERATIONS_MAX-1;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    ready_poll = 1;
    assert(gin_connect_service(s, 10) == GIN_CONNECT_OK);
    assert(polls == 1 && !gin_connect_receive_drained(s));
    ready_poll = 0; frames = 0;
    assert(gin_connect_service(s, 10) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    gin_connect_free(s);
}
static void byte_cap(void)
{
    struct gin_connect *s = session();
    /* The outbound writer can consume the byte quantum before a receive. */
    s->outbound.data = malloc(GIN_CONNECT_IO_MAX);
    assert(s->outbound.data); s->outbound.length = GIN_CONNECT_IO_MAX;
    send_step = GIN_CONNECT_IO_MAX;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(writes == 1 && reads == 0 && !gin_connect_receive_drained(s));
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    gin_connect_free(s);
}
static void outbound_progress(void)
{
    struct gin_connect *s = session();
    s->outbound.data = malloc(100); assert(s->outbound.data); s->outbound.length = 100;
    send_step = 1;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    /* Each AGAIN preceded by write progress is invalidated next iteration. */
    assert(writes == 32 && reads == 32 && gin_connect_receive_drained(s));
    assert(s->outbound.data && s->offset == 32);
    send_step = 0;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s) && s->writer == WIRE_APPLICATION);
    gin_connect_free(s);
}
static void partial_text(void)
{
    struct gin_connect *s = session();
    frames = 1; read_flags = CURLWS_TEXT | CURLWS_CONT; read_final = 0;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(!gin_connect_receive_drained(s));
    frames = 1; read_flags = CURLWS_PING; read_final = 1;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(!gin_connect_receive_drained(s));
    /* Teardown clears the collector's partial-message observation. */
    gin_connect_close(s); s->state = GIN_CONNECT_READY;
    assert(gin_connect_service(s, 0) == GIN_CONNECT_OK);
    assert(gin_connect_receive_drained(s));
    gin_connect_free(s);
}
int main(void)
{
    quiet_and_reset(); operation_cap_and_poll(); byte_cap(); outbound_progress(); partial_text();
    return 0;
}
