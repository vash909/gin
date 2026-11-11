#include "gin_connect.h"
#include "connect_values.h"
#include <curl/curl.h>
#include <jansson.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#if LIBCURL_VERSION_NUM < 0x081700
#error libcurl 8.23.0 or newer is required for WebSocket transport
#endif

struct topic {
    struct gin_connect_topic_status status;
    char text[GIN_WS_TOPIC_CAP];
    char ref[GIN_PHOENIX_REF_CAP];
    char network[256];
    char target[257];
    int has_network, has_target;
};
struct gin_connect {
    struct gin_connect_settings settings;
    struct gin_credential credential;
    char *origin, *ca;
    struct topic topics[GIN_CONNECT_TOPICS_MAX];
    size_t count, pending, offset;
    struct gin_phoenix_bytes outbound;
    struct gin_phoenix_stream *stream;
    struct gin_phoenix_refs refs;
    struct gin_connect_result result;
    enum gin_connect_state state;
    gin_connect_frame_fn callback;
    void *user;
    CURL *curl;
    struct curl_slist *headers;
    curl_socket_t socket;
    struct timespec deadline;
    size_t header_bytes;
    int selection_count, selection_ok, header_limit;
};
static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
static int now(struct timespec *t) { return clock_gettime(CLOCK_MONOTONIC, t) == 0; }
static long remaining(const struct timespec *end)
{
    struct timespec t;
    long seconds, nanos;
    if (!now(&t)) return 0;
    seconds = (long)(end->tv_sec - t.tv_sec);
    nanos = end->tv_nsec - t.tv_nsec;
    if (seconds < 0 || (seconds == 0 && nanos <= 0)) return 0;
    if (seconds > LONG_MAX / 1000 - 1) return LONG_MAX;
    return seconds * 1000 + nanos / 1000000 + (nanos % 1000000 != 0);
}
static int deadline(struct timespec *t, long ms)
{
    if (!now(t)) return 0;
    t->tv_sec += ms / 1000;
    t->tv_nsec += (ms % 1000) * 1000000;
    if (t->tv_nsec >= 1000000000) { t->tv_sec++; t->tv_nsec -= 1000000000; }
    return 1;
}
static void transport_close(struct gin_connect *s)
{
    size_t i;
    if (s->curl) curl_easy_cleanup(s->curl);
    s->curl = NULL;
    {
        struct curl_slist *h;
        for (h = s->headers; h; h = h->next) if (h->data) wipe(h->data, strlen(h->data));
        curl_slist_free_all(s->headers); s->headers = NULL;
    }
    s->socket = CURL_SOCKET_BAD;
    gin_phoenix_bytes_free(&s->outbound); s->offset = 0;
    gin_phoenix_stream_reset(s->stream);
    for (i = 0; i < s->count; i++) {
        s->topics[i].status.join_ref = NULL;
        wipe(s->topics[i].ref, sizeof(s->topics[i].ref));
        if (s->topics[i].status.state != GIN_CONNECT_TOPIC_REJECTED)
            s->topics[i].status.state = GIN_CONNECT_TOPIC_DESIRED;
    }
}
static enum gin_connect_code fail(struct gin_connect *s, enum gin_connect_code c)
{
    transport_close(s); s->state = GIN_CONNECT_FAILED; s->result.code = c; return c;
}
static int available(void)
{
    const curl_version_info_data *v = curl_version_info(CURLVERSION_NOW);
    const char *const *p;
    int ws = 0, wss = 0;
    if (!v || v->version_num < 0x081700 || !(v->features & CURL_VERSION_SSL) ||
        !(v->features & CURL_VERSION_ASYNCHDNS) || !v->protocols) return 0;
    for (p = v->protocols; *p; p++) {
        if (!strcmp(*p, "ws")) ws = 1;
        if (!strcmp(*p, "wss")) wss = 1;
    }
    return ws && wss;
}
static enum gin_connect_code add_topic(struct gin_connect *s, const char *network, const char *target)
{
    char text[GIN_WS_TOPIC_CAP];
    size_t n, i;
    enum gin_ws_value_code c = gin_ws_topic(s->credential.subject_name, network, target, text, sizeof(text), &n);
    if (c != GIN_WS_VALUE_OK) return c == GIN_WS_VALUE_LIMIT ? GIN_CONNECT_LIMIT : GIN_CONNECT_INVALID;
    for (i = 0; i < s->count; i++) if (!strcmp(text, s->topics[i].text)) return GIN_CONNECT_OK;
    if (s->count == GIN_CONNECT_TOPICS_MAX) return GIN_CONNECT_LIMIT;
    i = s->count++;
    memcpy(s->topics[i].text, text, n + 1);
    s->topics[i].status.topic = s->topics[i].text;
    if (network) { strcpy(s->topics[i].network, network); s->topics[i].has_network = 1; }
    if (target) { strcpy(s->topics[i].target, target); s->topics[i].has_target = 1; }
    return GIN_CONNECT_OK;
}
enum gin_connect_code gin_connect_new(const struct gin_connect_settings *settings,
    const struct gin_credential *credential, const struct gin_connect_subscription *plan,
    size_t count, gin_connect_frame_fn callback, void *user, struct gin_connect **out)
{
    struct gin_connect *s;
    size_t i;
    enum gin_connect_code c;
    if (!out || *out || !settings || !credential || (!plan && count) ||
        gin_credential_validate(credential) != GIN_CREDENTIAL_OK ||
        !gin_http_origin_valid(settings->http.origin) || strcmp(settings->http.origin, credential->origin) ||
        settings->http.connect_ms <= 0 || settings->http.total_ms <= 0 ||
        settings->policy.client_protocol <= 0 || settings->policy.required_server_protocol <= 0 ||
        settings->join_ms <= 0 || settings->join_ms > INT_MAX) return GIN_CONNECT_INVALID;
    s = calloc(1, sizeof(*s));
    if (!s) return GIN_CONNECT_NOMEM;
    s->settings = *settings; s->credential = *credential;
    s->origin = strdup(settings->http.origin);
    if (settings->http.ca_file) s->ca = strdup(settings->http.ca_file);
    if (!s->origin || (settings->http.ca_file && !s->ca) ||
        gin_phoenix_stream_new(&s->stream) != GIN_PHOENIX_OK) {
        gin_connect_free(s); return GIN_CONNECT_NOMEM;
    }
    s->settings.http.origin = s->origin; s->settings.http.ca_file = s->ca;
    s->callback = callback; s->user = user; s->socket = CURL_SOCKET_BAD;
    c = add_topic(s, NULL, NULL);
    for (i = 0; i < count && c == GIN_CONNECT_OK; i++) {
        if (!plan[i].network) { c = GIN_CONNECT_INVALID; break; }
        c = add_topic(s, plan[i].network, NULL);
        if (c == GIN_CONNECT_OK && plan[i].target) c = add_topic(s, plan[i].network, plan[i].target);
    }
    if (c != GIN_CONNECT_OK) { gin_connect_free(s); return c; }
    *out = s; return GIN_CONNECT_OK;
}
static size_t header(char *p, size_t size, size_t count, void *user)
{
    struct gin_connect *s = user;
    static const char field[] = "Sec-WebSocket-Protocol:";
    size_t n, start = sizeof(field) - 1, end;
    if (size && count > SIZE_MAX / size) return 0;
    n = size * count;
    if (n > GIN_HTTP_HEADER_MAX - s->header_bytes) { s->header_limit = 1; return 0; }
    s->header_bytes += n;
    if (n >= 5 && !memcmp(p, "HTTP/", 5)) { s->selection_count = 0; s->selection_ok = 0; }
    if (n >= start && !strncasecmp(p, field, start)) {
        s->selection_count++;
        end = n;
        while (start < end && (p[start] == ' ' || p[start] == '\t')) start++;
        while (end > start && (p[end-1] == '\r' || p[end-1] == '\n' || p[end-1] == ' ' || p[end-1] == '\t')) end--;
        s->selection_ok = end - start == 7 && !memcmp(p + start, "phoenix", 7);
    }
    return n;
}
static size_t discard(char *p, size_t a, size_t b, void *u)
{ (void)p; (void)a; (void)b; (void)u; return 0; }
static enum gin_connect_code curl_error(CURLcode c)
{
    if (c == CURLE_OPERATION_TIMEDOUT) return GIN_CONNECT_TIMEOUT;
    if (c == CURLE_PEER_FAILED_VERIFICATION || c == CURLE_SSL_CONNECT_ERROR ||
        c == CURLE_SSL_CACERT_BADFILE) return GIN_CONNECT_TLS;
    return GIN_CONNECT_TRANSPORT;
}
static enum gin_connect_code queue_join(struct gin_connect *s)
{
    struct gin_phoenix_fields f;
    struct topic *t = &s->topics[s->pending];
    enum gin_phoenix_code c;
    c = gin_phoenix_refs_next(&s->refs, t->ref, sizeof(t->ref));
    if (c != GIN_PHOENIX_OK) return fail(s, GIN_CONNECT_LIMIT);
    f.join_ref = t->ref; f.ref = t->ref; f.topic = t->text; f.event = "phx_join";
    f.payload.data = (const unsigned char *)"{}"; f.payload.length = 2;
    c = gin_phoenix_encode(&f, &s->outbound);
    if (c != GIN_PHOENIX_OK) return fail(s, c == GIN_PHOENIX_NOMEM ? GIN_CONNECT_NOMEM : GIN_CONNECT_LIMIT);
    t->status.state = GIN_CONNECT_TOPIC_PENDING; t->status.join_ref = t->ref;
    if (!deadline(&s->deadline, s->settings.join_ms)) return fail(s, GIN_CONNECT_TRANSPORT);
    return GIN_CONNECT_OK;
}
enum gin_connect_code gin_connect_start(struct gin_connect *s)
{
    struct gin_protocol_info info;
    struct gin_credential identity;
    char url[GIN_CREDENTIAL_ORIGIN_MAX + 100], offer[GIN_WS_PROTOCOL_CAP], line[GIN_WS_PROTOCOL_CAP + 32];
    size_t n, i;
    CURLcode c;
    if (!s || s->state == GIN_CONNECT_CONNECTING || s->state == GIN_CONNECT_JOINING || s->state == GIN_CONNECT_READY)
        return GIN_CONNECT_INVALID;
    if (s->result.socket_epoch == UINT64_MAX) return fail(s, GIN_CONNECT_LIMIT);
    {
        uint64_t epoch = s->result.socket_epoch + 1u;
        transport_close(s);
        memset(&s->result, 0, sizeof(s->result));
        s->result.socket_epoch = epoch;
    }
    s->state = GIN_CONNECT_CONNECTING;
    /* Reuse no request references or transport state across explicit attempts. */
    if (!available()) return fail(s, GIN_CONNECT_UNAVAILABLE);
    s->result.protocol = gin_protocol_fetch(&s->settings.http, &s->settings.policy, &info);
    if (s->result.protocol != GIN_PROTOCOL_OK) return fail(s, GIN_CONNECT_PREFLIGHT);
    s->result.auth_attempted = 1;
    s->result.auth = gin_auth_identity(&s->settings.http, &s->credential, &identity);
    if (s->result.auth != GIN_AUTH_OK) {
        enum gin_connect_code code = GIN_CONNECT_UNAUTHORIZED;
        if (s->result.auth == GIN_AUTH_SCOPE) code = GIN_CONNECT_SCOPE;
        else if (s->result.auth == GIN_AUTH_FORBIDDEN) code = GIN_CONNECT_FORBIDDEN;
        else if (s->result.auth == GIN_AUTH_TLS) code = GIN_CONNECT_TLS;
        else if (s->result.auth == GIN_AUTH_TIMEOUT) code = GIN_CONNECT_TIMEOUT;
        gin_credential_clear(&identity); return fail(s, code);
    }
    for (i = 0; i < s->count; i++) {
        struct topic *t = &s->topics[i];
        enum gin_ws_value_code v = gin_ws_topic(identity.subject_name,
            t->has_network ? t->network : NULL, t->has_target ? t->target : NULL,
            t->text, sizeof(t->text), &n);
        t->status.state = GIN_CONNECT_TOPIC_DESIRED;
        if (v != GIN_WS_VALUE_OK) { gin_credential_clear(&identity); return fail(s, GIN_CONNECT_INVALID); }
    }
    gin_credential_clear(&identity);
    if (gin_ws_protocols(s->credential.token, offer, sizeof(offer), &n) != GIN_WS_VALUE_OK)
        return fail(s, GIN_CONNECT_INVALID);
    (void)snprintf(line, sizeof(line), "Sec-WebSocket-Protocol: %s", offer);
    wipe(offer, sizeof(offer));
    s->headers = curl_slist_append(NULL, line); wipe(line, sizeof(line));
    if (!s->headers || !(s->curl = curl_easy_init())) return fail(s, GIN_CONNECT_NOMEM);
    (void)snprintf(url, sizeof(url), "wss%s/socket/websocket?vsn=2.0.0&client_proto=%d",
        s->origin + 5, s->settings.policy.client_protocol);
    s->selection_count = 0; s->selection_ok = 0; s->header_bytes = 0; s->header_limit = 0;
#define OPT(k,v) do { if (curl_easy_setopt(s->curl, k, v) != CURLE_OK) return fail(s, GIN_CONNECT_UNAVAILABLE); } while (0)
    OPT(CURLOPT_URL, url); OPT(CURLOPT_HTTPHEADER, s->headers);
    OPT(CURLOPT_CONNECT_ONLY, 2L); OPT(CURLOPT_HTTP_VERSION, (long)CURL_HTTP_VERSION_1_1);
    OPT(CURLOPT_PROTOCOLS_STR, "wss"); OPT(CURLOPT_REDIR_PROTOCOLS_STR, "wss");
    OPT(CURLOPT_FOLLOWLOCATION, 0L); OPT(CURLOPT_MAXREDIRS, 0L);
    OPT(CURLOPT_PROXY, ""); OPT(CURLOPT_NOPROXY, "*");
    OPT(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED); OPT(CURLOPT_HTTPAUTH, (long)CURLAUTH_NONE);
    OPT(CURLOPT_PROXYAUTH, (long)CURLAUTH_NONE); OPT(CURLOPT_NOSIGNAL, 1L);
    OPT(CURLOPT_SSL_VERIFYPEER, 1L); OPT(CURLOPT_SSL_VERIFYHOST, 2L);
    OPT(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    if (s->ca) OPT(CURLOPT_CAINFO, s->ca);
    OPT(CURLOPT_CONNECTTIMEOUT_MS, s->settings.http.connect_ms);
    OPT(CURLOPT_TIMEOUT_MS, s->settings.http.total_ms); OPT(CURLOPT_WS_OPTIONS, 0L);
    OPT(CURLOPT_HEADERFUNCTION, header); OPT(CURLOPT_HEADERDATA, s);
    OPT(CURLOPT_WRITEFUNCTION, discard); OPT(CURLOPT_VERBOSE, 0L);
#undef OPT
    c = curl_easy_perform(s->curl);
    if (curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &s->result.http_status) != CURLE_OK)
        return fail(s, GIN_CONNECT_TRANSPORT);
    if (s->header_limit) return fail(s, GIN_CONNECT_LIMIT);
    if (s->result.http_status == 403) return fail(s, GIN_CONNECT_FORBIDDEN);
    if (s->result.http_status == 426) return fail(s, GIN_CONNECT_UPGRADE_REQUIRED);
    if (s->result.http_status != 101 && s->result.http_status != 0) return fail(s, GIN_CONNECT_HANDSHAKE);
    if (c != CURLE_OK) return fail(s, curl_error(c));
    if (s->result.http_status != 101 || s->selection_count != 1 || !s->selection_ok)
        return fail(s, GIN_CONNECT_HANDSHAKE);
    if (curl_easy_getinfo(s->curl, CURLINFO_ACTIVESOCKET, &s->socket) != CURLE_OK || s->socket == CURL_SOCKET_BAD)
        return fail(s, GIN_CONNECT_TRANSPORT);
    s->pending = 0; s->offset = 0; s->state = GIN_CONNECT_JOINING;
    return queue_join(s);
}
static enum gin_connect_code receive_frame(struct gin_connect *s, struct gin_phoenix_frame *frame)
{
    const struct gin_phoenix_fields *f = gin_phoenix_frame_fields(frame);
    size_t i;
    if (s->state == GIN_CONNECT_JOINING) {
        struct topic *t = &s->topics[s->pending];
        if (gin_phoenix_reply_matches(frame, t->text, t->ref, t->ref)) {
            const struct gin_phoenix_reply *r = gin_phoenix_frame_reply(frame);
            if (r->status != GIN_PHOENIX_REPLY_OK) {
                t->status.state = GIN_CONNECT_TOPIC_REJECTED; return fail(s, GIN_CONNECT_JOIN_REJECTED);
            }
            if (s->pending == 0) {
                json_t *root = json_loadb((const char *)r->response.data, r->response.length, JSON_REJECT_DUPLICATES, NULL);
                json_t *v = root ? json_object_get(root, "protocol_version") : NULL;
                int valid = json_is_integer(v) && json_integer_value(v) > 0 &&
                    json_integer_value(v) <= INT_MAX && json_integer_value(v) >= s->settings.policy.required_server_protocol;
                json_decref(root);
                if (!valid) return fail(s, GIN_CONNECT_PREFLIGHT);
            }
            /* A reply cannot acknowledge bytes that have not yet left our queue. */
            if (s->outbound.data) return fail(s, GIN_CONNECT_MALFORMED);
            t->status.state = GIN_CONNECT_TOPIC_JOINED;
            if (++s->pending == s->count) s->state = GIN_CONNECT_READY;
            else return queue_join(s);
            return GIN_CONNECT_OK;
        }
    }
    if (!strcmp(f->event, "phx_reply")) return GIN_CONNECT_OK;
    for (i = 0; i < s->count; i++) {
        struct topic *t = &s->topics[i];
        if (strcmp(t->text, f->topic) || !t->status.join_ref ||
            (f->join_ref && strcmp(t->ref, f->join_ref))) continue;
        if (!strcmp(f->event, "phx_error") || !strcmp(f->event, "phx_close")) return fail(s, GIN_CONNECT_CLOSED);
        if (t->status.state == GIN_CONNECT_TOPIC_JOINED && s->callback)
            s->callback(s->user, frame, s->result.socket_epoch);
        break;
    }
    return GIN_CONNECT_OK;
}
enum gin_connect_code gin_connect_service(struct gin_connect *s, unsigned max_wait_ms)
{
    unsigned operations = 0;
    size_t bytes = 0;
    int polled = 0;
    struct timespec end;
    if (!s || max_wait_ms > GIN_CONNECT_SERVICE_MAX_MS) return GIN_CONNECT_INVALID;
    if (s->state == GIN_CONNECT_IDLE || s->state == GIN_CONNECT_CONNECTING) return GIN_CONNECT_INVALID;
    if (s->state != GIN_CONNECT_JOINING && s->state != GIN_CONNECT_READY) return s->result.code;
    if (!deadline(&end, (long)max_wait_ms)) return fail(s, GIN_CONNECT_TRANSPORT);
    while (operations < GIN_CONNECT_OPERATIONS_MAX && bytes < GIN_CONNECT_IO_MAX) {
        unsigned char buffer[16384];
        size_t n = 0;
        int progress = 0;
        CURLcode c;
        const struct curl_ws_frame *meta = NULL;
        if (s->state == GIN_CONNECT_JOINING && !remaining(&s->deadline)) return fail(s, GIN_CONNECT_TIMEOUT);
        if (s->outbound.data) {
            size_t left = s->outbound.length - s->offset;
            c = curl_ws_send(s->curl, s->outbound.data + s->offset, left, &n, 0, CURLWS_TEXT);
            operations++;
            if (n > left) return fail(s, GIN_CONNECT_TRANSPORT);
            if (c != CURLE_OK && c != CURLE_AGAIN) return fail(s, curl_error(c));
            s->offset += n; bytes += n; progress = n != 0;
            if (s->offset == s->outbound.length) { gin_phoenix_bytes_free(&s->outbound); s->offset = 0; }
        }
        if (operations == GIN_CONNECT_OPERATIONS_MAX || bytes == GIN_CONNECT_IO_MAX) break;
        c = curl_ws_recv(s->curl, buffer,
            GIN_CONNECT_IO_MAX - bytes < sizeof(buffer) ? GIN_CONNECT_IO_MAX - bytes : sizeof(buffer), &n, &meta);
        operations++; bytes += n;
        if (c == CURLE_OK) {
            int flags, final;
            struct gin_phoenix_frame *frame = NULL;
            enum gin_phoenix_code pc;
            if (!meta) return fail(s, GIN_CONNECT_TRANSPORT);
            flags = meta->flags; final = meta->bytesleft == 0 && !(flags & CURLWS_CONT);
            progress = 1;
            if (flags & CURLWS_CLOSE) return fail(s, GIN_CONNECT_CLOSED);
            if (flags & CURLWS_BINARY) return fail(s, GIN_CONNECT_MALFORMED);
            if (flags & CURLWS_TEXT) {
                pc = gin_phoenix_stream_feed(s->stream, buffer, n, final, &frame);
                if (pc != GIN_PHOENIX_OK && pc != GIN_PHOENIX_NEED_MORE)
                    return fail(s, pc == GIN_PHOENIX_LIMIT ? GIN_CONNECT_LIMIT : pc == GIN_PHOENIX_NOMEM ? GIN_CONNECT_NOMEM : GIN_CONNECT_MALFORMED);
                if (frame) {
                    enum gin_connect_code result = receive_frame(s, frame);
                    gin_phoenix_frame_free(frame);
                    if (result != GIN_CONNECT_OK) return result;
                }
            }
        } else if (c == CURLE_GOT_NOTHING) return fail(s, GIN_CONNECT_CLOSED);
        else if (c != CURLE_AGAIN) return fail(s, curl_error(c));
        if (progress) continue;
        if (polled || !max_wait_ms || !remaining(&end)) break;
        {
            struct pollfd p;
            long wait = remaining(&end);
            int rc;
            if (s->state == GIN_CONNECT_JOINING && remaining(&s->deadline) < wait) wait = remaining(&s->deadline);
            p.fd = s->socket; p.events = POLLIN | (s->outbound.data ? POLLOUT : 0); p.revents = 0;
            polled = 1;
            do {
                rc = poll(&p, 1, (int)wait);
                wait = remaining(&end);
                if (s->state == GIN_CONNECT_JOINING && remaining(&s->deadline) < wait) wait = remaining(&s->deadline);
            } while (rc < 0 && errno == EINTR && wait > 0);
            if (rc < 0 && errno != EINTR) return fail(s, GIN_CONNECT_TRANSPORT);
            if (rc <= 0) break;
            if (p.revents & POLLNVAL) return fail(s, GIN_CONNECT_TRANSPORT);
        }
    }
    if (s->state == GIN_CONNECT_JOINING && !remaining(&s->deadline)) return fail(s, GIN_CONNECT_TIMEOUT);
    return GIN_CONNECT_OK;
}
void gin_connect_close(struct gin_connect *s)
{ if (s) { transport_close(s); s->state = GIN_CONNECT_STOPPED; s->result.code = GIN_CONNECT_CLOSED; } }
void gin_connect_free(struct gin_connect *s)
{
    if (!s) return;
    transport_close(s); gin_phoenix_stream_free(s->stream);
    gin_credential_clear(&s->credential);
    free(s->origin); free(s->ca); wipe(s, sizeof(*s)); free(s);
}
enum gin_connect_state gin_connect_get_state(const struct gin_connect *s)
{ return s ? s->state : GIN_CONNECT_STOPPED; }
struct gin_connect_result gin_connect_last_result(const struct gin_connect *s)
{ struct gin_connect_result r = {0}; if (s) return s->result; r.code = GIN_CONNECT_INVALID; return r; }
size_t gin_connect_topic_count(const struct gin_connect *s) { return s ? s->count : 0; }
const struct gin_connect_topic_status *gin_connect_topic_at(const struct gin_connect *s, size_t i)
{ return s && i < s->count ? &s->topics[i].status : NULL; }
const char *gin_connect_error(enum gin_connect_code c)
{
    static const char *const errors[] = {"ok", "invalid", "out-of-memory", "limit", "unavailable", "preflight",
        "unauthorized", "scope", "forbidden", "upgrade-required", "tls", "timeout", "handshake", "transport",
        "malformed", "join-rejected", "closed"};
    return (unsigned)c < sizeof(errors)/sizeof(errors[0]) ? errors[c] : "unknown-connect-error";
}
