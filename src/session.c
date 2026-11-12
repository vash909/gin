#include "gin_session.h"
#include "gin_fs.h"
#include "gin_http.h"
#include "gin_credential.h"
#include "gin_protocol.h"
#include "gin_topology.h"
#include "gin_connect.h"
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int session_active;
static volatile sig_atomic_t session_stopping;
static volatile sig_atomic_t session_wake = -1;
struct owner {
    struct gin_root root;
    int wake[2], status, input, installed, masked, http;
    sigset_t blocked, saved_mask;
    struct sigaction saved_int, saved_term, saved_pipe;
};
static void stop_signal(int number)
{
    int saved = errno;
    unsigned char byte = (unsigned char)number;
    session_stopping = 1;
    if (session_wake >= 0) {
        ssize_t ignored = write((int)session_wake, &byte, 1);
        (void)ignored;
    }
    errno = saved;
}
static int private_fd(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    return flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) < 0 ? -1 : 0;
}
static void owner_init(struct owner *o)
{
    memset(o, 0, sizeof(*o));
    o->root.rootfd = o->root.lockfd = -1;
    o->wake[0] = o->wake[1] = o->status = o->input = -1;
}
static enum gin_session_code owner_begin(struct owner *o)
{
    struct sigaction action;
    sigset_t pending;
    if (session_active) return GIN_SESSION_BUSY;
    if (sigpending(&pending) < 0) return GIN_SESSION_IO;
    /* Ignoring SIGPIPE discards a pending instance, so refuse before mutation. */
    if (sigismember(&pending, SIGPIPE) == 1) return GIN_SESSION_BUSY;
    session_active = 1;
    sigemptyset(&o->blocked);
    sigaddset(&o->blocked, SIGINT); sigaddset(&o->blocked, SIGTERM);
    sigaddset(&o->blocked, SIGPIPE);
    if (sigprocmask(SIG_BLOCK, &o->blocked, &o->saved_mask) < 0)
        return GIN_SESSION_IO;
    o->masked = 1;
    if (sigpending(&pending) < 0) return GIN_SESSION_IO;
    if (sigismember(&pending, SIGPIPE) == 1) return GIN_SESSION_BUSY;
    if (pipe(o->wake) < 0 || private_fd(o->wake[0]) < 0 ||
        private_fd(o->wake[1]) < 0) return GIN_SESSION_IO;
    memset(&action, 0, sizeof(action));
    action.sa_handler = stop_signal; action.sa_mask = o->blocked;
    session_stopping = 0; session_wake = o->wake[1];
    if (sigaction(SIGINT, &action, &o->saved_int) < 0) return GIN_SESSION_IO;
    o->installed = 1;
    if (sigaction(SIGTERM, &action, &o->saved_term) < 0) return GIN_SESSION_IO;
    o->installed = 2;
    action.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &action, &o->saved_pipe) < 0) return GIN_SESSION_IO;
    o->installed = 3;
    if (sigprocmask(SIG_SETMASK, &o->saved_mask, NULL) < 0) return GIN_SESSION_IO;
    return GIN_SESSION_OK;
}
static void close_owned(int *fd, enum gin_session_code *code)
{
    if (*fd >= 0) {
        int result = close(*fd);
        *fd = -1;
        if (result < 0 && *code == GIN_SESSION_OK) *code = GIN_SESSION_CLEANUP;
    }
}
static enum gin_session_code owner_end(struct owner *o, enum gin_session_code code)
{
    if (o->http) gin_http_global_cleanup();
    close_owned(&o->status, &code); close_owned(&o->input, &code);
    if (o->masked && sigprocmask(SIG_BLOCK, &o->blocked, NULL) < 0 &&
        code == GIN_SESSION_OK) code = GIN_SESSION_CLEANUP;
    if (o->installed >= 3 && sigaction(SIGPIPE, &o->saved_pipe, NULL) < 0 &&
        code == GIN_SESSION_OK) code = GIN_SESSION_CLEANUP;
    if (o->installed >= 2 && sigaction(SIGTERM, &o->saved_term, NULL) < 0 &&
        code == GIN_SESSION_OK) code = GIN_SESSION_CLEANUP;
    if (o->installed >= 1 && sigaction(SIGINT, &o->saved_int, NULL) < 0 &&
        code == GIN_SESSION_OK) code = GIN_SESSION_CLEANUP;
    session_wake = -1;
    close_owned(&o->wake[1], &code); close_owned(&o->wake[0], &code);
    if (gin_root_release(&o->root) < 0 && code == GIN_SESSION_OK)
        code = GIN_SESSION_CLEANUP;
    session_active = 0;
    if (o->masked && sigprocmask(SIG_SETMASK, &o->saved_mask, NULL) < 0 &&
        code == GIN_SESSION_OK) code = GIN_SESSION_CLEANUP;
    return code;
}

