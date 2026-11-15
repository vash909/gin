#include "gin_session.h"
#include "liveness_values.h"
#include "http_control.h"
#include "connect_control.h"
#include "gin_fs.h"
#include "gin_http.h"
#include "gin_credential.h"
#include "gin_protocol.h"
#include "gin_topology.h"
#include "gin_connect.h"
#include "gin_liveness.h"
#include "gin_input.h"
#include "gin_command.h"
#include "gin_send.h"
#include "gin_control.h"
#include "session_operation.h"
#include "session_live_projection.h"
#include "checkpoint.h"
#include "fs_private.h"
#include "gin_history.h"
#include "history_values.h"
#include <time.h>
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int session_active;
static volatile sig_atomic_t session_stopping;
static volatile sig_atomic_t session_wake = -1;
struct owner {
    struct gin_root root;
    struct gin_checkpoint *checkpoint;
    char subject_id[37];
    /* Owned per-attempt staging; scope strings borrow the enclosing session.
     * Deferred ID-less presentation: <=64 records and <=2MiB rendered bytes,
     * plus their opaque bounded record headers and this fixed pointer array.
     * These are additional to the history planner's explicit snapshot budget. */
    struct gin_history_recovery *history;
    struct gin_history_scope history_scope;
    struct gin_render_record *deferred[GIN_LIVE_ROWS_MAX];
    size_t deferred_count, deferred_bytes;
    int64_t processed_anchor;
    int history_pending, starting, new_window;
    struct timespec history_deadline;
    struct gin_topology *history_borrowed_topology;
    int wake[2], status, input, fifo, operations, installed, masked, http, eof_stopping;
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
    o->wake[0] = o->wake[1] = o->status = o->input = o->fifo = o->operations = -1;
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
    close_owned(&o->fifo, &code); close_owned(&o->operations, &code);
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
        ((flags & O_ACCMODE) != (writing ? O_WRONLY : O_RDONLY) &&
         (flags & O_ACCMODE) != O_RDWR)) return -1;
    if (writing && fpathconf(fd, _PC_PIPE_BUF) < 6) return -1;
    return fcntl(fd, F_DUPFD_CLOEXEC, 3);
}
struct metadata {
    struct gin_connect *connection;
    struct gin_live_queue *queue;
    size_t count, bytes;
    int count_overflow, bytes_overflow, nomem;
    int64_t receive_ms;
};
/* The codec has already validated JSON. Decode only bounded ASCII strings;
 * escaped key/kind spellings remain equivalent, nested decoys are ignored. */
static int ascii_string(const unsigned char *p, size_t n, size_t *at,
    char *out, size_t capacity)
{
    size_t used = 0;
    int fits = 1;
    out[0] = 0;
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
static int recognized_payload(const unsigned char *p, size_t n)
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
                    int fits;
                    value[0] = 0;
                    fits = ascii_string(p,n,&j,value,sizeof(value));
                    if (!strncmp(value,"e2e_",4)) return 2;
                    if (!fits) return 0;
                    if (!strcmp(value,"query_windows_list")) return 1;
                    return !strcmp(value,"message") || !strcmp(value,"own_nick_changed") ||
                        !strcmp(value,"joined") || !strcmp(value,"window_pending") ||
                        !strcmp(value,"window_invited") || !strcmp(value,"window_invite_declined") ||
                        !strcmp(value,"join_failed") || !strcmp(value,"kicked") ? 2 : 0;
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
static void metadata_frame(void *context, const struct gin_phoenix_frame *frame, uint64_t epoch)
{
    struct metadata *m = context;
    const struct gin_phoenix_fields *f = gin_phoenix_frame_fields(frame);
    struct gin_live_view view;
    size_t i;
    enum gin_live_code code;
    int recognized;
    if (!f || strcmp(f->event, "event") || !epoch ||
        epoch != gin_connect_last_result(m->connection).socket_epoch) return;
    for (i = 0; i < gin_connect_topic_count(m->connection); ++i) {
        const struct gin_connect_topic_status *source = gin_connect_topic_at(m->connection, i);
        if (!source || strcmp(source->topic, f->topic)) continue;
        if (source->state != GIN_CONNECT_TOPIC_JOINED || !source->join_ref ||
            !*source->join_ref || (f->join_ref && strcmp(f->join_ref, source->join_ref))) return;
        recognized = recognized_payload(f->payload.data, f->payload.length);
        if (!recognized) return;
        memset(&view, 0, sizeof(view));
        view.fields = *f; view.source_join_ref = source->join_ref;
        view.observed_epoch = epoch; view.receive_ms = m->receive_ms; view.source_index = i;
        view.kind = GIN_LIVE_LIFECYCLE;
        if (i == 0 && recognized == 1) {
            view.kind = GIN_LIVE_QUERY_SNAPSHOT;
            if (++m->count > GIN_SESSION_METADATA_ROWS_MAX) m->count_overflow = 1;
            if (f->payload.length > GIN_SESSION_METADATA_BYTES_MAX - m->bytes)
                m->bytes_overflow = 1;
            else m->bytes += f->payload.length;
        }
        if (m->count_overflow || m->bytes_overflow || m->nomem) return;
        code = gin_live_queue_push(m->queue, &view);
        if (code == GIN_LIVE_NOMEM) m->nomem = 1;
        else if (code != GIN_LIVE_OK) m->count_overflow = 1;
        return;
    }
}
static int owner_stop(struct owner *o, enum gin_session_code *code)
{
    unsigned char byte;
    ssize_t n;
    if (session_stopping || o->eof_stopping) return 1;
    if (o->input < 0) return 0;
    n = read(o->input, &byte, 1);
    /* EOF is an observed stop request, not a probe that a later FIFO writer
     * may reverse between nested owner checks. Do not consume later bytes. */
    if (n == 0) { o->eof_stopping=1; return 1; }
    if (n > 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        *code = GIN_SESSION_INPUT; return 1;
    }
    return 0;
}
static int fifo_deadline_start(struct timespec *deadline)
{
    uintmax_t maximum = (time_t)-1 > 0 ? (uintmax_t)(time_t)-1 :
        UINTMAX_MAX >> (sizeof(uintmax_t) * CHAR_BIT - sizeof(time_t) * CHAR_BIT + 1);
    struct timespec now;
    uintmax_t carry;
    long ns;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0 || now.tv_sec < 0 ||
        now.tv_nsec < 0 || now.tv_nsec >= 1000000000) return -1;
    ns = now.tv_nsec + 100000000;
    carry = ns >= 1000000000 ? 1u : 0u;
    if ((uintmax_t)now.tv_sec > maximum - carry) return -1;
    deadline->tv_sec = (time_t)((uintmax_t)now.tv_sec + carry);
    deadline->tv_nsec = ns - (carry ? 1000000000 : 0);
    return 0;
}
/* Shared startup/recovery episode clock; bounded addition, no deadline reset
 * merely because another retry was scheduled. */
static int owner_deadline_start(struct timespec *deadline, long milliseconds)
{
    uintmax_t maximum = (time_t)-1 > 0 ? (uintmax_t)(time_t)-1 :
        UINTMAX_MAX >> (sizeof(uintmax_t) * CHAR_BIT - sizeof(time_t) * CHAR_BIT + 1);
    struct timespec now;
    uintmax_t add;
    long ns;
    if (milliseconds <= 0 || clock_gettime(CLOCK_MONOTONIC,&now)<0 || now.tv_sec<0 ||
        now.tv_nsec<0 || now.tv_nsec>=1000000000) return -1;
    add=(uintmax_t)(milliseconds/1000);
    ns=now.tv_nsec+(milliseconds%1000)*1000000;
    if (ns>=1000000000) { ++add; ns-=1000000000; }
    if (add>maximum || (uintmax_t)now.tv_sec>maximum-add) return -1;
    deadline->tv_sec=(time_t)((uintmax_t)now.tv_sec+add); deadline->tv_nsec=ns;
    return 0;
}
static int deadline_state(const struct timespec *deadline)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    return now.tv_sec > deadline->tv_sec ||
        (now.tv_sec == deadline->tv_sec && now.tv_nsec >= deadline->tv_nsec);
}
/* Wake/optional-stdin polling only: selected FIFO records are never admitted
 * while either initial bootstrap or native recovery is waiting. */
