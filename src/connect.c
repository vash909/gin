#include "gin_connect.h"
#include "gin_liveness.h"
#include "liveness_values.h"
#include "connect_values.h"
#include "gin_command.h"
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
    enum { WIRE_IDLE, WIRE_APPLICATION, WIRE_HEARTBEAT } writer;
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
    int selection_count, selection_ok, header_limit, header_complete;
    struct gin_query_result query;
    struct timespec query_deadline;
    int config_protocol, root_protocol, in_callback, query_written, session_expired;
    struct gin_liveness_settings liveness;
    int configured, started, cancelled, transient;
    unsigned retries_used;
    enum gin_liveness_reason reason;
    struct timespec retry_due;
    struct timespec observed;
    int have_observed, clock_bad;
    enum gin_heartbeat_state heartbeat;
    /* Independent storage: application/query bytes and receipts never alias it. */
    struct timespec heartbeat_due, heartbeat_deadline;
    char heartbeat_frame[GIN_LIVENESS_HEARTBEAT_FRAME_MAX];
    char heartbeat_ref[GIN_PHOENIX_REF_CAP];
    size_t heartbeat_length, heartbeat_offset;
    enum gin_liveness_reason failure_reason;
    int receive_drained, text_partial;
};
static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
static int time_compare(const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec != b->tv_sec) return a->tv_sec < b->tv_sec ? -1 : 1;
    return (a->tv_nsec > b->tv_nsec) - (a->tv_nsec < b->tv_nsec);
}
static int read_clock(const struct gin_connect *s, struct timespec *t)
{
    return !s->clock_bad && clock_gettime(CLOCK_MONOTONIC, t) == 0 &&
        t->tv_sec >= 0 && t->tv_nsec >= 0 && t->tv_nsec < 1000000000 &&
        (!s->have_observed || time_compare(t, &s->observed) >= 0);
}
static int now(struct gin_connect *s, struct timespec *t)
{
    if (!read_clock(s, t)) { s->clock_bad = 1; return 0; }
    s->observed = *t; s->have_observed = 1; return 1;
}
static long time_left(const struct timespec *end, const struct timespec *t)
{
    uintmax_t seconds;
    long nanos;
    if (time_compare(end, t) <= 0) return 0;
    seconds = (uintmax_t)end->tv_sec - (uintmax_t)t->tv_sec;
    nanos = end->tv_nsec - t->tv_nsec;
    if (nanos < 0) { seconds--; nanos += 1000000000; }
    if (seconds > (uintmax_t)LONG_MAX / 1000 - 1) return LONG_MAX;
    return (long)seconds * 1000 + nanos / 1000000 + (nanos % 1000000 != 0);
}
static long remaining(struct gin_connect *s, const struct timespec *end)
{
    struct timespec t;
    return now(s, &t) ? time_left(end, &t) : 0;
}
static int time_add(struct timespec *out, const struct timespec *t, unsigned long ms)
{
    uintmax_t maximum = (time_t)-1 > 0 ? (uintmax_t)(time_t)-1 :
        UINTMAX_MAX >> (sizeof(uintmax_t) * CHAR_BIT - sizeof(time_t) * CHAR_BIT + 1);
    uintmax_t add = ms / 1000;
    long ns = t->tv_nsec + (long)(ms % 1000) * 1000000;
    if (ns >= 1000000000) { add++; ns -= 1000000000; }
    if (t->tv_sec < 0 || add > maximum || (uintmax_t)t->tv_sec > maximum - add) return 0;
    out->tv_sec = (time_t)((uintmax_t)t->tv_sec + add); out->tv_nsec = ns; return 1;
}
static int deadline(struct gin_connect *s, struct timespec *t, long ms)
{
    struct timespec current;
    if (ms < 0 || !now(s, &current) || !time_add(t, &current, (unsigned long)ms)) {
        s->clock_bad = 1; return 0;
    }
    return 1;
}
enum gin_connect_code gin_connect_set_liveness(struct gin_connect *s,
    const struct gin_liveness_settings *p)
{
    if (!s || !p || s->in_callback || s->configured || s->started || s->state != GIN_CONNECT_IDLE ||
        !p->heartbeat_interval_ms || p->heartbeat_interval_ms > GIN_LIVENESS_INTERVAL_MAX_MS ||
        !p->heartbeat_timeout_ms || p->heartbeat_timeout_ms > GIN_LIVENESS_TIMEOUT_MAX_MS ||
        !p->retry_base_ms || p->retry_base_ms > p->retry_cap_ms ||
        p->retry_cap_ms > GIN_LIVENESS_BACKOFF_MAX_MS || p->max_retries > GIN_LIVENESS_RETRIES_MAX)
        return GIN_CONNECT_INVALID;
    s->liveness = *p; s->configured = 1; return GIN_CONNECT_OK;
}
static struct gin_liveness_status liveness_at(const struct gin_connect *s, const struct timespec *t)
{
    struct gin_liveness_status v = {0};
    long wait;
    if (!s || !s->configured) return v;
    v.reason = s->reason; v.retries_used = s->retries_used;
    v.heartbeat = s->heartbeat;
    v.retries_limit = s->liveness.max_retries; v.socket_epoch = s->result.socket_epoch;
    if (!t) { v.state = GIN_LIVENESS_CLOCK_ERROR; v.reason = GIN_LIVENESS_REASON_CLOCK; return v; }
    if (s->cancelled) v.state = GIN_LIVENESS_CANCELLED;
    else if (s->state != GIN_CONNECT_FAILED) v.state = GIN_LIVENESS_ACTIVE;
    else if (!s->transient) v.state = GIN_LIVENESS_TERMINAL;
    else if (s->retries_used >= s->liveness.max_retries) v.state = GIN_LIVENESS_EXHAUSTED;
    else {
        wait = time_left(&s->retry_due, t); v.retry_wait_ms = (unsigned)wait;
        v.state = s->query.outcome != GIN_QUERY_NONE ? GIN_LIVENESS_RECEIPT_BLOCKED :
            wait ? GIN_LIVENESS_WAITING : GIN_LIVENESS_DUE;
    }
    return v;
}
struct gin_liveness_status gin_connect_liveness_status(const struct gin_connect *s)
{
    struct timespec t;
    return liveness_at(s, s && s->configured && read_clock(s, &t) ? &t : NULL);
}
static enum gin_connect_code start_attempt(struct gin_connect *s);
static enum gin_connect_code fail(struct gin_connect *s, enum gin_connect_code c);
enum gin_retry_step gin_connect_retry(struct gin_connect *s)
{
    struct gin_liveness_status v;
    struct timespec t;
    if (!s || s->in_callback) return GIN_RETRY_INVALID;
    if (!s->configured) return GIN_RETRY_DISABLED;
    if (!now(s, &t)) { (void)fail(s, GIN_CONNECT_CLOCK); return GIN_RETRY_CLOCK_ERROR; }
    v = liveness_at(s, &t);
    switch (v.state) {
    case GIN_LIVENESS_CLOCK_ERROR: s->clock_bad = 1; (void)fail(s, GIN_CONNECT_CLOCK); return GIN_RETRY_CLOCK_ERROR;
    case GIN_LIVENESS_DISABLED: return GIN_RETRY_DISABLED;
    case GIN_LIVENESS_CANCELLED: return GIN_RETRY_CANCELLED;
    case GIN_LIVENESS_TERMINAL: return GIN_RETRY_TERMINAL;
    case GIN_LIVENESS_EXHAUSTED: return GIN_RETRY_EXHAUSTED;
    case GIN_LIVENESS_RECEIPT_BLOCKED: return GIN_RETRY_BLOCKED;
    case GIN_LIVENESS_DUE: break;
    default: return GIN_RETRY_NOT_DUE;
    }
    s->retries_used++; (void)start_attempt(s); return GIN_RETRY_ATTEMPTED;
}
struct gin_query_result gin_connect_query_result(const struct gin_connect *s)
{ struct gin_query_result r = {0}; return s ? s->query : r; }
int gin_connect_query_take(struct gin_connect *s, struct gin_query_result *out)
{
    if (!s || !out || s->in_callback) return -1;
    if (s->query.outcome == GIN_QUERY_NONE || s->query.outcome == GIN_QUERY_PENDING) return 0;
    *out = s->query; memset(&s->query, 0, sizeof(s->query)); return 1;
}
enum gin_query_code gin_connect_open_query(struct gin_connect *s,
    const struct gin_topology *topology, const char *slug, const char *nick,
    const char *chantypes, enum gin_query_mode mode, uint64_t id, unsigned timeout_ms)
{
    struct gin_query_result q = {0};
    struct gin_command command;
    struct gin_phoenix_fields f;
    struct gin_phoenix_refs refs;
    struct gin_phoenix_bytes bytes = {NULL, 0};
    struct timespec end;
    const struct gin_topology_network *network;
    unsigned char record[7 + GIN_COMMAND_NICK_MAX + 1];
    size_t n;
    json_t *payload;
    char *json;
    enum gin_phoenix_code code;
    if (!s || s->in_callback) return GIN_QUERY_INVALID;
    if (s->configured && (s->heartbeat == GIN_HEARTBEAT_QUEUED ||
        s->heartbeat == GIN_HEARTBEAT_WRITING ||
        (s->heartbeat == GIN_HEARTBEAT_SCHEDULED && !remaining(s, &s->heartbeat_due)))) {
        if (s->clock_bad) { (void)fail(s, GIN_CONNECT_CLOCK); return GIN_QUERY_TRANSPORT; }
        return GIN_QUERY_BUSY;
    }
    if (s->query.outcome != GIN_QUERY_NONE || s->outbound.data) return GIN_QUERY_BUSY;
    if (s->state != GIN_CONNECT_READY || !s->result.socket_epoch ||
        s->topics[0].status.state != GIN_CONNECT_TOPIC_JOINED ||
        !s->topics[0].status.join_ref || !s->topics[0].ref[0]) return GIN_QUERY_UNAVAILABLE;
    if (!id || !timeout_ms || timeout_ms > 60000 || !nick || !slug || !chantypes) return GIN_QUERY_INVALID;
    if (mode != GIN_QUERY_MODE_PLAIN) return GIN_QUERY_UNSUPPORTED;
    if (s->config_protocol < 4 || s->root_protocol < 4) return GIN_QUERY_UNSUPPORTED;
    for (n = 0; n <= GIN_COMMAND_NICK_MAX && nick[n]; n++) {}
    if (n > GIN_COMMAND_NICK_MAX) return GIN_QUERY_LIMIT;
    memcpy(record, "/query ", 7); memcpy(record + 7, nick, n);
    if (gin_command_parse(record, 7 + n, chantypes, &command) != GIN_COMMAND_OK ||
        command.kind != GIN_COMMAND_QUERY) return GIN_QUERY_INVALID;
    network = gin_topology_find_slug(topology, slug);
    if (!network || network->id <= 0) return GIN_QUERY_INVALID;
    if (!deadline(s, &end, (long)timeout_ms)) { (void)fail(s, GIN_CONNECT_CLOCK); return GIN_QUERY_TRANSPORT; }
    refs = s->refs;
    code = gin_phoenix_refs_next(&refs, q.request_ref, sizeof(q.request_ref));
    if (code != GIN_PHOENIX_OK) return GIN_QUERY_LIMIT;
    memcpy(q.join_ref, s->topics[0].ref, sizeof(q.join_ref));
    payload = json_pack("{s:I,s:s}", "network_id", (json_int_t)network->id, "target_nick", nick);
    if (!payload) return GIN_QUERY_NOMEM;
    json = json_dumps(payload, JSON_COMPACT); json_decref(payload);
    if (!json) return GIN_QUERY_NOMEM;
    f.join_ref = q.join_ref; f.ref = q.request_ref; f.topic = s->topics[0].text;
    f.event = "open_query_window"; f.payload.data = (const unsigned char *)json; f.payload.length = strlen(json);
    code = gin_phoenix_encode(&f, &bytes); free(json);
    if (code != GIN_PHOENIX_OK) return code == GIN_PHOENIX_NOMEM ? GIN_QUERY_NOMEM : GIN_QUERY_LIMIT;
    q.operation_id = id; q.socket_epoch = s->result.socket_epoch; q.outcome = GIN_QUERY_PENDING;
    s->refs = refs; s->query = q; s->query_deadline = end; s->query_written = 0;
    s->outbound = bytes; s->offset = 0;
    return GIN_QUERY_OK;
}
static void query_abort(struct gin_connect *s, enum gin_query_code code)
{
    if (s->query.outcome != GIN_QUERY_PENDING) return;
    s->query.code = code;
    s->query.outcome = s->query_written ? GIN_QUERY_UNCERTAIN : GIN_QUERY_NOT_SENT;
}
static void transport_close(struct gin_connect *s)
{
    size_t i;
    s->receive_drained = s->text_partial = 0;
    if (s->curl) curl_easy_cleanup(s->curl);
    s->curl = NULL;
    {
        struct curl_slist *h;
        for (h = s->headers; h; h = h->next) if (h->data) wipe(h->data, strlen(h->data));
        curl_slist_free_all(s->headers); s->headers = NULL;
    }
    s->socket = CURL_SOCKET_BAD;
    gin_phoenix_bytes_free(&s->outbound); s->offset = 0;
    s->writer = WIRE_IDLE;
    s->heartbeat = GIN_HEARTBEAT_OFF;
    s->heartbeat_length = s->heartbeat_offset = 0;
    wipe(s->heartbeat_frame, sizeof(s->heartbeat_frame));
    wipe(s->heartbeat_ref, sizeof(s->heartbeat_ref));
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
    if (s->clock_bad) c = GIN_CONNECT_CLOCK;
    query_abort(s, c == GIN_CONNECT_TIMEOUT ? GIN_QUERY_TIMEOUT :
        c == GIN_CONNECT_MALFORMED ? GIN_QUERY_MALFORMED :
        c == GIN_CONNECT_CLOSED ? GIN_QUERY_CLOSED : GIN_QUERY_TRANSPORT);
    if (s->configured) {
        unsigned delay_ms;
        s->transient = c == GIN_CONNECT_TRANSPORT || c == GIN_CONNECT_TIMEOUT || c == GIN_CONNECT_CLOSED;
        s->reason = c == GIN_CONNECT_CLOCK ? GIN_LIVENESS_REASON_CLOCK :
            c == GIN_CONNECT_CLOSED ? GIN_LIVENESS_REASON_PEER_CLOSE :
            c == GIN_CONNECT_SESSION_EXPIRED ? GIN_LIVENESS_REASON_SESSION_SEVERED :
            s->transient ? GIN_LIVENESS_REASON_IO : GIN_LIVENESS_REASON_LOCAL;
        if (s->failure_reason && c != GIN_CONNECT_CLOCK) s->reason = s->failure_reason;
        if (c == GIN_CONNECT_PREFLIGHT) {
            s->transient = s->result.protocol == GIN_PROTOCOL_TRANSPORT || s->result.protocol == GIN_PROTOCOL_TIMEOUT;
            s->reason = s->result.protocol == GIN_PROTOCOL_TLS ? GIN_LIVENESS_REASON_TLS :
                s->result.protocol == GIN_PROTOCOL_OK ? GIN_LIVENESS_REASON_PROTOCOL : GIN_LIVENESS_REASON_PREFLIGHT;
        } else if (s->result.auth_attempted && s->result.auth != GIN_AUTH_OK && c != GIN_CONNECT_CLOCK) {
            s->transient = s->result.auth == GIN_AUTH_TRANSPORT || s->result.auth == GIN_AUTH_TIMEOUT;
            s->reason = s->result.auth == GIN_AUTH_TLS ? GIN_LIVENESS_REASON_TLS : GIN_LIVENESS_REASON_AUTH;
        } else if (c == GIN_CONNECT_TLS) s->reason = GIN_LIVENESS_REASON_TLS;
        else if (c == GIN_CONNECT_MALFORMED || c == GIN_CONNECT_HANDSHAKE ||
            c == GIN_CONNECT_JOIN_REJECTED || c == GIN_CONNECT_UPGRADE_REQUIRED) s->reason = GIN_LIVENESS_REASON_PROTOCOL;
        else if (c == GIN_CONNECT_UNAUTHORIZED || c == GIN_CONNECT_SCOPE || c == GIN_CONNECT_FORBIDDEN)
            s->reason = GIN_LIVENESS_REASON_AUTH;
        if (s->failure_reason == GIN_LIVENESS_REASON_PROTOCOL || s->failure_reason == GIN_LIVENESS_REASON_LOCAL)
            s->transient = 0;
        if (s->transient && s->retries_used < s->liveness.max_retries &&
            gin_liveness_delay(s->liveness.retry_base_ms, s->liveness.retry_cap_ms,
                s->retries_used + 1u, &delay_ms) && !deadline(s, &s->retry_due, (long)delay_ms)) {
            c = GIN_CONNECT_CLOCK; s->transient = 0; s->reason = GIN_LIVENESS_REASON_CLOCK;
        }
    }
    transport_close(s); s->state = GIN_CONNECT_FAILED; s->result.code = c; return c;
}
static enum gin_connect_code local_transport_failure(struct gin_connect *s)
{
    s->failure_reason = GIN_LIVENESS_REASON_LOCAL; return fail(s, GIN_CONNECT_TRANSPORT);
}
static enum gin_connect_code heartbeat_arm(struct gin_connect *s)
{
    if (!s->configured) return GIN_CONNECT_OK;
    if (!deadline(s, &s->heartbeat_due, (long)s->liveness.heartbeat_interval_ms))
        return fail(s, GIN_CONNECT_CLOCK);
    s->heartbeat = GIN_HEARTBEAT_SCHEDULED; return GIN_CONNECT_OK;
}
static enum gin_connect_code timers(struct gin_connect *s)
{
    struct timespec t, hb_end;
    const struct timespec *expired = NULL;
    enum gin_liveness_reason reason = GIN_LIVENESS_REASON_NONE;
    int hb = s->configured && s->heartbeat != GIN_HEARTBEAT_OFF;
    if (!now(s, &t)) return fail(s, GIN_CONNECT_CLOCK);
    if (hb) {
        /* The budget begins at scheduled due, not late service or first write. */
        if (!time_add(&hb_end, &s->heartbeat_due, s->liveness.heartbeat_timeout_ms)) {
            s->clock_bad = 1; return fail(s, GIN_CONNECT_CLOCK);
        }
        s->heartbeat_deadline = hb_end;
    }
    if (s->query.outcome == GIN_QUERY_PENDING && time_compare(&s->query_deadline, &t) <= 0) {
        expired = &s->query_deadline; reason = GIN_LIVENESS_REASON_QUERY_TIMEOUT;
    }
    if (hb && time_compare(&hb_end, &t) <= 0 && (!expired || time_compare(&hb_end, expired) < 0)) {
        expired = &hb_end; reason = GIN_LIVENESS_REASON_HEARTBEAT_TIMEOUT;
    }
    if (s->state == GIN_CONNECT_JOINING && time_compare(&s->deadline, &t) <= 0 &&
        (!expired || time_compare(&s->deadline, expired) < 0)) {
        expired = &s->deadline; reason = GIN_LIVENESS_REASON_JOIN_TIMEOUT;
    }
    if (expired) { s->failure_reason = reason; return fail(s, GIN_CONNECT_TIMEOUT); }
    if (hb && s->heartbeat == GIN_HEARTBEAT_SCHEDULED && time_compare(&s->heartbeat_due, &t) <= 0) {
        struct gin_phoenix_refs refs = s->refs;
        int n;
        if (gin_phoenix_refs_next(&refs, s->heartbeat_ref, sizeof(s->heartbeat_ref)) != GIN_PHOENIX_OK)
            return fail(s, GIN_CONNECT_LIMIT);
        n = snprintf(s->heartbeat_frame, sizeof(s->heartbeat_frame),
            "[null,\"%s\",\"phoenix\",\"heartbeat\",{}]", s->heartbeat_ref);
        if (n < 0 || (size_t)n >= sizeof(s->heartbeat_frame)) return fail(s, GIN_CONNECT_LIMIT);
        s->refs = refs; s->heartbeat_length = (size_t)n; s->heartbeat_offset = 0;
        s->heartbeat = GIN_HEARTBEAT_QUEUED;
    }
    return GIN_CONNECT_OK;
}
static long poll_budget(struct gin_connect *s, const struct timespec *end)
{
    long wait = remaining(s, end), n;
    if (s->state == GIN_CONNECT_JOINING && (n = remaining(s, &s->deadline)) < wait) wait = n;
    if (s->query.outcome == GIN_QUERY_PENDING && (n = remaining(s, &s->query_deadline)) < wait) wait = n;
    if (s->heartbeat != GIN_HEARTBEAT_OFF &&
        (n = remaining(s, s->heartbeat == GIN_HEARTBEAT_SCHEDULED ? &s->heartbeat_due : &s->heartbeat_deadline)) < wait) wait = n;
    return wait > INT_MAX ? INT_MAX : wait;
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
    if (n >= 5 && !memcmp(p, "HTTP/", 5)) { s->selection_count = 0; s->selection_ok = 0; s->header_complete = 0; }
    if (n == 2 && p[0] == '\r' && p[1] == '\n') s->header_complete = 1;
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
    if (c == CURLE_OUT_OF_MEMORY) return GIN_CONNECT_NOMEM;
    if (c == CURLE_PEER_FAILED_VERIFICATION || c == CURLE_SSL_CONNECT_ERROR ||
        c == CURLE_SSL_CACERT_BADFILE || c == CURLE_SSL_CRL_BADFILE ||
        c == CURLE_SSL_ISSUER_ERROR || c == CURLE_SSL_PINNEDPUBKEYNOTMATCH ||
        c == CURLE_SSL_CLIENTCERT || c == CURLE_SSL_CERTPROBLEM ||
        c == CURLE_SSL_INVALIDCERTSTATUS) return GIN_CONNECT_TLS;
    if (c == CURLE_COULDNT_RESOLVE_HOST || c == CURLE_COULDNT_CONNECT ||
        c == CURLE_SEND_ERROR || c == CURLE_RECV_ERROR || c == CURLE_GOT_NOTHING ||
        c == CURLE_PARTIAL_FILE) return GIN_CONNECT_TRANSPORT;
    if (c == CURLE_WEIRD_SERVER_REPLY || c == CURLE_TOO_MANY_REDIRECTS) return GIN_CONNECT_HANDSHAKE;
    return GIN_CONNECT_UNAVAILABLE;
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
    if (!deadline(s, &s->deadline, s->settings.join_ms)) return fail(s, GIN_CONNECT_TRANSPORT);
    return GIN_CONNECT_OK;
}
enum gin_connect_code gin_connect_start(struct gin_connect *s)
{
    if (!s || s->in_callback || s->session_expired) return GIN_CONNECT_INVALID;
    s->receive_drained = 0;
    if (s->configured && s->started)
        return gin_connect_retry(s) == GIN_RETRY_ATTEMPTED ? s->result.code : GIN_CONNECT_INVALID;
    if (s->configured && s->cancelled) return GIN_CONNECT_INVALID;
    return start_attempt(s);
}
static enum gin_connect_code start_attempt(struct gin_connect *s)
{
    struct gin_protocol_info info;
    struct gin_credential identity;
    char url[GIN_CREDENTIAL_ORIGIN_MAX + 100], offer[GIN_WS_PROTOCOL_CAP], line[GIN_WS_PROTOCOL_CAP + 32];
    size_t n, i;
    CURLcode c;
    if (!s || s->in_callback || s->session_expired || s->query.outcome != GIN_QUERY_NONE || s->state == GIN_CONNECT_CONNECTING || s->state == GIN_CONNECT_JOINING || s->state == GIN_CONNECT_READY)
        return GIN_CONNECT_INVALID;
    if (!s->configured) { s->clock_bad = 0; s->have_observed = 0; }
    s->started = 1;
    if (s->result.socket_epoch == UINT64_MAX) return fail(s, GIN_CONNECT_LIMIT);
    {
        uint64_t epoch = s->result.socket_epoch + 1u;
        transport_close(s);
        memset(&s->result, 0, sizeof(s->result));
        s->result.socket_epoch = epoch;
    }
    s->state = GIN_CONNECT_CONNECTING;
    s->failure_reason = GIN_LIVENESS_REASON_NONE; s->reason = GIN_LIVENESS_REASON_NONE;
    /* Reuse no request references or transport state across explicit attempts. */
    if (!available()) return fail(s, GIN_CONNECT_UNAVAILABLE);
    s->result.protocol = gin_protocol_fetch(&s->settings.http, &s->settings.policy, &info);
    if (s->result.protocol != GIN_PROTOCOL_OK) return fail(s, GIN_CONNECT_PREFLIGHT);
    s->config_protocol = info.server_protocol; s->root_protocol = 0;
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
    s->selection_count = 0; s->selection_ok = 0; s->header_bytes = 0; s->header_limit = 0; s->header_complete = 0;
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
        return c != CURLE_OK ? fail(s, curl_error(c)) : local_transport_failure(s);
    if (s->header_limit) return fail(s, GIN_CONNECT_LIMIT);
    /* libcurl rejects a completed non-101 upgrade with HTTP_RETURNED_ERROR;
     * discard deliberately aborts a rejection body. Neither exception forgives
     * TLS, timeout, partial-transfer or local failures behind a provisional status. */
    if (c != CURLE_OK && !(s->header_complete && s->result.http_status != 101 &&
        (c == CURLE_HTTP_RETURNED_ERROR || c == CURLE_WRITE_ERROR))) return fail(s, curl_error(c));
    if (s->result.http_status == 403) return fail(s, GIN_CONNECT_FORBIDDEN);
    if (s->result.http_status == 426) return fail(s, GIN_CONNECT_UPGRADE_REQUIRED);
    if (s->result.http_status != 101 && s->result.http_status != 0) return fail(s, GIN_CONNECT_HANDSHAKE);
    if (c != CURLE_OK) return fail(s, GIN_CONNECT_HANDSHAKE);
    if (s->result.http_status != 101 || s->selection_count != 1 || !s->selection_ok)
        return fail(s, GIN_CONNECT_HANDSHAKE);
    if (curl_easy_getinfo(s->curl, CURLINFO_ACTIVESOCKET, &s->socket) != CURLE_OK || s->socket == CURL_SOCKET_BAD)
        return local_transport_failure(s);
    s->pending = 0; s->offset = 0; s->state = GIN_CONNECT_JOINING;
    if (heartbeat_arm(s) != GIN_CONNECT_OK) return s->result.code;
    return queue_join(s);
}
static enum gin_connect_code receive_frame(struct gin_connect *s, struct gin_phoenix_frame *frame)
{
    const struct gin_phoenix_fields *f = gin_phoenix_frame_fields(frame);
    size_t i;
    if (timers(s) != GIN_CONNECT_OK) return s->result.code;
    if (s->heartbeat >= GIN_HEARTBEAT_QUEUED && !f->join_ref &&
        gin_phoenix_reply_matches(frame, "phoenix", NULL, s->heartbeat_ref)) {
        const struct gin_phoenix_reply *r = gin_phoenix_frame_reply(frame);
        if (s->heartbeat != GIN_HEARTBEAT_WAITING_REPLY || r->status != GIN_PHOENIX_REPLY_OK)
            return fail(s, GIN_CONNECT_MALFORMED);
        s->heartbeat_length = s->heartbeat_offset = 0;
        return heartbeat_arm(s);
    }
    if (s->count && s->result.socket_epoch &&
        s->topics[0].status.state == GIN_CONNECT_TOPIC_JOINED &&
        s->topics[0].status.join_ref && !strcmp(f->topic, s->topics[0].text) &&
        (!f->join_ref || !strcmp(f->join_ref, s->topics[0].ref)) &&
        !strcmp(f->event, "event")) {
        json_t *payload = json_loadb((const char *)f->payload.data, f->payload.length,
            JSON_REJECT_DUPLICATES, NULL);
        json_t *kind = payload ? json_object_get(payload, "kind") : NULL;
        json_t *code = payload ? json_object_get(payload, "code") : NULL;
        int parsed = payload != NULL;
        int known = json_is_string(kind) && !strcmp(json_string_value(kind), "web_session_severed");
        int invalid = known && !json_is_string(code);
        int expired = known && json_is_string(code) && !strcmp(json_string_value(code), "rate_limit_flood");
        json_decref(payload);
        if (!parsed || invalid) return fail(s, GIN_CONNECT_MALFORMED);
        if (expired) { s->session_expired = 1; return fail(s, GIN_CONNECT_SESSION_EXPIRED); }
    }
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
                if (valid) s->root_protocol = (int)json_integer_value(v);
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
    if (s->query.outcome == GIN_QUERY_PENDING &&
        s->query.socket_epoch == s->result.socket_epoch &&
        s->topics[0].status.state == GIN_CONNECT_TOPIC_JOINED &&
        !strcmp(s->query.join_ref, s->topics[0].ref) &&
        gin_phoenix_reply_matches(frame, s->topics[0].text, s->query.join_ref, s->query.request_ref)) {
        const struct gin_phoenix_reply *r = gin_phoenix_frame_reply(frame);
        if (!remaining(s, &s->query_deadline)) return fail(s, GIN_CONNECT_TIMEOUT);
        if (s->outbound.data) return fail(s, GIN_CONNECT_MALFORMED);
        if (r->status == GIN_PHOENIX_REPLY_UNKNOWN) return fail(s, GIN_CONNECT_MALFORMED);
        s->query.code = r->status == GIN_PHOENIX_REPLY_OK ? GIN_QUERY_OK : GIN_QUERY_REMOTE;
        s->query.outcome = r->status == GIN_PHOENIX_REPLY_OK ? GIN_QUERY_ACCEPTED : GIN_QUERY_REMOTE_FAILURE;
    }
    if (!strcmp(f->event, "phx_reply")) return GIN_CONNECT_OK;
    for (i = 0; i < s->count; i++) {
        struct topic *t = &s->topics[i];
        if (strcmp(t->text, f->topic) || !t->status.join_ref ||
            (f->join_ref && strcmp(t->ref, f->join_ref))) continue;
        if (!strcmp(f->event, "phx_error") || !strcmp(f->event, "phx_close")) {
            s->failure_reason = GIN_LIVENESS_REASON_PROTOCOL; return fail(s, GIN_CONNECT_CLOSED);
        }
        if (t->status.state == GIN_CONNECT_TOPIC_JOINED && s->callback) {
            s->in_callback = 1; s->callback(s->user, frame, s->result.socket_epoch); s->in_callback = 0;
        }
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
    if (s) s->receive_drained = 0;
    if (!s || s->in_callback || max_wait_ms > GIN_CONNECT_SERVICE_MAX_MS) return GIN_CONNECT_INVALID;
    if (s->state == GIN_CONNECT_IDLE || s->state == GIN_CONNECT_CONNECTING) return GIN_CONNECT_INVALID;
    if (s->state != GIN_CONNECT_JOINING && s->state != GIN_CONNECT_READY) return s->result.code;
    if (!deadline(s, &end, (long)max_wait_ms)) return fail(s, GIN_CONNECT_TRANSPORT);

    while (operations < GIN_CONNECT_OPERATIONS_MAX && bytes < GIN_CONNECT_IO_MAX) {
        unsigned char buffer[16384];
        size_t n = 0;
        int progress = 0;
        CURLcode c;
        const struct curl_ws_frame *meta = NULL;
        s->receive_drained = 0;
        if (timers(s) != GIN_CONNECT_OK) return s->result.code;
        if ((s->heartbeat == GIN_HEARTBEAT_QUEUED || s->heartbeat == GIN_HEARTBEAT_WRITING) &&
            s->writer != WIRE_APPLICATION) {
            size_t left = s->heartbeat_length - s->heartbeat_offset;
            s->writer = WIRE_HEARTBEAT;
            c = curl_ws_send(s->curl, s->heartbeat_frame + s->heartbeat_offset, left, &n, 0, CURLWS_TEXT);
            operations++;
            if (n > left) return local_transport_failure(s);
            if (c != CURLE_OK && c != CURLE_AGAIN) return fail(s, curl_error(c));
            s->heartbeat_offset += n; bytes += n; progress = n != 0;
            s->heartbeat = s->heartbeat_offset == s->heartbeat_length ?
                GIN_HEARTBEAT_WAITING_REPLY : s->heartbeat_offset ? GIN_HEARTBEAT_WRITING : GIN_HEARTBEAT_QUEUED;
            if (s->heartbeat == GIN_HEARTBEAT_WAITING_REPLY) s->writer = WIRE_IDLE;
        } else if (s->outbound.data) {
            size_t left = s->outbound.length - s->offset;
            /* Even AGAIN/0 can retain libcurl's encoder and buffered payload. */
            s->writer = WIRE_APPLICATION;
            c = curl_ws_send(s->curl, s->outbound.data + s->offset, left, &n, 0, CURLWS_TEXT);
            operations++;
            if (s->query.outcome == GIN_QUERY_PENDING && n) s->query_written = 1;
            if (n > left) return local_transport_failure(s);
            if (c != CURLE_OK && c != CURLE_AGAIN) return fail(s, curl_error(c));
            s->offset += n; bytes += n; progress = n != 0;
            if (s->offset == s->outbound.length) {
                gin_phoenix_bytes_free(&s->outbound); s->offset = 0; s->writer = WIRE_IDLE;
            }
        }
        if (operations == GIN_CONNECT_OPERATIONS_MAX || bytes == GIN_CONNECT_IO_MAX) break;
        c = curl_ws_recv(s->curl, buffer,
            GIN_CONNECT_IO_MAX - bytes < sizeof(buffer) ? GIN_CONNECT_IO_MAX - bytes : sizeof(buffer), &n, &meta);
        operations++; bytes += n;
        if (c == CURLE_OK) {
            int flags, final;
            struct gin_phoenix_frame *frame = NULL;
            enum gin_phoenix_code pc;
            if (!meta) return local_transport_failure(s);
            flags = meta->flags; final = meta->bytesleft == 0 && !(flags & CURLWS_CONT);
            progress = 1;
            if (flags & CURLWS_CLOSE) return fail(s, GIN_CONNECT_CLOSED);
            if (flags & CURLWS_BINARY) return fail(s, GIN_CONNECT_MALFORMED);
            if (flags & CURLWS_TEXT) {
                s->text_partial = !final;
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
        else s->receive_drained = !s->text_partial;
        if (progress) continue;
        if (polled || !max_wait_ms || !remaining(s, &end)) break;
        {
            struct pollfd p;
            long wait = poll_budget(s, &end);
            int rc;
            p.fd = s->socket; p.events = POLLIN | ((s->outbound.data || s->heartbeat == GIN_HEARTBEAT_QUEUED ||
                s->heartbeat == GIN_HEARTBEAT_WRITING) ? POLLOUT : 0); p.revents = 0;
            polled = 1;
            do {
                rc = poll(&p, 1, (int)wait);
                wait = poll_budget(s, &end);
            } while (rc < 0 && errno == EINTR && wait > 0);
            if (rc < 0 && errno != EINTR) return local_transport_failure(s);
            if (rc <= 0) break;
            s->receive_drained = 0;
            if (p.revents & POLLNVAL) return local_transport_failure(s);
        }
    }
    return timers(s);
}
void gin_connect_close(struct gin_connect *s)
{
    if (!s || s->in_callback) return;
    if (s->configured) { s->cancelled = 1; s->reason = GIN_LIVENESS_REASON_EXPLICIT_CLOSE; }
    query_abort(s, GIN_QUERY_CLOSED); transport_close(s);
    s->state = GIN_CONNECT_STOPPED; s->result.code = GIN_CONNECT_CLOSED;
}
void gin_connect_free(struct gin_connect *s)
{
    if (!s) return;
    transport_close(s); gin_phoenix_stream_free(s->stream);
    gin_credential_clear(&s->credential);
    free(s->origin); free(s->ca); wipe(s, sizeof(*s)); free(s);
}
enum gin_connect_state gin_connect_get_state(const struct gin_connect *s)
{ return s ? s->state : GIN_CONNECT_STOPPED; }
int gin_connect_receive_drained(const struct gin_connect *s)
{ return s && (s->state == GIN_CONNECT_JOINING || s->state == GIN_CONNECT_READY) &&
    s->result.socket_epoch && s->receive_drained && !s->text_partial; }
struct gin_connect_result gin_connect_last_result(const struct gin_connect *s)
{ struct gin_connect_result r = {0}; if (s) return s->result; r.code = GIN_CONNECT_INVALID; return r; }
size_t gin_connect_topic_count(const struct gin_connect *s) { return s ? s->count : 0; }
const struct gin_connect_topic_status *gin_connect_topic_at(const struct gin_connect *s, size_t i)
{ return s && i < s->count ? &s->topics[i].status : NULL; }
const char *gin_connect_error(enum gin_connect_code c)
{
    static const char *const errors[] = {"ok", "invalid", "out-of-memory", "limit", "unavailable", "preflight",
        "unauthorized", "scope", "forbidden", "upgrade-required", "tls", "timeout", "handshake", "transport",
        "malformed", "join-rejected", "closed", "session-expired", "clock"};
    return (unsigned)c < sizeof(errors)/sizeof(errors[0]) ? errors[c] : "unknown-connect-error";
}
