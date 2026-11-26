#include "account_discovery.h"
#include "connect_control.h"
#include "session_live_queue.h"
#include <string.h>
#include <time.h>
#include <stdint.h>
struct state {
 struct gin_connect *connection;
 struct gin_live_queue *queue;
 struct gin_http_control *borrowed;
 uint64_t deadline, receive;
 size_t snapshots, bytes;
 enum gin_account_discovery_code failure;
};
static int now_ms(uint64_t *n) {
 struct timespec ts;
 if(clock_gettime(CLOCK_MONOTONIC,&ts)||ts.tv_sec<0||ts.tv_nsec<0||ts.tv_nsec>=1000000000L ||
 (uint64_t)ts.tv_sec>(UINT64_MAX-999)/1000) return 0;
 *n=(uint64_t)ts.tv_sec*1000+(uint64_t)ts.tv_nsec/1000000; return 1;
}
static int tick(void *opaque,unsigned *wait) {
 struct state *s=opaque; uint64_t now; unsigned proposed=*wait;
 if(s->failure) return 1;
 if(s->borrowed) {
  if(s->borrowed->aborted) { s->failure=GIN_DISCOVERY_STOP; return 1; }
  if(s->borrowed->tick(s->borrowed->opaque,wait)) {
   s->borrowed->aborted=1; s->failure=GIN_DISCOVERY_STOP; return 1;
  }
  if(*wait>proposed) *wait=proposed;
 }
 if(!now_ms(&now)) { s->failure=GIN_DISCOVERY_CLOCK; return 1; }
 if(now>=s->deadline) { s->failure=GIN_DISCOVERY_TIMEOUT; return 1; }
 if(s->deadline-now<*wait) *wait=(unsigned)(s->deadline-now);
 s->receive=now; return 0;
}
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

                    if (!fits) return 0;
                    if (!strcmp(value,"query_windows_list")) return 1;
                    return 0;
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

static void receive(void *opaque,const struct gin_phoenix_frame *frame,uint64_t epoch) {
 struct state *s=opaque;
 const struct gin_phoenix_fields *f=gin_phoenix_frame_fields(frame);
 const struct gin_connect_topic_status *root=gin_connect_topic_at(s->connection,0);
 struct gin_live_view view; enum gin_live_code code;
 if(s->failure || !f || !root || gin_connect_topic_count(s->connection)!=1 ||
 strcmp(f->event,"event") || strcmp(f->topic,root->topic) ||
 root->state!=GIN_CONNECT_TOPIC_JOINED || !root->join_ref || !*root->join_ref ||
 !epoch || epoch!=gin_connect_last_result(s->connection).socket_epoch ||
 (f->join_ref && strcmp(f->join_ref,root->join_ref)) ||
 recognized_payload(f->payload.data,f->payload.length)!=1) return;
 if(s->snapshots>=GIN_ACCOUNT_DISCOVERY_SNAPSHOTS_MAX ||
 f->payload.length>GIN_ACCOUNT_DISCOVERY_BYTES_MAX-s->bytes) {
  s->failure=GIN_DISCOVERY_LIMIT; return;
 }
 ++s->snapshots; s->bytes+=f->payload.length;
 memset(&view,0,sizeof(view)); view.fields=*f; view.source_join_ref=root->join_ref;
 view.observed_epoch=epoch; view.receive_ms=(int64_t)s->receive;
 view.source_index=0; view.kind=GIN_LIVE_QUERY_SNAPSHOT;
 code=gin_live_queue_push(s->queue,&view);
 if(code!=GIN_LIVE_OK) s->failure=code==GIN_LIVE_NOMEM?GIN_DISCOVERY_NOMEM:GIN_DISCOVERY_LIMIT;
}
struct gin_account_discovery_result gin_account_discover(
 const struct gin_connect_settings *settings,const struct gin_credential *credential,
 const char *chantypes,struct gin_http_control *borrowed,unsigned startup_ms,
 struct gin_topology **out) {
 struct gin_account_discovery_result r={GIN_DISCOVERY_INVALID,GIN_TOPOLOGY_OK,GIN_CONNECT_OK,0,0};
 struct state s; struct gin_http_control control;
 struct gin_topology *topology=NULL; uint64_t now; unsigned wait=0;
 memset(&s,0,sizeof(s));
 if(!out || *out || !settings || !credential || !chantypes ||
 !startup_ms || startup_ms>60000 || (borrowed && !borrowed->tick)) return r;
 if(!now_ms(&now) || now>UINT64_MAX-startup_ms) { r.code=GIN_DISCOVERY_CLOCK; return r; }
 s.borrowed=borrowed; s.deadline=now+startup_ms;
 control.tick=tick; control.opaque=&s; control.aborted=0;
 if(tick(&s,&wait)) goto done;
 r.connect=gin_connect_new(settings,credential,NULL,0,receive,&s,&s.connection);
 if(r.connect!=GIN_CONNECT_OK) { r.code=GIN_DISCOVERY_CONNECT; goto done; }
 if(gin_live_queue_new_sources(8,GIN_LIVE_BYTES_MAX,1,&s.queue)!=GIN_LIVE_OK) {
  r.code=GIN_DISCOVERY_NOMEM; goto done;
 }
 r.topology=gin_topology_fetch_controlled(&settings->http,&settings->policy,credential,chantypes,&topology,&control);
 if(r.topology!=GIN_TOPOLOGY_OK) { r.code=GIN_DISCOVERY_TOPOLOGY; goto done; }
 wait=0; if(tick(&s,&wait)) goto done;
 r.connect=gin_connect_start_controlled(s.connection,&control);
 if(r.connect!=GIN_CONNECT_OK) { r.code=GIN_DISCOVERY_CONNECT; goto done; }
 for(;;) {
  const struct gin_live_view *v; const struct gin_connect_topic_status *root;
  wait=50; if(tick(&s,&wait)) break;
  r.connect=gin_connect_service(s.connection,wait);
  if(r.connect!=GIN_CONNECT_OK) { r.code=GIN_DISCOVERY_CONNECT; break; }
  if(s.failure) break;
  while((v=gin_live_queue_peek(s.queue))!=NULL) {
   struct gin_topology *replacement=NULL;
   r.topology=gin_topology_apply_queries(topology,v->fields.payload.data,v->fields.payload.length,&replacement);
   if(r.topology!=GIN_TOPOLOGY_OK) { r.code=GIN_DISCOVERY_TOPOLOGY; goto done; }
   gin_topology_free(topology); topology=replacement; gin_live_queue_drop(s.queue);
  }
  wait=0; if(tick(&s,&wait)) break;
  root=gin_connect_topic_at(s.connection,0);
  if(gin_topology_queries_known(topology) && root && root->state==GIN_CONNECT_TOPIC_JOINED &&
   root->join_ref && *root->join_ref && gin_connect_get_state(s.connection)==GIN_CONNECT_READY &&
   gin_connect_receive_drained(s.connection)) { r.code=GIN_DISCOVERY_OK; break; }
 }
done:
 if(s.failure) r.code=s.failure;
 if(s.connection) {
  gin_connect_close(s.connection);
  r.cleanup_failed=gin_connect_cleanup_failed(s.connection);
  r.quarantined=gin_connect_quarantined(s.connection);
  if((r.cleanup_failed || r.quarantined) && r.code==GIN_DISCOVERY_OK) r.code=GIN_DISCOVERY_CLEANUP;
  gin_connect_free(s.connection);
 }
 gin_live_queue_free(s.queue);
 if(r.code==GIN_DISCOVERY_OK) { *out=topology; topology=NULL; }
 gin_topology_free(topology); return r;
}