static enum gin_session_code owner_wait_slice(struct owner *o, unsigned wait)
{
    struct pollfd fds[2];
    nfds_t count=1;
    fds[0].fd=o->wake[0]; fds[0].events=POLLIN; fds[0].revents=0;
    if (o->input>=0) {
        fds[1].fd=o->input; fds[1].events=POLLIN; fds[1].revents=0; count=2;
    }
    return poll(fds,count,(int)wait)<0 && errno!=EINTR ? GIN_SESSION_IO : GIN_SESSION_OK;
}
struct owner_http_control {
    struct owner *owner;
    const struct timespec *deadline;
    enum gin_session_code reason;
};
static int owner_http_tick(void *opaque, unsigned *wait)
{
    struct owner_http_control *state=opaque;
    struct timespec now;
    uintmax_t seconds;
    if (owner_stop(state->owner,&state->reason)) return 1;
    if (clock_gettime(CLOCK_MONOTONIC,&now)<0 || now.tv_sec<0 ||
        now.tv_nsec<0 || now.tv_nsec>=1000000000) {
        state->reason=GIN_SESSION_IO; return 1;
    }
    if (now.tv_sec>state->deadline->tv_sec ||
        (now.tv_sec==state->deadline->tv_sec && now.tv_nsec>=state->deadline->tv_nsec)) {
        state->reason=GIN_SESSION_TIMEOUT; return 1;
    }
    seconds=(uintmax_t)state->deadline->tv_sec-(uintmax_t)now.tv_sec;
    if (seconds<=1u) {
        long ns=(long)seconds*1000000000L+state->deadline->tv_nsec-now.tv_nsec;
        unsigned remaining=(unsigned)(ns/1000000L)+(ns%1000000L!=0);
        if (remaining<*wait) *wait=remaining;
    }
    return 0;
}
/* Before native construction, retry only typed transport/timeouts. Additional
 * attempts consume the same requested budget later handed to native liveness.
 * A graceful stop returns OK with no snapshot; the caller must not continue. */
static enum gin_session_code fetch_initial_topology(struct owner *o,
    const struct gin_session_settings *settings, const struct gin_connect_settings *transport,
    const struct gin_credential *credential, struct gin_topology **topology,
    const struct timespec *startup, unsigned *used)
{
    enum gin_session_code code=GIN_SESSION_OK;
    struct owner_http_control state={o,startup,GIN_SESSION_OK};
    struct gin_http_control control={owner_http_tick,&state,0};
    for (;;) {
        enum gin_topology_code tc;
        struct timespec due;
        unsigned delay;
        int d;
        if (owner_stop(o,&code)) return code;
        d=deadline_state(startup);
        if (d) return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT;
        tc=gin_topology_fetch_controlled(&transport->http,&transport->policy,credential,
            settings->local.chantypes,topology,&control);
        if (control.aborted) {
            gin_topology_free(*topology); *topology=NULL; return state.reason;
        }
        if (owner_stop(o,&code)) {
            gin_topology_free(*topology); *topology=NULL; return code;
        }
        d=deadline_state(startup);
        if (d) {
            gin_topology_free(*topology); *topology=NULL;
            return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT;
        }
        if (tc==GIN_TOPOLOGY_OK) return GIN_SESSION_OK;
        gin_topology_free(*topology); *topology=NULL;
        if ((tc!=GIN_TOPOLOGY_TRANSPORT && tc!=GIN_TOPOLOGY_TIMEOUT) ||
            *used>=(unsigned)settings->max_retries)
            return tc==GIN_TOPOLOGY_TLS?GIN_SESSION_TLS:GIN_SESSION_TOPOLOGY;
        if (!gin_liveness_delay((unsigned)settings->retry_base_ms,
                (unsigned)settings->retry_cap_ms,*used+1u,&delay) ||
            owner_deadline_start(&due,(long)delay)<0) return GIN_SESSION_IO;
        for (;;) {
            if (owner_stop(o,&code)) return code;
            d=deadline_state(startup);
            if (d) return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT;
            d=deadline_state(&due);
            if (d<0) return GIN_SESSION_IO;
            if (d) break;
            code=owner_wait_slice(o,GIN_SESSION_SERVICE_MS);
            if (code!=GIN_SESSION_OK) return code;
        }
        /* Charge immediately before the next attempt, never while waiting. */
        ++*used;
    }
}
static int retry_possible(struct gin_connect *connection)
{
    struct gin_liveness_status live=gin_connect_liveness_status(connection);
    return live.state==GIN_LIVENESS_WAITING || live.state==GIN_LIVENESS_DUE;
}
static int history_now(uint64_t *value)
{
    struct timespec now;
    uint64_t seconds;
    if (clock_gettime(CLOCK_MONOTONIC,&now)<0 || now.tv_sec<0 ||
        now.tv_nsec<0 || now.tv_nsec>=1000000000L) return 0;
    seconds=(uint64_t)now.tv_sec;
    if (seconds>(UINT64_MAX-999u)/1000u) return 0;
    *value=seconds*1000u+(uint64_t)now.tv_nsec/1000000u;
    return 1;
}
static enum gin_session_code history_code(enum gin_history_code code)
{
    if (code==GIN_HISTORY_OK) return GIN_SESSION_OK;
    if (code==GIN_HISTORY_NOMEM) return GIN_SESSION_NOMEM;
    if (code==GIN_HISTORY_LIMIT) return GIN_SESSION_LIMIT;
    if (code==GIN_HISTORY_DEADLINE || code==GIN_HISTORY_TIMEOUT) return GIN_SESSION_TIMEOUT;
    if (code==GIN_HISTORY_TLS) return GIN_SESSION_TLS;
    if (code==GIN_HISTORY_UNSUPPORTED) return GIN_SESSION_UNSUPPORTED;
    if (code==GIN_HISTORY_AUTH) return GIN_SESSION_CREDENTIAL;
    if (code==GIN_HISTORY_TRANSPORT) return GIN_SESSION_CONNECTION;
    return GIN_SESSION_ROUTE;
}
static void owner_history_clear(struct owner *owner)
{
    size_t i;
    gin_history_recovery_free(owner->history); owner->history=NULL;
    for (i=0;i<owner->deferred_count;++i) gin_render_record_free(owner->deferred[i]);
    memset(owner->deferred,0,sizeof(owner->deferred));
    owner->deferred_count=owner->deferred_bytes=0;
}
/* Validate and load before admitting output. The stable checkpoint key excludes
 * the transient epoch/nick; this attempt's owned planner freezes both. */
static enum gin_session_code owner_scope(struct owner *owner,
    const struct gin_live_projection *projection, uint64_t epoch,
    struct gin_history_scope *scope)
{
    if (!epoch || !projection->own_nick || !*projection->own_nick)
        return GIN_SESSION_ROUTE;
    memset(scope,0,sizeof(*scope));
    scope->origin=projection->settings->origin;
    scope->subject_id=owner->subject_id;
    scope->network_slug=projection->settings->local.network;
    scope->target=projection->settings->local.target;
    scope->chantypes=projection->settings->local.chantypes;
    scope->own_nick=projection->own_nick;
    scope->network_id=projection->network_id;
    scope->socket_epoch=epoch;
    scope->role=projection->query?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
    scope->encryption=GIN_RENDER_MODE_PLAIN;
    return history_code(gin_history_scope_check(scope));
}
static enum gin_session_code owner_history_begin(struct owner *owner,
    const struct gin_history_scope *scope, int64_t anchor)
{
    uint64_t now;
    if (owner_deadline_start(&owner->history_deadline,60000)<0 || !history_now(&now))
        return GIN_SESSION_IO;
    owner->processed_anchor=anchor;
    owner->history_scope=*scope;
    owner->history_pending=1;
    return history_code(gin_history_recovery_new(scope,anchor,
        GIN_HISTORY_PAGE_ROWS_MAX,now,&owner->history));
}
static enum gin_session_code owner_checkpoint_open(struct owner *owner,
    const struct gin_live_projection *projection, uint64_t epoch, int output)
{
    struct gin_history_scope scope;
    struct gin_checkpoint_progress progress;
    enum gin_checkpoint_code result;
    enum gin_session_code code;
    if (!epoch || !projection->own_nick || !*projection->own_nick)
        return GIN_SESSION_ROUTE;
    if (owner->checkpoint) return GIN_SESSION_OK;
    code=owner_scope(owner,projection,epoch,&scope);
    if (code!=GIN_SESSION_OK) return code;
    result=gin_checkpoint_open(&owner->root,&scope,output,&owner->checkpoint,&progress);
    if (result!=GIN_CHECKPOINT_OK && result!=GIN_CHECKPOINT_UNKNOWN)
        return result==GIN_CHECKPOINT_NOMEM?GIN_SESSION_NOMEM:GIN_SESSION_IO;
    /* Quiet windows start even when the validated entry is missing. */
    return owner_history_begin(owner,&scope,progress.anchor);
}
/* Inspect existing windows before admission. ENOENT starts only a transient
 * unknown-baseline staging attempt: it is NOT validated missing metadata or
 * permission to initialize paths before current remote readiness/route/drain.
 * Every other private traversal failure remains fatal, never repaired. */
