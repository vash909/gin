#define _POSIX_C_SOURCE 200809L
#include "account_owner.h"
#include "gin_input.h"
#include "gin_command.h"
#include <string.h>
#include <time.h>
struct owner {
 const struct gin_account_owner_input *in;
 struct gin_account_owner_result result;
 uint64_t now, deadline;
 int starting;
 struct gin_account_receive *receiver;
 struct gin_account_dispatch *dispatcher;
 struct gin_account_output *output;
 struct gin_account_destination *destinations;
 int64_t *anchors;
 size_t count;
};
static int boundary(void *opaque,unsigned *wait)
{
 struct owner *o=opaque;struct timespec ts;uint64_t now;
 if(clock_gettime(CLOCK_MONOTONIC,&ts) || ts.tv_sec<0 ||
    (uint64_t)ts.tv_sec>(UINT64_MAX-999u)/1000u) {
  o->result.code=GIN_ACCOUNT_OWNER_CLOCK;return 1;
 }
 now=(uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
 if(now<o->now) {o->result.code=GIN_ACCOUNT_OWNER_CLOCK;return 1;}
 o->now=now;
 if(o->starting && o->deadline && now>=o->deadline) {
  o->result.code=GIN_ACCOUNT_OWNER_DEADLINE;return 1;
 }
 if(o->in->stop && (o->in->stop->aborted ||
    (o->in->stop->tick && o->in->stop->tick(o->in->stop->opaque,wait)))) {
  o->in->stop->aborted=1;o->result.stopped=1;return 1;
 }
 if(o->starting && o->deadline && *wait>o->deadline-now) *wait=(unsigned)(o->deadline-now);
 return 0;
}
static int service(struct owner *o,struct gin_account_receive *receiver,unsigned wait)
{
 struct timespec ts;
 if(boundary(o,&wait)) return 1;
 if(clock_gettime(CLOCK_REALTIME,&ts) || ts.tv_sec<0 ||
    (uint64_t)ts.tv_sec>((uint64_t)INT64_MAX-999u)/1000u) {
  o->result.code=GIN_ACCOUNT_OWNER_CLOCK;return 1;
 }
 o->result.receive=gin_account_receive_service(receiver,wait,(int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000);
 if(o->result.receive!=GIN_CONNECT_OK) {o->result.code=GIN_ACCOUNT_OWNER_RECEIVE;return 1;}
 return boundary(o,&wait);
}
static int drain(struct owner *o)
{
 unsigned wait=0;size_t i, budget=64;
 while(budget-- && gin_account_receive_peek(o->receiver)) {
  struct gin_account_destination d;
  struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
  int stop;
  if(boundary(o,&wait)) return 1;
  o->result.dispatch=gin_account_dispatch_next(o->dispatcher,&d,&record,&page);
  if(o->result.dispatch!=GIN_SESSION_OK) o->result.code=GIN_ACCOUNT_OWNER_DISPATCH;
  stop=o->result.code==GIN_ACCOUNT_OWNER_OK?boundary(o,&wait):0;
  if(!stop && o->result.code==GIN_ACCOUNT_OWNER_OK && record) {
   for(i=0;i<o->count;i++) if(o->destinations[i].network_index==d.network_index && o->destinations[i].window_index==d.window_index && o->destinations[i].role==d.role) break;
   if(i==o->count) o->result.code=GIN_ACCOUNT_OWNER_DISPATCH;
   else {
    struct gin_account_output_result r=gin_account_output_append(o->output,&d,record,o->anchors[i]);
    o->result.output=r;o->result.cleanup_failed|=r.cleanup_failed;
    if(r.code!=GIN_CHECKPOINT_OK) o->result.code=GIN_ACCOUNT_OWNER_OUTPUT;
    else {o->result.appended++;o->result.terminal=d.terminal;}
   }
  }
  gin_render_record_free(record);gin_history_page_free(page);
  if(stop || o->result.code!=GIN_ACCOUNT_OWNER_OK || o->result.terminal) return 1;
 }
 return 0;
}
static int send_tick(void *opaque,unsigned *wait)
{
 struct owner *o=opaque;
 if(service(o,o->receiver,0) || drain(o)) return 1;
 o->result.receive=gin_account_receive_http_wait_budget(o->receiver,wait);
 if(o->result.receive!=GIN_CONNECT_OK) {o->result.code=GIN_ACCOUNT_OWNER_RECEIVE;return 1;}
 if(*wait>10u) *wait=10u;
 return boundary(o,wait);
}
static void query_take(struct gin_account_receive *receiver,
 struct gin_account_fifo_result *fifo,uint64_t *pending)
{
 struct gin_query_result r;
 if(!receiver || !fifo || !*pending) return;
 r=gin_account_receive_query_result(receiver);
 if(r.operation_id!=*pending || r.outcome==GIN_QUERY_NONE || r.outcome==GIN_QUERY_PENDING) return;
 if(gin_account_receive_query_take(receiver,&r)==1) {
  fifo->query_terminal=r;fifo->has_query_terminal=1;*pending=0;
 }
}
static struct gin_account_owner_result run(const struct gin_account_owner_input *in,struct gin_account_fifo_result *fifo)
{
 struct owner o;
 struct gin_account_receive *receiver=NULL;
 struct gin_account_dispatch *dispatcher=NULL;
 struct gin_account_sinks *sinks=NULL;
 struct gin_account_output *output=NULL;
 struct gin_connect_settings settings;
 struct gin_account_destination destinations[GIN_CONNECT_TOPICS_MAX];
 int64_t anchors[GIN_CONNECT_TOPICS_MAX];
 struct gin_http_control control={boundary,&o,0};
 struct gin_input parsers[64];
 unsigned wait=0;size_t nc,wc,i,j,count=0;
 uint64_t query_pending=0;
 memset(&o,0,sizeof(o));o.in=in;o.starting=1;o.result.code=GIN_ACCOUNT_OWNER_INVALID;
 memset(parsers,0,sizeof(parsers));
 if(!in || !in->root || !in->plan || !in->topology || !in->settings ||
    !in->credential || !in->chantypes || in->encryption!=GIN_RENDER_MODE_PLAIN) return o.result;
 o.result.code=GIN_ACCOUNT_OWNER_OK;
 if(boundary(&o,&wait)) goto done;
 if(o.now>UINT64_MAX-60000u) {o.result.code=GIN_ACCOUNT_OWNER_CLOCK;goto done;}
 o.deadline=o.now+60000u;
 /* The facade's one-shot start has three synchronous transfers (config, /me,
  * upgrade), without a controlled-start entrypoint. Conserve the aggregate
  * allowance across all three, rather than granting 60 seconds to each.
  * Stop is checked on either side, not inside that borrowed legacy API. */
 settings=*in->settings;
 if(boundary(&o,&wait)) goto done;
 {
  long cap=(long)((o.deadline-o.now)/3u);
  if(cap<1) {o.result.code=GIN_ACCOUNT_OWNER_DEADLINE;goto done;}
  if(settings.http.total_ms>cap) settings.http.total_ms=cap;
  if(settings.http.connect_ms>settings.http.total_ms) settings.http.connect_ms=settings.http.total_ms;
 }
 o.result.receive=gin_account_receive_new(&settings,in->credential,in->plan,&receiver);
 if(o.result.receive!=GIN_CONNECT_OK) {o.result.code=GIN_ACCOUNT_OWNER_RECEIVE;goto done;}
 o.result.dispatch=gin_account_dispatch_new(receiver,in->plan,in->topology,in->chantypes,in->encryption,&dispatcher);
 if(o.result.dispatch!=GIN_SESSION_OK) {o.result.code=GIN_ACCOUNT_OWNER_DISPATCH;goto done;}
 if(boundary(&o,&wait)) goto done;
 o.result.receive=gin_account_receive_start(receiver);
 if(o.result.receive!=GIN_CONNECT_OK) {o.result.code=GIN_ACCOUNT_OWNER_RECEIVE;goto done;}
 while(gin_account_receive_state(receiver)!=GIN_CONNECT_READY || !gin_account_receive_drained(receiver))
  if(service(&o,receiver,10)) goto done;
 if(boundary(&o,&wait)) goto done;
 if(in->phase) in->phase(in->phase_context,1);
 if(boundary(&o,&wait)) goto done;
 {struct gin_account_sinks_result r=gin_account_sinks_new(in->root,in->credential,in->plan,in->chantypes,&sinks);
  o.result.sinks=r;
  o.result.cleanup_failed|=r.cleanup_failed;
  if(r.code!=GIN_ACCOUNT_SINKS_OK) {o.result.code=GIN_ACCOUNT_OWNER_SINKS;goto done;}}
 if(boundary(&o,&wait)) goto done;
 {struct gin_account_output_result r=gin_account_output_new(in->root,sinks,in->plan,receiver,in->chantypes,in->encryption,&output);
  o.result.output=r;
  o.result.cleanup_failed|=r.cleanup_failed;
  if(r.code!=GIN_CHECKPOINT_OK) {o.result.code=GIN_ACCOUNT_OWNER_OUTPUT;goto done;}}
 nc=gin_account_plan_network_count(in->plan);wc=gin_account_plan_window_count(in->plan);
 if(nc>GIN_CONNECT_TOPICS_MAX || wc>GIN_CONNECT_TOPICS_MAX-nc) {o.result.code=GIN_ACCOUNT_OWNER_INVALID;goto done;}
 count=nc+wc;
 for(i=0;i<count;i++) {
  struct gin_account_destination *d=&destinations[i];struct gin_checkpoint_progress p;
  memset(d,0,sizeof(*d));d->window_index=SIZE_MAX;d->network_index=i;d->role=GIN_RENDER_STATUS;
  if(i>=nc) {
   const struct gin_topology_window *w=gin_account_plan_window_at(in->plan,i-nc);
   for(j=0;j<nc;j++) if(gin_account_plan_network_at(in->plan,j)->id==w->network_id) break;
   if(j==nc) {o.result.code=GIN_ACCOUNT_OWNER_INVALID;goto done;}
   d->network_index=j;d->window_index=i-nc;d->role=w->role==GIN_REMOTE_CHANNEL?GIN_RENDER_CHANNEL:GIN_RENDER_QUERY;
  }
  if(gin_account_output_progress(output,d,&p)) {o.result.code=GIN_ACCOUNT_OWNER_OUTPUT;goto done;}
  anchors[i]=p.anchor;
 }
 {struct gin_account_history_bootstrap_input b={receiver,dispatcher,output,in->plan,in->topology,&in->settings->http,&in->settings->policy,in->credential,in->chantypes,in->encryption,&control};
  o.result.history=gin_account_history_bootstrap(&b);
  o.result.cleanup_failed|=o.result.history.cleanup_failed;
  o.result.quarantined|=o.result.history.quarantined;
  if(o.result.history.code!=GIN_HISTORY_OK || o.result.history.incomplete) {
   if(o.result.code==GIN_ACCOUNT_OWNER_OK && !o.result.stopped) o.result.code=GIN_ACCOUNT_OWNER_HISTORY;
   goto done;
  }
 }
 if(boundary(&o,&wait)) goto done;
 o.starting=0;
 o.receiver=receiver;o.dispatcher=dispatcher;o.output=output;
 o.destinations=destinations;o.anchors=anchors;o.count=count;
 if(in->phase) in->phase(in->phase_context,2);
 for(;;) {
  if(service(&o,receiver,10)) goto done;
  if(drain(&o)) goto done;
  query_take(receiver,fifo,&query_pending);
  if(query_pending) continue;
  /* One bounded parser step per descriptor per sweep; EOF/AGAIN never retry
   * inline. Borrow sinks without changing flags or opening the root lock. */
  for(i=0;fifo && i<count;i++) {
   struct gin_record record;enum gin_input_result ir;
   int fd=i<nc?gin_account_sinks_network_input(sinks,i):gin_account_sinks_window_input(sinks,i-nc);
   if(boundary(&o,&wait)) goto done;
   ir=gin_input_next(&parsers[i],fd,&record);
   if(ir==GIN_INPUT_ERROR) {o.result.code=GIN_ACCOUNT_OWNER_INVALID;goto done;}
   if(ir==GIN_INPUT_LINE && i<nc) {
    struct gin_command command;
    const struct gin_topology_network *n=gin_account_plan_network_at(in->plan,i);
    struct gin_send_route route;
    struct gin_send_settings send_settings;
    struct gin_http_control joining={send_tick,&o,0};
    struct gin_root borrowed=*in->root;
    if(gin_command_parse(record.data,record.length,in->chantypes,&command)!=GIN_COMMAND_OK ||
       (command.kind!=GIN_COMMAND_JOIN && command.kind!=GIN_COMMAND_PART && command.kind!=GIN_COMMAND_QUERY)) continue;
    if(fifo->operations==UINT64_MAX) {o.result.code=GIN_ACCOUNT_OWNER_INVALID;goto done;}
    if(command.kind==GIN_COMMAND_QUERY) {
     fifo->operations++;
     fifo->query_submission_id=fifo->operations;fifo->has_query_submission=1;
     fifo->query_submission_code=gin_account_receive_open_query(receiver,in->topology,
      n->slug,(const char *)command.data,in->chantypes,GIN_QUERY_MODE_PLAIN,fifo->operations,5000);
     if(fifo->query_submission_code==GIN_QUERY_OK) {
      query_pending=fifo->operations;break;
     }
     continue;
    }
    route.network_slug=n->slug;route.wire_target=(const char *)command.data;
    route.chantypes=in->chantypes;route.role=GIN_SEND_ROLE_CHANNEL;
    route.mode=GIN_SEND_MODE_PLAIN;
    send_settings.auth=in->settings->http;send_settings.protocol=in->settings->policy;
    fifo->operations++;
    /* Descriptor-only borrowed copy: no close, mutation or ownership transfer.
     * Retain the actual typed outcome before even an orderly outer stop. */
    fifo->control_terminal=command.kind==GIN_COMMAND_PART ?
     gin_control_part_controlled(&borrowed,&send_settings,in->credential,&route,fifo->operations,&joining) :
     gin_control_join_controlled(&borrowed,&send_settings,in->credential,&route,fifo->operations,&joining);
    fifo->has_control_terminal=1;
    if(boundary(&o,&wait) || o.result.code!=GIN_ACCOUNT_OWNER_OK || o.result.terminal) goto done;
   }
   if(ir==GIN_INPUT_LINE && i>=nc) {
    const struct gin_topology_window *w=gin_account_plan_window_at(in->plan,i-nc);
    const struct gin_topology_network *n;
    struct gin_send_route route;struct gin_send_settings send_settings;
    struct gin_http_control sending={send_tick,&o,0};
    if((w->role!=GIN_REMOTE_CHANNEL && w->role!=GIN_REMOTE_QUERY) || record.data[0]=='/') continue;
    for(j=0;j<nc;j++) if(gin_account_plan_network_at(in->plan,j)->id==w->network_id) break;
    if(j==nc || fifo->operations==UINT64_MAX) {o.result.code=GIN_ACCOUNT_OWNER_INVALID;goto done;}
    n=gin_account_plan_network_at(in->plan,j);
    route.network_slug=n->slug;route.wire_target=w->name.original_wire_target;
    route.chantypes=in->chantypes;
    route.role=w->role==GIN_REMOTE_CHANNEL?GIN_SEND_ROLE_CHANNEL:GIN_SEND_ROLE_QUERY;
    route.mode=GIN_SEND_MODE_PLAIN;
    send_settings.auth=in->settings->http;send_settings.protocol=in->settings->policy;
    fifo->operations++;
    /* Publish the actual result before any outer stop/error branch. */
    fifo->terminal=gin_send_plain_controlled(in->root,&send_settings,in->credential,&route,fifo->operations,record.data,record.length,&sending);
    fifo->has_terminal=1;
    if(boundary(&o,&wait) || o.result.code!=GIN_ACCOUNT_OWNER_OK || o.result.terminal) goto done;
   }
  }
 }
 done:
 /* Every early service/drain/stop exit converges here. Preserve an already
  * completed receipt first; close only resolves a still-pending operation. */
 query_take(receiver,fifo,&query_pending);
 if(query_pending) {
  gin_account_receive_close(receiver);
  query_take(receiver,fifo,&query_pending);
 }
 if(output && gin_account_output_close(&output)) o.result.cleanup_failed=1;
 gin_account_dispatch_free(dispatcher);
 if(sinks && gin_account_sinks_close(sinks)) o.result.cleanup_failed=1;
 if(receiver) {
  gin_account_receive_close(receiver);
  o.result.cleanup_failed|=gin_account_receive_cleanup_failed(receiver);
  o.result.quarantined|=gin_account_receive_quarantined(receiver);
  if(!o.result.quarantined) gin_account_receive_free(receiver);
 }
 if(o.result.code==GIN_ACCOUNT_OWNER_OK && in) (void)boundary(&o,&wait);
 if(o.result.code==GIN_ACCOUNT_OWNER_OK && o.result.cleanup_failed) o.result.code=GIN_ACCOUNT_OWNER_CLEANUP;
 return o.result;
}
struct gin_account_owner_result gin_account_owner_run(const struct gin_account_owner_input *in)
{
 return run(in,NULL);
}
struct gin_account_fifo_result gin_account_owner_run_fifo(const struct gin_account_owner_input *in)
{
 struct gin_account_fifo_result r;
 memset(&r,0,sizeof(r));r.owner=run(in,&r);return r;
}