static int ca_safe(const char *, const struct gin_root *);

static int borrow_pipe(int fd, int writing)
{
    struct stat st;
    int flags;
    if (fstat(fd, &st) < 0 || !S_ISFIFO(st.st_mode)) return -1;
    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || !(flags & O_NONBLOCK) ||
        (flags & O_ACCMODE) != (writing ? O_WRONLY : O_RDONLY)) return -1;
    if (writing && fpathconf(fd, _PC_PIPE_BUF) < 6) return -1;
    return fcntl(fd, F_DUPFD_CLOEXEC, 3);
}
struct metadata_row { unsigned char *data; size_t length; uint64_t epoch; };
struct metadata {
    struct gin_connect *connection;
    struct metadata_row rows[GIN_SESSION_METADATA_ROWS_MAX];
    size_t count, bytes;
    int count_overflow, bytes_overflow, nomem;
};
/* The codec has already validated JSON. Decode only bounded ASCII strings;
 * escaped key/kind spellings remain equivalent, nested decoys are ignored. */
static int ascii_string(const unsigned char *p, size_t n, size_t *at,
    char *out, size_t capacity)
{
    size_t used = 0;
    int fits = 1;
    if (*at >= n || p[(*at)++] != '"') return 0;
    while (*at < n) {
        unsigned c = p[(*at)++];
        if (c == '"') { out[used] = 0; return fits; }
        if (c == '\\') {
            if (*at >= n) return 0;
            c = p[(*at)++];
            if (c == 'u') {
                unsigned value = 0, i;
                for (i = 0; i < 4; ++i) {
                    unsigned h;
                    if (*at >= n) return 0;
                    h = p[(*at)++];
                    value = value * 16u + (h <= '9' ? h - '0' :
                        h <= 'F' ? h - 'A' + 10u : h - 'a' + 10u);
                }
                c = value;
            } else if (c == 'b' || c == 'f' || c == 'n' || c == 'r' || c == 't') c = 0;
        }
        if (c > 127u || !c) fits = 0;
        if (used + 1u < capacity) out[used++] = (char)c;
        else fits = 0;
    }
    return 0;
}
static int query_payload(const unsigned char *p, size_t n)
{
    size_t i = 0;
    unsigned depth = 0;
    while (i < n) {
        unsigned c = p[i];
        if (c == '"') {
            char key[32], value[32];
            int match = ascii_string(p, n, &i, key, sizeof(key));
            if (depth == 1 && match && !strcmp(key, "kind")) {
                size_t j = i;
                while (j < n && (p[j] == ' ' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r')) ++j;
                if (j < n && p[j++] == ':') {
                    while (j < n && (p[j] == ' ' || p[j] == '\t' || p[j] == '\n' || p[j] == '\r')) ++j;
                    return ascii_string(p,n,&j,value,sizeof(value)) && !strcmp(value,"query_windows_list");
                }
            }
            continue;
        }
        if (c == '{' || c == '[') ++depth;
        if (c == '}' || c == ']') { if (!depth) return 0; --depth; }
        ++i;
    }
    return 0;
}
static void metadata_copy(struct metadata *m, const unsigned char *data,
    size_t length, uint64_t epoch)
{
    struct metadata_row *row;
    if (m->count >= GIN_SESSION_METADATA_ROWS_MAX) m->count_overflow = 1;
    if (length > GIN_SESSION_METADATA_BYTES_MAX - m->bytes) {
        m->bytes_overflow = 1; m->bytes = GIN_SESSION_METADATA_BYTES_MAX;
    } else m->bytes += length;
    if (m->count_overflow || m->bytes_overflow || m->nomem) return;
    row = &m->rows[m->count];
    row->data = malloc(length);
    if (!row->data) { m->nomem = 1; return; }
    memcpy(row->data, data, length);
    row->length = length; row->epoch = epoch;
    ++m->count;
}
static void metadata_frame(void *context, const struct gin_phoenix_frame *frame, uint64_t epoch)
{
    struct metadata *m = context;
    const struct gin_phoenix_fields *f = gin_phoenix_frame_fields(frame);
    const struct gin_connect_topic_status *root = gin_connect_topic_at(m->connection, 0);
    if (!f || !root || root->state != GIN_CONNECT_TOPIC_JOINED || !epoch ||
        epoch != gin_connect_last_result(m->connection).socket_epoch ||
        strcmp(f->topic, root->topic) || strcmp(f->event, "event") ||
        (f->join_ref && (!root->join_ref || strcmp(f->join_ref, root->join_ref))) ||
        !query_payload(f->payload.data, f->payload.length)) return;
    metadata_copy(m,f->payload.data,f->payload.length,epoch);
}
static void metadata_clear(struct metadata *m)
{
    size_t i;
    for (i=0; i<m->count; ++i) {
        volatile unsigned char *p = m->rows[i].data;
        size_t n = m->rows[i].length;
        while (n--) *p++ = 0;
        free(m->rows[i].data);
    }
    m->count = m->bytes = 0;
}
static int owner_stop(struct owner *o, enum gin_session_code *code)
{
    unsigned char byte;
    ssize_t n;
    if (session_stopping) return 1;
    if (o->input < 0) return 0;
    n = read(o->input, &byte, 1);
    if (n == 0) return 1;
    if (n > 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        *code = GIN_SESSION_INPUT; return 1;
    }
    return 0;
}
static int deadline_state(const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    return now.tv_sec > deadline->tv_sec ||
        (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}
static enum gin_session_code publish_ready(struct owner *o)
{
    unsigned attempt;
    if (o->status < 0) return GIN_SESSION_OK;
    for (attempt=0; attempt<3; ++attempt) {
        ssize_t n = write(o->status, "READY\n", 6);
        if (n == 6) return GIN_SESSION_OK;
        if (n < 0 && errno == EINTR) continue;
        return GIN_SESSION_STATUS;
    }
    return GIN_SESSION_STATUS;
}
enum gin_session_code gin_session_run(const struct gin_session_settings *settings,
    int fd, int eof)
{
    struct gin_session_settings copy;
    const struct gin_session_settings *s = &copy;
    struct owner o;
    struct gin_credential credential;
    struct gin_topology *topology = NULL;
    struct gin_connect *connection = NULL;
    struct metadata metadata;
    struct gin_connect_settings connect_settings;
    struct gin_connect_subscription subscription;
    struct gin_name name;
    struct timespec deadline;
    const struct gin_protocol_policy policy = {GIN_SESSION_CLIENT_PROTOCOL, GIN_SESSION_SERVER_MIN};
    enum gin_session_code code = GIN_SESSION_OK;
    int ready = 0;
    int64_t network_id = 0;
    uint64_t snapshot_epoch = 0;
    if (!settings || !gin_session_settings_valid(settings) || (eof != 0 && eof != 1))
        return GIN_SESSION_INVALID;
    copy = *settings;
    if (session_active) return GIN_SESSION_BUSY;
    owner_init(&o); memset(&credential, 0, sizeof(credential));
    memset(&metadata, 0, sizeof(metadata));
    if (fd != -1 && (fd < 3 || (o.status = borrow_pipe(fd, 1)) < 0)) return GIN_SESSION_STATUS;
    if (eof && (o.input = borrow_pipe(STDIN_FILENO, 0)) < 0) { code = GIN_SESSION_INPUT; goto done; }
    code = owner_begin(&o);
    if (code != GIN_SESSION_OK) goto done;
    if (owner_stop(&o, &code)) goto done;
    if (clock_gettime(CLOCK_MONOTONIC, &deadline) < 0) { code=GIN_SESSION_IO; goto done; }
    deadline.tv_sec += s->startup_ms / 1000;
    deadline.tv_nsec += (s->startup_ms % 1000) * 1000000;
    if (deadline.tv_nsec >= 1000000000) { ++deadline.tv_sec; deadline.tv_nsec -= 1000000000; }
    if (gin_root_acquire(s->local.root, &o.root) < 0) { code=GIN_SESSION_IO; goto done; }
    if (!ca_safe(s->ca_file, &o.root)) { code=GIN_SESSION_TLS; goto done; }
    if (gin_credential_load(&o.root,s->origin,&credential) != GIN_CREDENTIAL_OK) {
        code=GIN_SESSION_CREDENTIAL; goto done;
    }
    if (owner_stop(&o,&code)) goto done;
    if (gin_http_global_init() != GIN_HTTP_OK) { code=GIN_SESSION_IO; goto done; }
    o.http=1;
    connect_settings.http.origin=s->origin;
    connect_settings.http.ca_file=*s->ca_file?s->ca_file:NULL;
    connect_settings.http.connect_ms=s->connect_ms; connect_settings.http.total_ms=s->total_ms;
    connect_settings.policy=policy; connect_settings.join_ms=s->join_ms;
    {
        enum gin_topology_code tc=gin_topology_fetch(&connect_settings.http,&policy,&credential,s->local.chantypes,&topology);
        if (owner_stop(&o,&code)) goto done;
        if (tc != GIN_TOPOLOGY_OK) { code=tc == GIN_TOPOLOGY_TLS?GIN_SESSION_TLS:GIN_SESSION_TOPOLOGY; goto done; }
    }
    {
        const struct gin_topology_network *network=gin_topology_find_slug(topology,s->local.network);
        const struct gin_topology_window *window;
        if (!network || gin_name_prepare(s->local.network,s->local.target,s->local.chantypes,&name) < 0) {
            code=GIN_SESSION_ROUTE; goto done;
        }
        network_id=network->id;
        window=gin_topology_find_window(topology,network_id,name.canonical_target);
        if (name.kind == GIN_WINDOW_CHANNEL && (!window || window->role != GIN_REMOTE_CHANNEL || !window->joined)) {
            code=GIN_SESSION_ROUTE; goto done;
        }
    }
    { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; goto done; } }
    subscription.network=s->local.network; subscription.target=s->local.target;
    {
        enum gin_connect_code cc=gin_connect_new(&connect_settings,&credential,&subscription,1,metadata_frame,&metadata,&connection);
        if (cc != GIN_CONNECT_OK) { code=cc==GIN_CONNECT_NOMEM?GIN_SESSION_NOMEM:GIN_SESSION_CONNECTION; goto done; }
    }
    metadata.connection=connection; gin_credential_clear(&credential);
    if (owner_stop(&o,&code)) goto done;
    {
        enum gin_connect_code cc=gin_connect_start(connection);
        if (owner_stop(&o,&code)) goto done;
        if (cc != GIN_CONNECT_OK) { code=cc==GIN_CONNECT_TLS?GIN_SESSION_TLS:GIN_SESSION_CONNECTION; goto done; }
    }
    for (;;) {
        enum gin_connect_code cc;
        size_t i;
        if (owner_stop(&o,&code)) break;
        if (!ready) { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; break; } }
        cc=gin_connect_service(connection,GIN_SESSION_SERVICE_MS);
        /* Callback failures are already latched; stop otherwise beats the
         * just-returned aggregate network failure, not an earlier owner error. */
        if (metadata.count_overflow || metadata.bytes_overflow) { code=GIN_SESSION_LIMIT; break; }
        if (metadata.nomem) { code=GIN_SESSION_NOMEM; break; }
        if (owner_stop(&o,&code)) break;
        if (cc != GIN_CONNECT_OK) { code=GIN_SESSION_CONNECTION; break; }
        for (i=0; i<metadata.count; ++i) {
            struct gin_topology *next=NULL;
            enum gin_topology_code tc;
            if (metadata.rows[i].epoch != gin_connect_last_result(connection).socket_epoch) continue;
            tc=gin_topology_apply_queries(topology,metadata.rows[i].data,metadata.rows[i].length,&next);
            if (tc != GIN_TOPOLOGY_OK) { code=tc==GIN_TOPOLOGY_NOMEM?GIN_SESSION_NOMEM:GIN_SESSION_TOPOLOGY; break; }
            gin_topology_free(topology); topology=next; snapshot_epoch=metadata.rows[i].epoch;
        }
        metadata_clear(&metadata);
        if (code != GIN_SESSION_OK || owner_stop(&o,&code)) break;
        if (!ready) { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; break; } }
        if (name.kind == GIN_WINDOW_NONCHANNEL && gin_topology_queries_known(topology)) {
            const struct gin_topology_window *w=gin_topology_find_window(topology,network_id,name.canonical_target);
            if (!w || w->role != GIN_REMOTE_QUERY) { code=GIN_SESSION_ROUTE; break; }
        }
        if (!ready && gin_connect_get_state(connection)==GIN_CONNECT_READY &&
            gin_connect_receive_drained(connection)) {
            int joined=1;
            uint64_t epoch=gin_connect_last_result(connection).socket_epoch;
            for (i=0;i<gin_connect_topic_count(connection);++i) {
                const struct gin_connect_topic_status *topic=gin_connect_topic_at(connection,i);
                if (!topic || topic->state != GIN_CONNECT_TOPIC_JOINED) joined=0;
            }
            if (joined && epoch && (name.kind==GIN_WINDOW_CHANNEL ||
                (gin_topology_queries_known(topology) && snapshot_epoch==epoch))) {
                code=publish_ready(&o); if (code != GIN_SESSION_OK) break;
                ready=1;
            }
        }
    }
done:
    gin_connect_free(connection); metadata_clear(&metadata);
    gin_topology_free(topology); gin_credential_clear(&credential);
    return owner_end(&o,code);
}
static void secret_wipe(void *buffer, size_t length)
{
    volatile unsigned char *p = buffer;
    while (length--) *p++ = 0;
}
static int bearer_metadata(const struct stat *s)
{
    return S_ISREG(s->st_mode) && (s->st_mode & 07777) == 0600 &&
        s->st_uid == geteuid() && s->st_nlink == 1;
}
static enum gin_session_code read_bearer(const char *path, char *token)
{
    struct stat before, after;
    size_t used = 0, i;
    int fd, padding = 0;
    enum gin_session_code code = GIN_SESSION_IO;
    if (lstat(path, &before) < 0 || !bearer_metadata(&before)) return code;
    fd = open(path, O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
    if (fd < 0) return code;
    if (fstat(fd, &after) < 0 || !bearer_metadata(&after) ||
        before.st_dev != after.st_dev || before.st_ino != after.st_ino) goto done;
    while (used < GIN_CREDENTIAL_TOKEN_MAX + 1u) {
        ssize_t n = read(fd, token + used, GIN_CREDENTIAL_TOKEN_MAX + 1u - used);
        if (n < 0) { if (errno == EINTR) continue; goto done; }
        if (!n) break;
        used += (size_t)n;
    }
    code = GIN_SESSION_CREDENTIAL;
    if (!used || used > GIN_CREDENTIAL_TOKEN_MAX || token[0] == '=') goto done;
    for (i = 0; i < used; ++i) {
        unsigned char c = (unsigned char)token[i];
        if (c == '=') { padding = 1; continue; }
        if (padding || !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || (c != 0 && strchr("-._~+/", c)))) goto done;
    }
    token[used] = 0;
    code = GIN_SESSION_OK;
done:
    if (close(fd) < 0 && code == GIN_SESSION_OK) code = GIN_SESSION_IO;
    return code;
}
/* Inspect only: opening a lock alias and then closing it loses POSIX locks. */
static int ca_safe(const char *path, const struct gin_root *root)
{
    struct stat ca, lock;
    if (!path || !*path) return 1;
    if (stat(path, &ca) < 0 || !S_ISREG(ca.st_mode) ||
        (ca.st_mode & 0022) || (ca.st_uid != 0 && ca.st_uid != geteuid())) return 0;
    if (root && root->lockfd >= 0) {
        if (fstat(root->lockfd, &lock) < 0) return 0;
        if (ca.st_dev == lock.st_dev && ca.st_ino == lock.st_ino) return 0;
    }
    return 1;
}
enum gin_session_code gin_session_import(const char *root, const char *origin,
    const char *ca, const char *file)
{
    char token[GIN_CREDENTIAL_TOKEN_MAX + 2u] = {0};
    struct owner o;
    struct gin_credential credential;
    struct gin_auth_settings auth;
    const struct gin_protocol_policy policy = {GIN_SESSION_CLIENT_PROTOCOL, GIN_SESSION_SERVER_MIN};
    struct gin_protocol_auth_result result;
    enum gin_session_code code;
    if (!root || !origin || !file || !gin_http_origin_valid(origin) ||
        gin_validate(root, "net", "#one", "#") < 0) return GIN_SESSION_INVALID;
    if (session_active) return GIN_SESSION_BUSY;
    code = read_bearer(file, token);
    if (code != GIN_SESSION_OK) { secret_wipe(token, sizeof(token)); return code; }
    owner_init(&o); memset(&credential, 0, sizeof(credential));
    code = owner_begin(&o);
    if (code != GIN_SESSION_OK) goto done;
    if (session_stopping) goto done;
    if (gin_root_acquire(root, &o.root) < 0) { code = GIN_SESSION_IO; goto done; }
    if (!ca_safe(ca, &o.root)) { code = GIN_SESSION_TLS; goto done; }
    if (session_stopping) goto done;
    if (gin_http_global_init() != GIN_HTTP_OK) { code = GIN_SESSION_IO; goto done; }
    o.http = 1;
    auth.origin = origin; auth.ca_file = ca && *ca ? ca : NULL;
    auth.connect_ms = 10000; auth.total_ms = 20000;
    result = gin_protocol_resume(&auth, &policy, token, &credential);
    if (session_stopping) goto done;
    if (result.protocol != GIN_PROTOCOL_OK) {
        code = result.protocol == GIN_PROTOCOL_TLS ? GIN_SESSION_TLS : GIN_SESSION_PROTOCOL;
        goto done;
    }
    if (!result.auth_attempted || result.auth != GIN_AUTH_OK) {
        code = result.auth_attempted && result.auth == GIN_AUTH_TLS ? GIN_SESSION_TLS : GIN_SESSION_AUTH;
        goto done;
    }
    /* A save I/O error may follow rename publication; do not retry/rollback. */
    if (gin_credential_save(&o.root, &credential) != GIN_CREDENTIAL_OK)
        code = GIN_SESSION_CREDENTIAL;
done:
    secret_wipe(token, sizeof(token)); gin_credential_clear(&credential);
    return owner_end(&o, code);
}
const char *gin_session_error(enum gin_session_code c)
{
    static const char *const errors[] = {
        "ok", "invalid session settings", "session I/O failure",
        "credential failure", "protocol failure", "authentication failure",
        "TLS failure", "topology failure", "selected route unavailable",
        "connection failure", "startup timeout", "status pipe failure",
        "unsupported session input", "metadata limit", "unsupported session mode",
        "out of memory", "session cleanup failure", "session owner busy"
    };
    return (unsigned)c < sizeof(errors) / sizeof(errors[0]) ? errors[c] :
        "unknown session error";
}