static enum gin_session_code owner_existing_checkpoint(struct owner *owner,
    const struct gin_live_projection *projection, uint64_t epoch, int *output)
{
    const struct gin_session_settings *s=projection->settings;
    int window=gin_root_window_open(&owner->root,s->local.network,
        s->local.target,s->local.chantypes);
    if (window<0) {
        struct gin_history_scope scope;
        enum gin_session_code code;
        if (errno!=ENOENT) return GIN_SESSION_IO;
        code=owner_scope(owner,projection,epoch,&scope);
        if (code!=GIN_SESSION_OK) return code;
        owner->new_window=1;
        return owner_history_begin(owner,&scope,0);
    }
    if (close(window)<0) return GIN_SESSION_IO;
    *output=gin_root_output_open(&owner->root,s->local.network,s->local.target,
        s->local.chantypes);
    if (*output<0) return GIN_SESSION_IO;
    return owner_checkpoint_open(owner,projection,epoch,*output);
}
/* Called only after current joined-source, selected-role and receive-drain
 * admission. Creating a legitimate new local window is distinct from repairing
 * an existing incomplete/unsafe window, which failed traversal above. */
static enum gin_session_code owner_new_window_ready(struct owner *owner,
    const struct gin_live_projection *projection, uint64_t epoch, int *output)
{
    const struct gin_session_settings *s=projection->settings;
    struct gin_checkpoint_progress progress;
    enum gin_checkpoint_code result;
    if (!owner->history_pending || !owner->history || owner->checkpoint || *output>=0 ||
        owner->processed_anchor!=0) return GIN_SESSION_IO;
    if (epoch!=owner->history_scope.socket_epoch) return GIN_SESSION_ROUTE;
    if (gin_root_init(&owner->root,s->local.network,s->local.target,s->local.chantypes)<0 ||
        (*output=gin_root_output_open(&owner->root,s->local.network,s->local.target,s->local.chantypes))<0)
        return GIN_SESSION_IO;
    result=gin_checkpoint_open(&owner->root,&owner->history_scope,*output,
        &owner->checkpoint,&progress);
    if (result==GIN_CHECKPOINT_NOMEM) return GIN_SESSION_NOMEM;
    /* Do not overwrite newly discovered state or reset/reload the staged
     * attempt. Cooperating owners share this held root lock throughout. */
    if (result!=GIN_CHECKPOINT_UNKNOWN || progress.anchor!=0 || !progress.unknown_before)
        return GIN_SESSION_IO;
    owner->new_window=0;
    return GIN_SESSION_OK;
}
static enum gin_session_code owner_stage_record(struct owner *owner,
    const struct gin_history_page *page, struct gin_render_record **record)
{
    if (page) {
        uint64_t now;
        const struct gin_history_row *row=gin_history_page_row(page,0);
        if (!row || !history_now(&now)) return GIN_SESSION_IO;
        /* A trusted previous prefix suppresses its old live duplicates only
         * after complete source/row validation. Never derive progress from it. */
        if (row->id>owner->processed_anchor)
            return history_code(gin_history_recovery_live(owner->history,page,now));
    } else if (*record) {
        struct gin_phoenix_view bytes=gin_render_record_bytes(*record);
        if (owner->deferred_count==GIN_LIVE_ROWS_MAX ||
            bytes.length>GIN_LIVE_BYTES_MAX-owner->deferred_bytes) return GIN_SESSION_LIMIT;
        owner->deferred_bytes+=bytes.length;
        owner->deferred[owner->deferred_count++]=*record; *record=NULL;
    }
    return GIN_SESSION_OK;
}
/* No callbacks run during this drain: the current queue is already bounded.
 * Share the same projection/append authority across ordinary and HTTP turns. */
static enum gin_session_code owner_drain_metadata(struct owner *owner,
    struct metadata *metadata, struct gin_live_projection *projection,
    struct gin_topology **topology, int *output, uint64_t *snapshot_epoch)
{
    enum gin_session_code code=GIN_SESSION_OK;
    int completed_output=0;
    while (gin_live_queue_count(metadata->queue)) {
        const struct gin_live_view *view=gin_live_queue_peek(metadata->queue);
        struct gin_topology *next=NULL;
        struct gin_render_record *record=NULL;
        struct gin_history_page *history=NULL;
        int terminal=0;
        size_t appended=0;
        if (view->observed_epoch != gin_connect_last_result(metadata->connection).socket_epoch) {
            code=GIN_SESSION_ROUTE; return code;
        }
        projection->source_target=view->source_index==2 ? projection->settings->local.target :
            view->source_index==3 ? projection->own_nick : NULL;
        code=owner->history_pending ?
            gin_live_project_history(view,projection,*topology,&next,&record,&terminal,
                &owner->history_scope,&history) :
            gin_live_project(view,projection,*topology,&next,&record,&terminal);
        if (code==GIN_SESSION_OK && next) {
            /* The synchronous fetch still borrows its original topology while
             * its outside-curl tick installs newer validated replacements. */
            if (*topology!=owner->history_borrowed_topology) gin_topology_free(*topology);
            *topology=next; next=NULL;
            *snapshot_epoch=view->observed_epoch;
        }
        gin_topology_free(next);
        if (owner->history_pending) {
            if (code==GIN_SESSION_OK && terminal) code=GIN_SESSION_ROUTE;
            if (code==GIN_SESSION_OK) code=owner_stage_record(owner,history,&record);
        } else if (code==GIN_SESSION_OK && record) {
            if (*output < 0) {
                if (gin_root_init(&owner->root,projection->settings->local.network,projection->settings->local.target,projection->settings->local.chantypes)<0 ||
                    (*output=gin_root_output_open(&owner->root,projection->settings->local.network,projection->settings->local.target,projection->settings->local.chantypes))<0)
                    code=GIN_SESSION_IO;
            }
            if (code==GIN_SESSION_OK)
                code=owner_checkpoint_open(owner,projection,view->observed_epoch,*output);
            if (code==GIN_SESSION_OK) {
                /* Existing state must have been loaded by the pre-admission
                 * path; never append over a newly discovered catch-up need. */
                if (owner->history_pending) code=GIN_SESSION_IO;
                else if (gin_render_append(*output,record,&appended)!=GIN_RENDER_OK)
                    code=GIN_SESSION_IO;
                else if (appended) completed_output=1;
            }
        }
        gin_history_page_free(history);
        gin_render_record_free(record);
        (void)gin_live_queue_drop(metadata->queue);
        if (code!=GIN_SESSION_OK) return code;
        if (terminal) { code=GIN_SESSION_ROUTE; return code; }
    }
    metadata->count=metadata->bytes=0;
    if (completed_output) {
        struct gin_checkpoint_write_result result;
        /* One snapshot per fully successful bounded drain, never a live ID or
         * fetch cursor promoted to progress. Errors above deliberately leave
         * partial/crash byte ambiguity unsnapshotted. Append and checkpoint
         * are not atomic; retain the previously established baseline policy. */
        result=gin_checkpoint_commit(owner->checkpoint,owner->processed_anchor);
        /* A failure may already be published: stop, never retry or roll back. */
        if (result.code!=GIN_CHECKPOINT_OK || result.cleanup_failed)
            return GIN_SESSION_IO;
    }
    return GIN_SESSION_OK;
}
struct owner_live_http_control {
    struct owner *owner;
    struct metadata *metadata;
    struct gin_live_projection *projection;
    struct gin_topology **topology;
    int *output;
    uint64_t *snapshot_epoch;
    enum gin_session_code reason;
    int recover_native;
};
static int owner_live_http_tick(void *opaque, unsigned *wait)
{
    struct owner_live_http_control *state=opaque;
    struct metadata *metadata=state->metadata;
    struct timespec wall;
    enum gin_connect_code cc;
    if (owner_stop(state->owner,&state->reason)) return 1;
    if (clock_gettime(CLOCK_REALTIME,&wall)<0 ||
        !gin_live_receive_ms(&wall,&metadata->receive_ms)) {
        state->reason=GIN_SESSION_IO; return 1;
    }
    cc=gin_connect_service(metadata->connection,0u);
    if (metadata->count_overflow || metadata->bytes_overflow) {
        state->reason=GIN_SESSION_LIMIT; return 1;
    }
    if (metadata->nomem) { state->reason=GIN_SESSION_NOMEM; return 1; }
    if (owner_stop(state->owner,&state->reason)) return 1;
    if (cc!=GIN_CONNECT_OK) {
        struct gin_liveness_status live=gin_connect_liveness_status(metadata->connection);
        if (live.state!=GIN_LIVENESS_WAITING && live.state!=GIN_LIVENESS_DUE) {
            state->reason=GIN_SESSION_CONNECTION; return 1;
        }
    }
    state->reason=owner_drain_metadata(state->owner,metadata,state->projection,
        state->topology,state->output,state->snapshot_epoch);
    if (state->reason!=GIN_SESSION_OK) return 1;
    /* Never reconnect or replay a mutation from inside its HTTP controller.
     * The caller publishes its terminal outcome before applying this reason. */
    if (cc!=GIN_CONNECT_OK) {
        state->reason=GIN_SESSION_CONNECTION;
        state->recover_native=1; /* Only the eligible, successfully drained path. */
        return 1;
    }
    if (gin_connect_http_wait_budget(metadata->connection,wait)!=GIN_CONNECT_OK) {
        state->reason=GIN_SESSION_CONNECTION; return 1;
    }
    return 0;
}
struct owner_history_http_control {
    struct owner_live_http_control live;
    struct owner_http_control admission, startup;
};
static int owner_history_http_tick(void *opaque, unsigned *wait)
{
    struct owner_history_http_control *state=opaque;
    if (owner_http_tick(&state->admission,wait)) {
        state->live.reason=state->admission.reason; return 1;
    }
    if (state->live.owner->starting && owner_http_tick(&state->startup,wait)) {
        state->live.reason=state->startup.reason; return 1;
    }
    return owner_live_http_tick(&state->live,wait);
}
/* REST has completed but a productive/partial Native receive batch is not an
 * authority barrier. Continue bounded staging until actual AGAIN/no TEXT, with
 * the same stop/heartbeat/startup/admission limits and no FIFO admission. */
