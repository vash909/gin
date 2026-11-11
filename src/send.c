#include "gin_send.h"
#include "gin_fs.h"
#include "send_values.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}
/* Read existing descriptors only: opening/closing the lock inode loses locks. */
static int held_root(const struct gin_root *r)
{
    struct stat d, l, named;
    if (!r || r->rootfd < 0 || r->lockfd < 0 ||
        fstat(r->rootfd, &d) || fstat(r->lockfd, &l) ||
        fstatat(r->rootfd, ".gin.lock", &named, AT_SYMLINK_NOFOLLOW)) return 0;
    return S_ISDIR(d.st_mode) && (d.st_mode & 07777) == 0700 &&
        d.st_uid == geteuid() && S_ISREG(l.st_mode) &&
        (l.st_mode & 07777) == 0600 && l.st_uid == geteuid() && l.st_nlink == 1 &&
        l.st_dev == named.st_dev && l.st_ino == named.st_ino &&
        S_ISREG(named.st_mode) && (fcntl(r->lockfd, F_GETFL) & O_ACCMODE) == O_RDWR;
}
struct gin_send_result gin_send_plain(const struct gin_root *root,
    const struct gin_send_settings *s, const struct gin_credential *credential,
    const struct gin_send_route *route, uint64_t id, const unsigned char *body, size_t length)
{
    struct gin_send_result r = {0};
    struct gin_protocol_info info;
    struct gin_credential verified;
    struct gin_http_settings http;
    struct gin_http_request request;
    struct gin_http_result response;
    char *path = NULL, *url = NULL;
    unsigned char *json = NULL, *reply = NULL;
    size_t json_length = 0, url_length;
    r.request_id = id; r.code = GIN_SEND_INVALID;
    r.protocol = GIN_PROTOCOL_INVALID; r.auth = GIN_AUTH_INVALID;
    memset(&verified, 0, sizeof(verified));
    /* All lexical checks, including exact stored origin, precede even fstat. */
    if (!s || !id || !root || !gin_http_origin_valid(s->auth.origin) ||
        s->auth.connect_ms <= 0 || s->auth.total_ms <= 0 ||
        s->auth.connect_ms > s->auth.total_ms || s->auth.total_ms > GIN_HTTP_TIMEOUT_MAX ||
        s->protocol.client_protocol <= 0 || s->protocol.required_server_protocol <= 0 ||
        gin_credential_validate(credential) != GIN_CREDENTIAL_OK ||
        strcmp(s->auth.origin, credential->origin)) return r;
    r.code = gin_send_payload_build(route, body, length, &path, &json, &json_length);
    if (r.code != GIN_SEND_OK) goto done;
    url_length = strlen(s->auth.origin) + strlen(path);
    if (url_length >= GIN_SEND_URL_MAX) { r.code = GIN_SEND_LIMIT; goto done; }
    if (!held_root(root)) { r.code = GIN_SEND_INVALID; goto done; }
    url = malloc(url_length + 1); reply = malloc(GIN_SEND_RESPONSE_MAX);
    if (!url || !reply) { r.code = GIN_SEND_NOMEM; goto done; }
    strcpy(url, s->auth.origin); strcat(url, path);
    r.protocol = gin_protocol_fetch(&s->auth, &s->protocol, &info);
    if (r.protocol != GIN_PROTOCOL_OK) { r.code = GIN_SEND_PROTOCOL; goto done; }
    r.auth_attempted = 1;
    r.auth = gin_auth_identity(&s->auth, credential, &verified);
    if (r.auth != GIN_AUTH_OK) { r.code = GIN_SEND_AUTH; goto done; }
    http.url = url; http.ca_file = s->auth.ca_file;
    http.connect_ms = s->auth.connect_ms; http.total_ms = s->auth.total_ms;
    http.header_limit = GIN_HTTP_HEADER_MAX; http.allow_loopback_http = 0;
    request.settings = &http; request.method = GIN_HTTP_POST_JSON;
    request.bearer = verified.token; request.bearer_origin = verified.origin;
    request.json_body = json; request.json_length = json_length;
    r.post_attempted = 1; r.outcome = GIN_SEND_UNCERTAIN;
    response = gin_http_request(&request, reply, GIN_SEND_RESPONSE_MAX);
    r.http_status = response.http_status;
    switch (response.code) {
    case GIN_HTTP_OK:
        r.code = gin_send_reply_parse(response.http_status, reply, response.body_length,
            route->network_slug, &r.message_id, &r.outcome);
        break;
    case GIN_HTTP_STATUS: r.code = GIN_SEND_REMOTE; r.outcome = GIN_SEND_REMOTE_FAILURE; break;
    case GIN_HTTP_TLS: r.code = GIN_SEND_TLS; break;
    case GIN_HTTP_TIMEOUT: r.code = GIN_SEND_TIMEOUT; break;
    case GIN_HTTP_LIMIT: r.code = GIN_SEND_LIMIT; break;
    default: r.code = GIN_SEND_TRANSPORT; break;
    }
done:
    gin_credential_clear(&verified);
    if (reply) { wipe(reply, GIN_SEND_RESPONSE_MAX); free(reply); }
    if (json) { wipe(json, json_length); free(json); }
    if (url) { wipe(url, url_length); free(url); }
    free(path);
    return r;
}
const char *gin_send_error(enum gin_send_code code)
{
    static const char *const names[] = {"ok", "invalid", "unsupported", "limit", "out-of-memory",
        "protocol", "auth", "remote", "malformed", "tls", "timeout", "transport"};
    return (unsigned)code < sizeof(names)/sizeof(names[0]) ? names[code] : "unknown-send-error";
}
const char *gin_send_outcome_name(enum gin_send_outcome outcome)
{
    static const char *const names[] = {"not-attempted", "accepted-persisted", "accepted-no-persist",
        "remote-failure", "uncertain"};
    return (unsigned)outcome < sizeof(names)/sizeof(names[0]) ? names[outcome] : "unknown-send-outcome";
}
