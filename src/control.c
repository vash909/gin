#include "gin_control.h"
#include "gin_command.h"
#include "gin_fs.h"
#include "gin_naming.h"
#include "send_values.h"
#include <jansson.h>
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
/* Inspect existing descriptors only; another lock-inode close drops locks. */
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
static char *segment(char *p, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' || c == '~')
            *p++ = (char)c;
        else { *p++ = '%'; *p++ = hex[c >> 4]; *p++ = hex[c & 15]; }
    }
    *p = 0;
    return p;
}
static enum gin_control_code auth_error(enum gin_auth_code c)
{
    switch (c) {
    case GIN_AUTH_UNAUTHORIZED: return GIN_CONTROL_UNAUTHORIZED;
    case GIN_AUTH_SCOPE: return GIN_CONTROL_SCOPE;
    case GIN_AUTH_FORBIDDEN: return GIN_CONTROL_FORBIDDEN;
    case GIN_AUTH_THROTTLED: return GIN_CONTROL_THROTTLED;
    case GIN_AUTH_TLS: return GIN_CONTROL_TLS;
    case GIN_AUTH_TIMEOUT: return GIN_CONTROL_TIMEOUT;
    case GIN_AUTH_LIMIT: return GIN_CONTROL_LIMIT;
    case GIN_AUTH_TRANSPORT: return GIN_CONTROL_TRANSPORT;
    default: return GIN_CONTROL_PREFLIGHT;
    }
}
static enum gin_control_code protocol_error(enum gin_protocol_code c)
{
    switch (c) {
    case GIN_PROTOCOL_TLS: return GIN_CONTROL_TLS;
    case GIN_PROTOCOL_TIMEOUT: return GIN_CONTROL_TIMEOUT;
    case GIN_PROTOCOL_LIMIT: return GIN_CONTROL_LIMIT;
    case GIN_PROTOCOL_TRANSPORT: return GIN_CONTROL_TRANSPORT;
    default: return GIN_CONTROL_PREFLIGHT;
    }
}
static enum gin_control_code remote_error(long status)
{
    switch (status) {
    case 401: return GIN_CONTROL_UNAUTHORIZED;
    case 403: return GIN_CONTROL_FORBIDDEN;
    case 426: return GIN_CONTROL_UPGRADE_REQUIRED;
    case 429: return GIN_CONTROL_THROTTLED;
    default: return GIN_CONTROL_REMOTE;
    }
}
static struct gin_control_result control(struct gin_root *root,
    const struct gin_send_settings *s, const struct gin_credential *credential,
    const struct gin_send_route *route, uint64_t id, int part)
{
    struct gin_control_result r = {0};
    struct gin_command command;
    enum gin_command_code parsed;
    char network_component[256];
    struct gin_protocol_policy policy;
    struct gin_protocol_info info;
    struct gin_credential verified = {0};
    struct gin_http_settings http;
    struct gin_http_request request;
    struct gin_http_result response;
    char record[6 + GIN_COMMAND_CHANNEL_MAX + 1];
    char path[GIN_SEND_PATH_MAX + 1], url[GIN_SEND_URL_MAX + 1];
    char *p, *json = NULL;
    unsigned char *reply = NULL;
    size_t n, json_length = 0;
    int64_t ignored_id;
    enum gin_send_outcome ignored_outcome;
    enum gin_send_code ack;
    r.code = GIN_CONTROL_INVALID; r.operation_id = id;
    r.protocol = GIN_PROTOCOL_INVALID; r.auth = GIN_AUTH_INVALID;
    if (!s || !id || !route || !route->network_slug || !route->wire_target || !route->chantypes)
        return r;
    if (route->role != GIN_SEND_ROLE_CHANNEL) return r;
    if (route->mode == GIN_SEND_MODE_UNKNOWN || route->mode == GIN_SEND_MODE_ENCRYPTED) {
        r.code = GIN_CONTROL_UNSUPPORTED; return r;
    }
    if (route->mode != GIN_SEND_MODE_PLAIN) return r;
    if (!gin_http_origin_valid(s->auth.origin) ||
        s->auth.connect_ms <= 0 || s->auth.total_ms <= 0 ||
        s->auth.connect_ms > s->auth.total_ms || s->auth.total_ms > GIN_HTTP_TIMEOUT_MAX ||
        s->protocol.client_protocol <= 0 || s->protocol.required_server_protocol <= 0 ||
        gin_credential_validate(credential) != GIN_CREDENTIAL_OK ||
        strcmp(s->auth.origin, credential->origin)) return r;
    n = strnlen(route->wire_target, GIN_COMMAND_CHANNEL_MAX + 1u);
    if (!n || n > GIN_COMMAND_CHANNEL_MAX) return r;
    memcpy(record, part ? "/part " : "/join ", 6);
    memcpy(record + 6, route->wire_target, n);
    parsed = gin_command_parse((const unsigned char *)record, n + 6, route->chantypes, &command);
    if (parsed != GIN_COMMAND_OK) return r;
    /* Parser owns wire target policy, including lone #/& which need no local
     * window. Network encoding validates only; never put it on the wire. */
    if (gin_network_encode(route->network_slug, network_component, sizeof(network_component)) ||
        !held_root(root)) return r;
    /* Validated slug<=255 and channel<=50; worst-case segment encoding fits. */
    strcpy(path, "/networks/"); p = segment(path + strlen(path), route->network_slug);
    strcpy(p, "/channels"); p += strlen(p);
    if (part) { *p++ = '/'; p = segment(p, route->wire_target); }
    n = strlen(s->auth.origin) + (size_t)(p - path);
    if (n > GIN_SEND_URL_MAX) { r.code = GIN_CONTROL_LIMIT; return r; }
    strcpy(url, s->auth.origin); strcat(url, path);
    if (!part) {
        json_t *object = json_object(), *value;
        if (!object) { r.code = GIN_CONTROL_NOMEM; goto done; }
        value = json_string(route->wire_target);
        if (!value) { json_decref(object); r.code = GIN_CONTROL_NOMEM; goto done; }
        if (json_object_set_new(object, "name", value)) {
            json_decref(object); r.code = GIN_CONTROL_NOMEM; goto done;
        }
        json = json_dumps(object, JSON_COMPACT); json_decref(object);
        if (!json) { r.code = GIN_CONTROL_NOMEM; goto done; }
        json_length = strlen(json);
    }
    reply = malloc(GIN_SEND_RESPONSE_MAX);
    if (!reply) { r.code = GIN_CONTROL_NOMEM; goto done; }
    policy = s->protocol;
    /* Conservative local feature floor, not a live-version qualification. */
    if (policy.required_server_protocol < 4) policy.required_server_protocol = 4;
    r.protocol = gin_protocol_fetch(&s->auth, &policy, &info);
    if (r.protocol != GIN_PROTOCOL_OK) { r.code = protocol_error(r.protocol); goto done; }
    r.auth = gin_auth_identity(&s->auth, credential, &verified);
    if (r.auth != GIN_AUTH_OK) { r.code = auth_error(r.auth); goto done; }
    http.url = url; http.ca_file = s->auth.ca_file;
    http.connect_ms = s->auth.connect_ms; http.total_ms = s->auth.total_ms;
    http.header_limit = GIN_HTTP_HEADER_MAX; http.allow_loopback_http = 0;
    request.settings = &http; request.method = part ? GIN_HTTP_DELETE : GIN_HTTP_POST_JSON;
    request.bearer = verified.token; request.bearer_origin = verified.origin;
    request.json_body = (const unsigned char *)json; request.json_length = json_length;
    r.attempted = 1; r.outcome = GIN_CONTROL_UNCERTAIN;
    response = gin_http_request(&request, reply, GIN_SEND_RESPONSE_MAX);
    r.http_status = response.http_status;
    switch (response.code) {
    case GIN_HTTP_OK:
        /* Gate explicitly: Send's 201 persisted-row semantics do not apply. */
        if (response.http_status != 202) { r.code = GIN_CONTROL_MALFORMED; break; }
        ack = gin_send_reply_parse(202, reply, response.body_length, route->network_slug,
            &ignored_id, &ignored_outcome);
        r.code = ack == GIN_SEND_OK ? GIN_CONTROL_OK :
            ack == GIN_SEND_LIMIT ? GIN_CONTROL_LIMIT : GIN_CONTROL_MALFORMED;
        if (r.code == GIN_CONTROL_OK) r.outcome = GIN_CONTROL_ACCEPTED;
        break;
    case GIN_HTTP_STATUS:
        r.code = remote_error(response.http_status); r.outcome = GIN_CONTROL_REMOTE_FAILURE; break;
    case GIN_HTTP_TLS: r.code = GIN_CONTROL_TLS; break;
    case GIN_HTTP_TIMEOUT: r.code = GIN_CONTROL_TIMEOUT; break;
    case GIN_HTTP_LIMIT: r.code = GIN_CONTROL_LIMIT; break;
    default: r.code = GIN_CONTROL_TRANSPORT; break;
    }
done:
    gin_credential_clear(&verified);
    if (reply) { wipe(reply, GIN_SEND_RESPONSE_MAX); free(reply); }
    if (json) { wipe(json, json_length); free(json); }
    wipe(url, sizeof(url));
    return r;
}
struct gin_control_result gin_control_join(struct gin_root *root,
    const struct gin_send_settings *s, const struct gin_credential *credential,
    const struct gin_send_route *route, uint64_t id)
{ return control(root, s, credential, route, id, 0); }
struct gin_control_result gin_control_part(struct gin_root *root,
    const struct gin_send_settings *s, const struct gin_credential *credential,
    const struct gin_send_route *route, uint64_t id)
{ return control(root, s, credential, route, id, 1); }
const char *gin_control_error(enum gin_control_code code)
{
    static const char *const names[] = {"ok", "invalid", "unsupported", "out-of-memory",
        "limit", "preflight", "unauthorized", "scope", "forbidden", "upgrade-required",
        "throttled", "tls", "timeout", "transport", "remote", "malformed"};
    return (unsigned)code < sizeof(names)/sizeof(names[0]) ? names[code] : "unknown-control-error";
}