static int owner_history_receive_barrier(struct owner_history_http_control *state)
{
    do {
        unsigned wait=GIN_SESSION_SERVICE_MS;
        if (owner_history_http_tick(state,&wait)) return 0;
        if (gin_connect_receive_drained(state->live.metadata->connection)) return 1;
        state->live.reason=owner_wait_slice(state->live.owner,wait);
        if (state->live.reason!=GIN_SESSION_OK) return 0;
    } while (1);
}
static enum gin_session_code owner_history_publish(struct owner *owner, int output)
{
    const struct gin_history_page *view=gin_history_recovery_view(owner->history);
    struct gin_history_status status=gin_history_recovery_status(owner->history);
    int64_t successful_prefix=owner->processed_anchor;
    size_t i;
    int completed_output=0;
    enum gin_session_code code=GIN_SESSION_OK;
    if (status.state!=GIN_HISTORY_VISIBLE_DRAINED) return GIN_SESSION_LIMIT;
    for (i=0;i<gin_history_page_count(view);++i) {
        const struct gin_history_row *row=gin_history_page_row(view,i);
        struct gin_render_record *record=NULL;
        enum gin_render_code rendered;
        size_t appended=0;
        if (owner_stop(owner,&code)) return code;
        rendered=gin_history_render(view,i,&record);
        if (rendered==GIN_RENDER_IGNORE) continue;
        if (rendered!=GIN_RENDER_OK) {
            gin_render_record_free(record);
            return rendered==GIN_RENDER_NOMEM?GIN_SESSION_NOMEM:
                rendered==GIN_RENDER_LIMIT || rendered==GIN_RENDER_RANGE?GIN_SESSION_LIMIT:
                rendered==GIN_RENDER_UNSUPPORTED_ENCRYPTION?GIN_SESSION_UNSUPPORTED:GIN_SESSION_ROUTE;
        }
        rendered=gin_render_append(output,record,&appended);
        gin_render_record_free(record);
        if (rendered!=GIN_RENDER_OK) return GIN_SESSION_IO;
        if (appended) {
            completed_output=1;
            /* The REST cursor is only an eligibility ceiling. Independently
             * advance through ordered rows AFTER each complete successful
             * append; a higher buffered live ID is never sufficient. */
            if (row->id<=status.fetch_after) successful_prefix=row->id;
        }
    }
    for (i=0;i<owner->deferred_count;++i) {
        size_t appended=0;
        if (owner_stop(owner,&code)) return code;
        if (gin_render_append(output,owner->deferred[i],&appended)!=GIN_RENDER_OK)
            return GIN_SESSION_IO;
        if (appended) completed_output=1;
    }
    if (owner_stop(owner,&code)) return code;
    if (completed_output) {
        struct gin_checkpoint_write_result result=gin_checkpoint_commit(owner->checkpoint,successful_prefix);
        if (result.code!=GIN_CHECKPOINT_OK || result.cleanup_failed) return GIN_SESSION_IO;
        owner->processed_anchor=successful_prefix;
    }
    owner->history_pending=0;
    owner_history_clear(owner);
    return GIN_SESSION_OK;
}
/* A single bounded recovery attempt, no mutation replay and no output inside
 * callbacks. Per-request preflight/identity stays fresh; the owner also enforces
 * whole-attempt and original startup deadlines before admitting each page. */
static enum gin_session_code owner_history_catchup(struct owner *owner,
    struct metadata *metadata, struct gin_live_projection *projection,
    struct gin_topology **topology, int *output, uint64_t *snapshot_epoch,
    const struct gin_connect_settings *transport, const struct gin_credential *credential,
    const struct timespec *startup, int *recover_native)
{
    struct owner_history_http_control state={
        {owner,metadata,projection,topology,output,snapshot_epoch,GIN_SESSION_OK,0},
        {owner,&owner->history_deadline,GIN_SESSION_OK},
        {owner,startup,GIN_SESSION_OK}};
    struct gin_http_control control={owner_history_http_tick,&state,0};
    *recover_native=0;
    while (gin_history_recovery_status(owner->history).state==GIN_HISTORY_RECOVERING) {
        struct gin_history_request request;
        struct gin_history_page *page=NULL;
        struct gin_history_fetch_result fetched;
        struct gin_topology *borrowed=*topology;
        enum gin_session_code code;
        uint64_t now;
        if (!history_now(&now)) return GIN_SESSION_IO;
        code=history_code(gin_history_recovery_next(owner->history,now,&request));
        if (code!=GIN_SESSION_OK) return code;
        owner->history_borrowed_topology=borrowed;
        fetched=gin_history_fetch_controlled(&transport->http,&transport->policy,
            credential,borrowed,&owner->history_scope,&request,&page,&control);
        owner->history_borrowed_topology=NULL;
        if (borrowed!=*topology) gin_topology_free(borrowed);
        if (control.aborted) {
            gin_history_page_free(page);
            *recover_native=state.live.recover_native;
            return state.live.reason;
        }
        code=history_code(fetched.code);
        if (code!=GIN_SESSION_OK) { gin_history_page_free(page); return code; }
        if (!owner_history_receive_barrier(&state)) {
            gin_history_page_free(page);
            *recover_native=state.live.recover_native;
            return state.live.reason;
        }
        if (!history_now(&now)) { gin_history_page_free(page); return GIN_SESSION_IO; }
        code=history_code(gin_history_recovery_page(owner->history,page,now));
        gin_history_page_free(page);
        if (code!=GIN_SESSION_OK) return code;
    }
    return owner_history_publish(owner,*output);
}
static enum gin_session_code recover_online(struct owner *o,
    const struct gin_session_settings *settings, const struct gin_connect_settings *transport,
    const struct gin_credential *credential, struct gin_connect *connection,
    const struct gin_name *name, int64_t network_id, char own_nick[257],
    struct gin_topology **topology, const struct timespec *episode)
{
    enum gin_session_code code=GIN_SESSION_OK;
    struct owner_http_control state={o,episode,GIN_SESSION_OK};
    struct gin_http_control control={owner_http_tick,&state,0};
    for (;;) {
        struct gin_liveness_status live;
        struct gin_topology *fresh=NULL;
        const struct gin_topology_network *network;
        const struct gin_topology_window *window;
        enum gin_topology_code tc;
        enum gin_retry_step step;
        char nick[257]={0};
        int d;
        if (owner_stop(o,&code)) return code;
        d=deadline_state(episode);
        if (d) return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT;
        live=gin_connect_liveness_status(connection);
        if (live.state==GIN_LIVENESS_WAITING) {
            unsigned wait=live.retry_wait_ms<GIN_SESSION_SERVICE_MS?
                live.retry_wait_ms:GIN_SESSION_SERVICE_MS;
            code=owner_wait_slice(o,wait);
            if (code!=GIN_SESSION_OK) return code;
            continue;
        }
        if (live.state!=GIN_LIVENESS_DUE) return GIN_SESSION_CONNECTION;
        /* No failed-state WSS service or FIFO admission during backoff. One
         * failed topology refresh terminates; it must not become free retries. */
        tc=gin_topology_fetch_controlled(&transport->http,&transport->policy,credential,
            settings->local.chantypes,&fresh,&control);
        if (control.aborted) { gin_topology_free(fresh); return state.reason; }
        if (owner_stop(o,&code)) { gin_topology_free(fresh); return code; }
        d=deadline_state(episode);
        if (d) { gin_topology_free(fresh); return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; }
        if (tc!=GIN_TOPOLOGY_OK) {
            gin_topology_free(fresh);
            return tc==GIN_TOPOLOGY_TLS?GIN_SESSION_TLS:GIN_SESSION_TOPOLOGY;
        }
        network=gin_topology_find_slug(fresh,settings->local.network);
        window=gin_topology_find_window(fresh,network_id,name->canonical_target);
        if (!network || network->id!=network_id ||
            (name->kind==GIN_WINDOW_CHANNEL &&
             (!window || window->role!=GIN_REMOTE_CHANNEL || !window->joined))) {
            gin_topology_free(fresh); return GIN_SESSION_ROUTE;
        }
        if (network->own_nick) {
            size_t length=strlen(network->own_nick);
            if (length>=sizeof(nick)) { gin_topology_free(fresh); return GIN_SESSION_ROUTE; }
            memcpy(nick,network->own_nick,length+1u);
        }
        if (name->kind==GIN_WINDOW_NONCHANNEL) {
            struct gin_name old_source,new_source;
            if (!*nick || gin_name_prepare(settings->local.network,own_nick,
                    settings->local.chantypes,&old_source)<0 ||
                gin_name_prepare(settings->local.network,nick,
                    settings->local.chantypes,&new_source)<0 ||
                new_source.kind!=GIN_WINDOW_NONCHANNEL ||
                strcmp(old_source.canonical_target,new_source.canonical_target)) {
                gin_topology_free(fresh); return GIN_SESSION_ROUTE;
            }
        }
        d=deadline_state(episode);
        if (d) { gin_topology_free(fresh); return d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; }
        if (owner_stop(o,&code)) { gin_topology_free(fresh); return code; }
        step=gin_connect_retry_controlled(connection,&control);
        if (control.aborted) { gin_topology_free(fresh); return state.reason; }
        if (owner_stop(o,&code)) { gin_topology_free(fresh); return code; }
        if (step!=GIN_RETRY_ATTEMPTED) {
            gin_topology_free(fresh); return GIN_SESSION_CONNECTION;
        }
        if (gin_connect_last_result(connection).code!=GIN_CONNECT_OK) {
            enum gin_connect_code failure=gin_connect_last_result(connection).code;
            gin_topology_free(fresh);
            if (failure==GIN_CONNECT_TLS) return GIN_SESSION_TLS;
            if (!retry_possible(connection)) return GIN_SESSION_CONNECTION;
            continue;
        }
        gin_topology_free(*topology); *topology=fresh;
        memcpy(own_nick,nick,sizeof(nick));
        if (o->history || o->checkpoint) {
            struct gin_history_scope scope=o->history_scope;
            int64_t anchor=o->processed_anchor;
            /* The prior queue was drained under its old authority. Discard
             * any uncommitted owned stage; never carry pages/live IDs across
             * epochs or replace the successfully committed output prefix.
             * Before initial checkpoint setup there is nothing to rearm. */
            scope.socket_epoch=gin_connect_last_result(connection).socket_epoch;
            scope.own_nick=own_nick;
            owner_history_clear(o);
            code=owner_history_begin(o,&scope,anchor);
            if (code!=GIN_SESSION_OK) return code;
        }
        return GIN_SESSION_OK;
    }
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
static enum gin_session_code publish_operation(struct owner *o, uint64_t id,
    enum gin_operation_kind kind, enum gin_operation_state state, unsigned reason)
{
    struct gin_operation_event event;
    char record[GIN_OPERATION_RECORD_MAX + 1u];
    size_t length = 0;
    unsigned attempt;
    if (o->operations < 0) return GIN_SESSION_OK;
    event.id=id; event.kind=kind; event.state=state; event.code=reason;
    if (!gin_operation_format(&event,record,sizeof(record),&length))
        return GIN_SESSION_OPERATION;
    for (attempt=0; attempt<3; ++attempt) {
        ssize_t n=write(o->operations,record,length);
        if (n>=0 && (size_t)n==length) return GIN_SESSION_OK;
        if (n<0 && errno==EINTR) continue;
        return GIN_SESSION_OPERATION;
    }
    return GIN_SESSION_OPERATION;
}
/* Take each terminal receipt once, outside callbacks. Pending slots block input.
 * The caller retains the primary owner error during abortive teardown. */
static enum gin_session_code finish_query(struct owner *o,
    struct gin_connect *connection, uint64_t *active, enum gin_session_code *failure)
{
    struct gin_query_result result;
    enum gin_operation_state state;
    int taken;
    *failure=GIN_SESSION_OK;
    if (!*active) return GIN_SESSION_OK;
    taken=gin_connect_query_take(connection,&result);
    if (!taken) return GIN_SESSION_OK;
    if (taken<0) return GIN_SESSION_CONNECTION;
    if (result.operation_id!=*active) { *active=0; return GIN_SESSION_CONNECTION; }
    *active=0;
    switch (result.outcome) {
    case GIN_QUERY_ACCEPTED: state=GIN_OPERATION_ACCEPTED_ACK; break;
    case GIN_QUERY_REMOTE_FAILURE:
        state=GIN_OPERATION_REMOTE_FAILURE; *failure=GIN_SESSION_SEND_REMOTE_FAILURE; break;
    case GIN_QUERY_NOT_SENT:
        state=GIN_OPERATION_NOT_SENT; *failure=GIN_SESSION_QUERY_NOT_SENT; break;
    case GIN_QUERY_UNCERTAIN:
        state=GIN_OPERATION_UNCERTAIN; *failure=GIN_SESSION_QUERY_UNCERTAIN; break;
    default: return GIN_SESSION_CONNECTION;
    }
    return publish_operation(o,result.operation_id,GIN_OPERATION_QUERY,state,(unsigned)result.code);
}
enum gin_session_code gin_session_run(const struct gin_session_settings *settings,
    int fd, int eof)
{
    return gin_session_run_with_operations(settings,fd,eof,-1);
}
enum gin_session_code gin_session_run_with_operations(
    const struct gin_session_settings *settings, int fd, int eof, int operation_fd)
{
    struct gin_session_settings copy;
    const struct gin_session_settings *s = &copy;
    struct owner o;
    struct gin_credential credential;
    struct gin_topology *topology = NULL;
    struct gin_connect *connection = NULL;
    struct metadata metadata;
    struct gin_connect_settings connect_settings;
    struct gin_connect_subscription subscriptions[2];
    struct gin_live_projection projection;
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record = {{0}, 0};
    struct gin_command command;
    struct timespec fifo_deadline = {0, 0};
    int fifo_cooldown = 0, service_now = 0;
    uint64_t operation_id = 0, query_id = 0;
    char subject[GIN_CREDENTIAL_NAME_MAX + 1], own_nick[257] = {0};
    size_t subscription_count = 1;
    int output = -1;
    struct gin_name name;
    struct timespec deadline;
    const struct gin_protocol_policy policy = {GIN_SESSION_CLIENT_PROTOCOL, GIN_SESSION_SERVER_MIN};
    enum gin_session_code code = GIN_SESSION_OK;
    int ready = 0, ready_announced = 0, recovering = 0;
    int64_t network_id = 0;
    unsigned startup_retries = 0;
    uint64_t snapshot_epoch = 0;
    if (!settings || !gin_session_settings_valid(settings) || (eof != 0 && eof != 1) ||
        (fd >= 0 && fd == operation_fd))
        return GIN_SESSION_INVALID;
    copy = *settings;
    if (session_active) return GIN_SESSION_BUSY;
    owner_init(&o); memset(&credential, 0, sizeof(credential));
    memset(&metadata, 0, sizeof(metadata));
    if (fd != -1 && (fd < 3 || (o.status = borrow_pipe(fd, 1)) < 0)) return GIN_SESSION_STATUS;
    if (operation_fd != -1 && (operation_fd < 3 ||
        (o.operations=borrow_pipe(operation_fd,1)) < 0 ||
        fpathconf(o.operations,_PC_PIPE_BUF) < (long)GIN_OPERATION_RECORD_MAX)) {
        code=GIN_SESSION_OPERATION; goto done;
    }
    if (eof && (o.input = borrow_pipe(STDIN_FILENO, 0)) < 0) { code = GIN_SESSION_INPUT; goto done; }
    code = owner_begin(&o);
    if (code != GIN_SESSION_OK) goto done;
    if (owner_stop(&o, &code)) goto done;
    if (owner_deadline_start(&deadline,s->startup_ms)<0) { code=GIN_SESSION_IO; goto done; }
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
    code=fetch_initial_topology(&o,s,&connect_settings,&credential,&topology,
        &deadline,&startup_retries);
    if (code!=GIN_SESSION_OK || !topology || owner_stop(&o,&code)) goto done;
    {
        const struct gin_topology_network *network=gin_topology_find_slug(topology,s->local.network);
        const struct gin_topology_window *window;
        if (!network || gin_name_prepare(s->local.network,s->local.target,s->local.chantypes,&name) < 0) {
            code=GIN_SESSION_ROUTE; goto done;
        }
        network_id=network->id;
        if (network->own_nick) memcpy(own_nick, network->own_nick, strlen(network->own_nick) + 1u);
        window=gin_topology_find_window(topology,network_id,name.canonical_target);
        if (name.kind == GIN_WINDOW_CHANNEL && (!window || window->role != GIN_REMOTE_CHANNEL || !window->joined)) {
            code=GIN_SESSION_ROUTE; goto done;
        }
    }
    { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; goto done; } }
    subscriptions[0].network=s->local.network; subscriptions[0].target=s->local.target;
    if (name.kind == GIN_WINDOW_NONCHANNEL) {
        struct gin_name own;
        if (!*own_nick || gin_name_prepare(s->local.network,own_nick,s->local.chantypes,&own) < 0 ||
            own.kind != GIN_WINDOW_NONCHANNEL) { code=GIN_SESSION_ROUTE; goto done; }
        subscriptions[1].network=s->local.network; subscriptions[1].target=own_nick;
        subscription_count=2;
    }
    memcpy(subject,credential.subject_name,sizeof(subject));
    memcpy(o.subject_id,credential.subject_id,sizeof(o.subject_id));
    memset(&projection,0,sizeof(projection));
    projection.settings=s; projection.subject=subject; projection.own_nick=own_nick;
    projection.network_id=network_id; projection.query=name.kind==GIN_WINDOW_NONCHANNEL;
    if (gin_live_queue_new(GIN_LIVE_ROWS_MAX,GIN_LIVE_BYTES_MAX,&metadata.queue) != GIN_LIVE_OK) {
        code=GIN_SESSION_NOMEM; goto done;
    }
    {
        enum gin_connect_code cc=gin_connect_new(&connect_settings,&credential,subscriptions,subscription_count,metadata_frame,&metadata,&connection);
        if (cc != GIN_CONNECT_OK) { code=cc==GIN_CONNECT_NOMEM?GIN_SESSION_NOMEM:GIN_SESSION_CONNECTION; goto done; }
    }
    if (gin_connect_topic_count(connection) > 4u) { code=GIN_SESSION_ROUTE; goto done; }
    metadata.connection=connection;
    {
        struct gin_liveness_settings live;
        /* Settings were bounded before ownership or I/O; the native policy is
         * configured once on this new connection, never reset by traffic. */
        live.heartbeat_interval_ms=(unsigned)s->heartbeat_interval_ms;
        live.heartbeat_timeout_ms=(unsigned)s->heartbeat_timeout_ms;
        live.retry_base_ms=(unsigned)s->retry_base_ms;
        live.retry_cap_ms=(unsigned)s->retry_cap_ms;
        if (startup_retries>(unsigned)s->max_retries) { code=GIN_SESSION_IO; goto done; }
        live.max_retries=(unsigned)s->max_retries-startup_retries;
        /* Native status counts only its remaining portion. Preserve the global
         * capped backoff sequence without reconfiguring a started connection.
         * No index 17 is requested when all sixteen owner retries were used. */
        if (live.max_retries && startup_retries &&
            !gin_liveness_delay((unsigned)s->retry_base_ms,(unsigned)s->retry_cap_ms,
                startup_retries+1u,&live.retry_base_ms)) { code=GIN_SESSION_IO; goto done; }
        if (gin_connect_set_liveness(connection,&live)!=GIN_CONNECT_OK) {
            code=GIN_SESSION_CONNECTION; goto done;
        }
    }
    if (owner_stop(&o,&code)) goto done;
    {
        struct owner_http_control state={&o,&deadline,GIN_SESSION_OK};
        struct gin_http_control control={owner_http_tick,&state,0};
        enum gin_connect_code cc=gin_connect_start_controlled(connection,&control);
        if (control.aborted) { code=state.reason; goto done; }
        if (owner_stop(&o,&code)) goto done;
        if (cc != GIN_CONNECT_OK) {
            if (!retry_possible(connection)) {
                code=cc==GIN_CONNECT_TLS?GIN_SESSION_TLS:GIN_SESSION_CONNECTION; goto done;
            }
            /* The initial native attempt shares the configured lifetime budget
             * and the original startup deadline, not a fresh per-retry timer. */
            recovering=1;
            code=recover_online(&o,s,&connect_settings,&credential,connection,&name,
                network_id,own_nick,&topology,&deadline);
            if (code!=GIN_SESSION_OK || owner_stop(&o,&code)) goto done;
        }
    }
    o.starting=1;
    code=owner_existing_checkpoint(&o,&projection,
        gin_connect_last_result(connection).socket_epoch,&output);
    if (code!=GIN_SESSION_OK || owner_stop(&o,&code)) goto done;
    for (;;) {
        enum gin_connect_code cc;
        size_t i;
        if (owner_stop(&o,&code)) break;
        if (!ready) { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; break; } }
        {
            struct timespec wall;
            /* One explicit owner timestamp for the following receive batch. */
            if (clock_gettime(CLOCK_REALTIME,&wall) < 0 ||
                !gin_live_receive_ms(&wall,&metadata.receive_ms)) { code=GIN_SESSION_IO; break; }
        }
        cc=gin_connect_service(connection,service_now ? 0u : GIN_SESSION_SERVICE_MS);
        service_now=0;
        /* Callback failures are already latched; stop otherwise beats the
         * just-returned aggregate network failure, not an earlier owner error. */
        if (metadata.count_overflow || metadata.bytes_overflow) { code=GIN_SESSION_LIMIT; break; }
        if (metadata.nomem) { code=GIN_SESSION_NOMEM; break; }
        if (owner_stop(&o,&code)) break;
        if (cc != GIN_CONNECT_OK) {
            struct gin_liveness_status live=gin_connect_liveness_status(connection);
            /* An occupied terminal QUERY slot blocks retry until its receipt is
             * consumed. It does not authorize retry of the command itself. */
            if (live.state!=GIN_LIVENESS_WAITING && live.state!=GIN_LIVENESS_DUE &&
                !(query_id && live.state==GIN_LIVENESS_RECEIPT_BLOCKED)) {
                code=GIN_SESSION_CONNECTION; break;
            }
        }
        /* Drain copied old-epoch records under their old topology before any
         * fresh snapshot becomes authority. Native failure retains the epoch. */
        code=owner_drain_metadata(&o,&metadata,&projection,&topology,&output,&snapshot_epoch);
        if (code != GIN_SESSION_OK || owner_stop(&o,&code)) break;
        if (cc!=GIN_CONNECT_OK) {
            if (query_id) {
                enum gin_session_code failure;
                code=finish_query(&o,connection,&query_id,&failure);
                if (code!=GIN_SESSION_OK) break;
                if (query_id) { code=GIN_SESSION_CONNECTION; break; }
                /* Report the individual outcome, then independently decide
                 * whether this transient connection may recover. No replay. */
                if (failure!=GIN_SESSION_OK && failure!=GIN_SESSION_QUERY_UNCERTAIN &&
                    failure!=GIN_SESSION_QUERY_NOT_SENT) { code=failure; break; }
            }
            /* Re-evaluate native authority after take AND successful publish. */
            if (!retry_possible(connection)) { code=GIN_SESSION_CONNECTION; break; }
            /* Before the first READY, retries share the original startup
             * deadline. Only later online-loss episodes receive a new one. */
            if (!recovering && ready_announced &&
                owner_deadline_start(&deadline,s->startup_ms)<0) {
                code=GIN_SESSION_IO; break;
            }
            recovering=1;
            code=recover_online(&o,s,&connect_settings,&credential,connection,&name,
                network_id,own_nick,&topology,&deadline);
            if (code!=GIN_SESSION_OK || owner_stop(&o,&code)) break;
            ready=0; snapshot_epoch=0; service_now=1; o.starting=1;
            continue;
        }
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
                if (o.new_window) {
                    code=owner_new_window_ready(&o,&projection,epoch,&output);
                    if (code!=GIN_SESSION_OK || owner_stop(&o,&code)) break;
                }
                if (o.history_pending) {
                    int recover_native=0;
                    code=owner_history_catchup(&o,&metadata,&projection,&topology,&output,
                        &snapshot_epoch,&connect_settings,&credential,&deadline,&recover_native);
                    if (code==GIN_SESSION_CONNECTION && recover_native && retry_possible(connection)) {
                        /* Like mutation HTTP, unwind the controlled request
                         * before the ordinary owner recovery path. Never reuse
                         * the aborted page/stage or reset this episode timer. */
                        service_now=1; continue;
                    }
                    if (code!=GIN_SESSION_OK || owner_stop(&o,&code)) break;
                    { int d=deadline_state(&deadline); if (d) { code=d<0?GIN_SESSION_IO:GIN_SESSION_TIMEOUT; break; } }
                }
                if (!ready_announced) {
                    if (gin_root_init(&o.root,s->local.network,s->local.target,s->local.chantypes)<0 ||
                        (o.fifo=gin_root_input_open(&o.root,s->local.network,s->local.target,s->local.chantypes))<0) {
                        code=GIN_SESSION_IO; break;
                    }
                    code=publish_ready(&o); if (code != GIN_SESSION_OK) break;
                    ready_announced=1;
                }
                ready=1; recovering=0; o.starting=0;
            }
        }
        if (query_id) {
            enum gin_session_code failure;
            code=finish_query(&o,connection,&query_id,&failure);
            if (code==GIN_SESSION_OK) code=failure;
            if (code!=GIN_SESSION_OK) break;
        }
        if (ready && !query_id && gin_connect_receive_drained(connection)) {
            enum gin_input_result event;
            enum gin_command_code parsed;
            const struct gin_topology_network *network;
            const struct gin_topology_window *window;
            struct gin_send_route route;
            struct gin_send_settings send_settings;
            struct gin_send_result result;
            enum gin_operation_kind operation_kind;
            unsigned invalid_reason;
            int is_control, selected_part=0;
            if (fifo_cooldown) {
                int d=deadline_state(&fifo_deadline);
                if (d<0) { code=GIN_SESSION_IO; break; }
                if (!d) continue;
                fifo_cooldown=0;
            }
            /* One bounded framing quantum per serviced/projected owner turn.
             * Buffered MORE/records are retried without kernel readiness. */
            event=gin_input_next(&input,o.fifo,&record);
            if (owner_stop(&o,&code)) break;
            if (event==GIN_INPUT_EOF) {
                if (fifo_deadline_start(&fifo_deadline)<0) { code=GIN_SESSION_IO; break; }
                fifo_cooldown=1;
                continue;
            }
            if (event==GIN_INPUT_ERROR) { code=GIN_SESSION_INPUT; break; }
            if (event!=GIN_INPUT_LINE && event!=GIN_INPUT_REJECTED) continue;
            parsed=event==GIN_INPUT_REJECTED ? GIN_COMMAND_INVALID :
                gin_command_parse(record.data,record.length,s->local.chantypes,&command);
            if (parsed==GIN_COMMAND_EMPTY) continue;
            if (operation_id==UINT64_MAX) { code=GIN_SESSION_LIMIT; break; }
            ++operation_id;
            if (parsed!=GIN_COMMAND_OK || (command.kind!=GIN_COMMAND_SEND &&
                command.kind!=GIN_COMMAND_JOIN && command.kind!=GIN_COMMAND_PART &&
                command.kind!=GIN_COMMAND_QUERY)) {
                unsigned reason=parsed==GIN_COMMAND_UNSUPPORTED || parsed==GIN_COMMAND_OK ?
                    GIN_COMMAND_UNSUPPORTED : GIN_COMMAND_INVALID;
                code=publish_operation(&o,operation_id,GIN_OPERATION_INPUT,
                    GIN_OPERATION_REJECTED,reason);
                if (code==GIN_SESSION_OK) code=reason==GIN_COMMAND_UNSUPPORTED ?
                    GIN_SESSION_UNSUPPORTED : GIN_SESSION_INPUT;
                break;
            }
            is_control=command.kind!=GIN_COMMAND_SEND;
            operation_kind=command.kind==GIN_COMMAND_JOIN?GIN_OPERATION_JOIN:
                command.kind==GIN_COMMAND_PART?GIN_OPERATION_PART:
                command.kind==GIN_COMMAND_QUERY?GIN_OPERATION_QUERY:GIN_OPERATION_SEND;
            invalid_reason=command.kind==GIN_COMMAND_QUERY?GIN_QUERY_INVALID:
                is_control?GIN_CONTROL_INVALID:GIN_SEND_INVALID;
            code=publish_operation(&o,operation_id,operation_kind,GIN_OPERATION_PENDING,0);
            if (code!=GIN_SESSION_OK) break;
            network=gin_topology_find_slug(topology,name.input_network_slug);
            window=gin_topology_find_window(topology,network_id,name.canonical_target);
            if (!network || network->id!=network_id || !window ||
                (name.kind==GIN_WINDOW_CHANNEL ?
                    window->role!=GIN_REMOTE_CHANNEL || !window->joined :
                    !gin_topology_queries_known(topology) || window->role!=GIN_REMOTE_QUERY)) {
                code=publish_operation(&o,operation_id,operation_kind,
                    GIN_OPERATION_NOT_ATTEMPTED,invalid_reason);
                if (code==GIN_SESSION_OK) code=command.kind==GIN_COMMAND_QUERY?
                    GIN_SESSION_QUERY_NOT_ATTEMPTED:is_control?
                    GIN_SESSION_CONTROL_NOT_ATTEMPTED:GIN_SESSION_SEND_NOT_ATTEMPTED;
                break;
            }
            if (command.kind==GIN_COMMAND_PART) {
                struct gin_name control_name;
                if (gin_name_prepare(name.input_network_slug,(const char *)command.data,
                    s->local.chantypes,&control_name)<0) {
                    code=publish_operation(&o,operation_id,operation_kind,
                        GIN_OPERATION_NOT_ATTEMPTED,GIN_CONTROL_INVALID);
                    if (code==GIN_SESSION_OK) code=GIN_SESSION_CONTROL_NOT_ATTEMPTED;
                    break;
                }
                selected_part=name.kind==GIN_WINDOW_CHANNEL &&
                    !strcmp(control_name.canonical_target,name.canonical_target);
            }
            route.network_slug=name.input_network_slug;
            route.wire_target=is_control?(const char *)command.data:name.original_wire_target;
            route.chantypes=s->local.chantypes;
            route.role=is_control || window->role==GIN_REMOTE_CHANNEL?
                GIN_SEND_ROLE_CHANNEL:GIN_SEND_ROLE_QUERY;
            route.mode=GIN_SEND_MODE_PLAIN;
            send_settings.auth=connect_settings.http;
            send_settings.protocol=policy;
            if (owner_stop(&o,&code)) {
                enum gin_session_code stopped=code;
                code=publish_operation(&o,operation_id,operation_kind,
                    GIN_OPERATION_NOT_ATTEMPTED,invalid_reason);
                if (code==GIN_SESSION_OK) code=stopped;
                break;
            }
            if (command.kind==GIN_COMMAND_QUERY) {
                enum gin_query_code qc=gin_connect_open_query(connection,topology,
                    name.input_network_slug,(const char *)command.data,s->local.chantypes,
                    GIN_QUERY_MODE_PLAIN,operation_id,(unsigned)s->join_ms);
                if (qc!=GIN_QUERY_OK) {
                    code=publish_operation(&o,operation_id,GIN_OPERATION_QUERY,
                        GIN_OPERATION_NOT_ATTEMPTED,(unsigned)qc);
                    if (code==GIN_SESSION_OK) code=GIN_SESSION_QUERY_NOT_ATTEMPTED;
                    break;
                }
                query_id=operation_id;
                service_now=1;
                continue;
            }
            if (is_control) {
                struct owner_live_http_control live_control={&o,&metadata,&projection,&topology,
                    &output,&snapshot_epoch,GIN_SESSION_OK,0};
                struct gin_http_control http_control={owner_live_http_tick,&live_control,0};
                struct gin_control_result control=command.kind==GIN_COMMAND_PART?
                    gin_control_part_controlled(&o.root,&send_settings,&credential,&route,operation_id,&http_control):
                    gin_control_join_controlled(&o.root,&send_settings,&credential,&route,operation_id,&http_control);
                enum gin_operation_state state;
                enum gin_session_code failure=GIN_SESSION_OK;
                switch (control.outcome) {
                case GIN_CONTROL_ACCEPTED: state=GIN_OPERATION_ACCEPTED_PENDING; break;
                case GIN_CONTROL_NOT_ATTEMPTED:
                    state=GIN_OPERATION_NOT_ATTEMPTED;
                    failure=GIN_SESSION_CONTROL_NOT_ATTEMPTED; break;
                case GIN_CONTROL_REMOTE_FAILURE:
                    state=GIN_OPERATION_REMOTE_FAILURE;
                    failure=GIN_SESSION_SEND_REMOTE_FAILURE; break;
                default:
                    state=GIN_OPERATION_UNCERTAIN;
                    failure=GIN_SESSION_CONTROL_UNCERTAIN; break;
                }
                code=publish_operation(&o,operation_id,operation_kind,state,(unsigned)control.code);
                if (code==GIN_SESSION_OK && http_control.aborted && live_control.recover_native &&
                    !selected_part && retry_possible(connection)) {
                    /* The individual command is complete, never replayed. Let
                     * the ordinary owner failure path recover its transport. */
                    service_now=1; continue;
                }
                if (code==GIN_SESSION_OK) code=http_control.aborted?live_control.reason:failure;
                /* The selected channel's boot membership is no longer authority.
                 * Stop after reporting, including when following input is buffered. */
                if (http_control.aborted || code!=GIN_SESSION_OK || selected_part) break;
                service_now=1;
                continue;
            }
            {
            struct owner_live_http_control live_control={&o,&metadata,&projection,&topology,
                &output,&snapshot_epoch,GIN_SESSION_OK,0};
            struct gin_http_control http_control={owner_live_http_tick,&live_control,0};
            result=gin_send_plain_controlled(&o.root,&send_settings,&credential,&route,
                operation_id,command.data,command.length,&http_control);
            /* Preserve native outcome, not an inference from HTTP status.
             * Publish completion before observing deferred termination. */
            {
                enum gin_operation_state state;
                enum gin_session_code failure=GIN_SESSION_OK;
                switch (result.outcome) {
                case GIN_SEND_ACCEPTED_PERSISTED: state=GIN_OPERATION_ACCEPTED_PERSISTED; break;
                case GIN_SEND_ACCEPTED_NO_PERSIST: state=GIN_OPERATION_ACCEPTED_NO_PERSIST; break;
                case GIN_SEND_NOT_ATTEMPTED:
                    state=GIN_OPERATION_NOT_ATTEMPTED; failure=GIN_SESSION_SEND_NOT_ATTEMPTED; break;
                case GIN_SEND_REMOTE_FAILURE:
                    state=GIN_OPERATION_REMOTE_FAILURE; failure=GIN_SESSION_SEND_REMOTE_FAILURE; break;
                default: state=GIN_OPERATION_UNCERTAIN; failure=GIN_SESSION_SEND_UNCERTAIN; break;
                }
                code=publish_operation(&o,operation_id,GIN_OPERATION_SEND,state,(unsigned)result.code);
                if (code==GIN_SESSION_OK && http_control.aborted && live_control.recover_native &&
                    retry_possible(connection)) {
                    service_now=1; continue;
                }
                if (code==GIN_SESSION_OK) code=http_control.aborted?live_control.reason:failure;
                if (http_control.aborted || code!=GIN_SESSION_OK) break;
            }
            }
            service_now=1;
        }
    }
done:
    gin_connect_close(connection);
    if (query_id) {
        enum gin_session_code failure, receipt;
        /* Close classifies queued bytes as NOT_SENT or UNCERTAIN without replay.
         * Keep a graceful stop graceful, but never hide receipt-publication I/O. */
        receipt=finish_query(&o,connection,&query_id,&failure);
        if (receipt==GIN_SESSION_OK && query_id) receipt=GIN_SESSION_CONNECTION;
        if (code==GIN_SESSION_OK) code=receipt;
    }
    if (code==GIN_SESSION_OK && gin_connect_cleanup_failed(connection)) code=GIN_SESSION_CLEANUP;
    if (gin_connect_quarantined(connection)) o.http=0; /* Retained library state, not clean teardown. */
    gin_connect_free(connection); gin_live_queue_free(metadata.queue);
    owner_history_clear(&o);
    if (o.checkpoint && gin_checkpoint_close(&o.checkpoint)<0 && code==GIN_SESSION_OK)
        code=GIN_SESSION_CLEANUP;
    close_owned(&output,&code);
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
        "out of memory", "session cleanup failure", "session owner busy",
        "operation pipe failure", "send not attempted",
        "remote operation failure", "send outcome uncertain",
        "control not attempted", "control outcome uncertain",
        "query not attempted", "query not sent", "query outcome uncertain"
    };
    return (unsigned)c < sizeof(errors) / sizeof(errors[0]) ? errors[c] :
        "unknown session error";
}
