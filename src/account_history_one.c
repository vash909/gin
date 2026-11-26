#define _POSIX_C_SOURCE 200809L
#include "account_history_one.h"
#include "account_sinks_hot.h"
#include "history_values.h"
#include "gin_naming.h"
#include "connect_values.h"
#include <string.h>
#include <time.h>

#define ONE_DEST 64u
struct one_destination {
 struct gin_account_destination destination;
 struct gin_history_scope scope;
 int64_t anchor;
 size_t base;
};
struct one_owner {
 const struct gin_account_history_one_input *in;
 struct gin_account_history_one_result result;
 struct one_destination destinations[ONE_DEST];
 size_t count, selected;
 struct { size_t destination; int64_t id; } identities[GIN_HISTORY_LIVE_ROWS_MAX];
 size_t identities_count;
 struct gin_history_recovery *recovery;
 const struct gin_topology *topology;
 uint64_t now;
 int inflight;
};
static enum gin_history_code one_check(struct one_owner *o)
{
 struct timespec ts;
 const struct gin_account_identity *id;
 const struct gin_account_registry_scope *s=o->in->scope;
 struct gin_account_dispatch_hot_result ds;
 struct gin_account_output_hot_result os;
 uint64_t now,seconds;
 if(clock_gettime(CLOCK_MONOTONIC,&ts) || ts.tv_sec<0 || ts.tv_nsec<0 || ts.tv_nsec>=1000000000L)
  return GIN_HISTORY_INVALID;
 seconds=(uint64_t)ts.tv_sec;
 if(seconds>(UINT64_MAX-999u)/1000u) return GIN_HISTORY_INVALID;
 now=seconds*1000u+(uint64_t)ts.tv_nsec/1000000u;
 if(now<o->now) return GIN_HISTORY_INVALID;
 o->now=now;
 if(now>=o->in->deadline_ms) return GIN_HISTORY_DEADLINE;
 if(gin_account_receive_state(o->in->receiver)!=GIN_CONNECT_READY ||
    gin_account_receive_epoch(o->in->receiver)!=s->epoch) return GIN_HISTORY_STALE;
 id=gin_account_receive_identity(o->in->receiver);
 /* Display identity belongs to the current receiver/registry, not the stored
  * credential's historical display label. UUID binding is independent. */
 if(!id || !id->origin || !id->subject_id || !id->subject_name ||
    strcmp(id->origin,o->in->credential->origin) || strcmp(id->origin,s->origin) ||
    strcmp(id->subject_id,o->in->credential->subject_id) || strcmp(id->subject_id,s->subject_uuid) ||
    strcmp(id->subject_name,s->topic_subject)) return GIN_HISTORY_ROUTE;
 ds=gin_account_dispatch_hot_status(o->in->dispatcher,o->in->receiver,o->in->registry,s,o->in->generation,o->in->window_index);
 os=gin_account_output_hot_status(o->in->output,o->in->root,o->in->sinks,o->in->plan,
    o->in->receiver,o->in->registry,s,o->in->generation,o->in->window_index);
 if(ds.code!=GIN_SESSION_OK || ds.proposal_pending || !ds.window_admitted ||
    os.code!=GIN_ACCOUNT_OUTPUT_HOT_OK || !os.window_admitted) return GIN_HISTORY_ROUTE;
 return GIN_HISTORY_OK;
}
static enum gin_history_code one_boundary(struct one_owner *o,unsigned *wait)
{
 enum gin_history_code c=one_check(o);
 if(c==GIN_HISTORY_OK && o->in->stop &&
    (o->in->stop->aborted || (o->in->stop->tick && o->in->stop->tick(o->in->stop->opaque,wait)))) {
  o->in->stop->aborted=1;o->result.stopped=1;return GIN_HISTORY_IGNORE;
 }
 /* A stop callback must not consume time outside the conserved episode. */
 return c==GIN_HISTORY_OK?one_check(o):c;
}
static size_t one_incoming(const struct one_destination *d,size_t rows)
{
 /* Same owned-page layout as history_page.c: scope strings, row array and
  * decoded strings bounded by valid wire bytes, plus conservative row NULs.
  * Raw HTTP, DOM, Native queue and fixed bookkeeping are separate bounds. */
 return d->base+GIN_HISTORY_RESPONSE_MAX+rows*(sizeof(struct gin_history_row)+8u);
}
static int one_room(size_t *b,size_t *n,size_t bytes,size_t rows)
{
 if(*b>GIN_HISTORY_OWNED_MAX || *n>GIN_HISTORY_ROWS_MAX ||
    bytes>GIN_HISTORY_OWNED_MAX-*b || rows>GIN_HISTORY_ROWS_MAX-*n) return 0;
 *b+=bytes;*n+=rows;return 1;
}
static enum gin_history_code one_reserve(struct one_owner *o,size_t incoming,size_t rows,int replacement)
{
 const struct gin_history_page *view=gin_history_recovery_view(o->recovery);
 size_t old=gin_history_page_owned_bytes(view),nr=gin_history_page_count(view),b=old,n=nr;
 if(!one_room(&b,&n,incoming,rows)) return GIN_HISTORY_LIMIT;
 if(replacement && !one_room(&b,&n,old+incoming,nr+rows)) return GIN_HISTORY_LIMIT;
 if(o->inflight) {
  size_t add=one_incoming(&o->destinations[o->selected],GIN_HISTORY_PAGE_ROWS_MAX);
  /* Keep this reservation through post-fetch drain AND page destruction.
   * A concurrent LIVE replacement can enlarge the next REST merge's old view. */
  if(replacement) {old+=incoming;nr+=rows;}
  if(!one_room(&b,&n,2u*add+old,2u*GIN_HISTORY_PAGE_ROWS_MAX+nr)) return GIN_HISTORY_LIMIT;
 }
 return GIN_HISTORY_OK;
}
static enum gin_history_code one_drain(struct one_owner *o)
{
 while(gin_account_receive_peek(o->in->receiver)) {
  struct gin_account_destination destination;
  struct gin_render_record *record=NULL;
  struct gin_history_page *page=NULL;
  struct gin_account_inventory *pending=NULL;
  const struct gin_history_row *row;
  size_t i,j,incoming=0;unsigned wait=0;
  enum gin_history_code c=one_boundary(o,&wait);
  if(c!=GIN_HISTORY_OK) return c;
  /* Reserve before dispatcher allocation; any queued destination might be the
   * selected query. The bounded temporary render record is accounted separately. */
  for(i=0;i<o->count;i++) {
   size_t bytes=one_incoming(&o->destinations[i],1);
   if(bytes>incoming) incoming=bytes;
  }
  c=one_reserve(o,incoming,1,1);
  if(c!=GIN_HISTORY_OK) return c;
  if(gin_account_dispatch_next_inventory_typed(o->in->dispatcher,&destination,&record,&page,&pending)!=GIN_SESSION_OK) {
   gin_render_record_free(record);gin_history_page_free(page);return GIN_HISTORY_ROUTE;
  }
  if(pending) {
   /* Even unchanged generation replaces adopted topology on resolve. Unwind
    * HTTP before returning this exact proposal to its sole caller owner. */
   o->result.pending=pending;
   gin_render_record_free(record);gin_history_page_free(page);return GIN_HISTORY_STALE;
  }
  if(!record && !page) continue;
  c=GIN_HISTORY_UNSUPPORTED;
  if(!record || !page || destination.terminal) goto discard;
  for(i=0;i<o->count;i++) {
   const struct gin_account_destination *d=&o->destinations[i].destination;
   if(d->network_index==destination.network_index && d->window_index==destination.window_index && d->role==destination.role) break;
  }
  c=GIN_HISTORY_ROUTE;
  if(i==o->count || gin_history_page_source(page)!=GIN_HISTORY_LIVE || gin_history_page_count(page)!=1 ||
     !gin_history_scope_equal(&o->destinations[i].scope,gin_history_page_scope(page))) goto discard;
  row=gin_history_page_row(page,0);
  if(!row || row->id<=0) {c=GIN_HISTORY_UNSUPPORTED;goto discard;}
  for(j=0;j<o->identities_count;j++) if(o->identities[j].destination==i && o->identities[j].id==row->id) break;
  if(j==o->identities_count) {
   if(j==GIN_HISTORY_LIVE_ROWS_MAX) {c=GIN_HISTORY_LIMIT;goto discard;}
   o->identities[j].destination=i;o->identities[j].id=row->id;o->identities_count++;
  }
  c=one_boundary(o,&wait);if(c!=GIN_HISTORY_OK) goto discard;
  if(i==o->selected) {
   c=one_reserve(o,gin_history_page_owned_bytes(page),1,1);
   if(c==GIN_HISTORY_OK) c=gin_history_recovery_live(o->recovery,page,o->now);
  } else {
   o->result.output_attempted=1;
   o->result.output=gin_account_output_append(o->in->output,&destination,record,o->destinations[i].anchor);
   o->result.cleanup_failed|=o->result.output.cleanup_failed|o->result.output.checkpoint.cleanup_failed;
   c=o->result.output.code==GIN_CHECKPOINT_OK?GIN_HISTORY_OK:GIN_HISTORY_REMOTE;
   if(c==GIN_HISTORY_OK) o->result.old_appended++;
  }
 discard:
  gin_render_record_free(record);gin_history_page_free(page);
  if(c!=GIN_HISTORY_OK) return c;
 }
 return GIN_HISTORY_OK;
}
static int one_tick(void *opaque,unsigned *wait)
{
 struct one_owner *o=opaque;struct timespec wall;
 enum gin_history_code c=one_boundary(o,wait);
 /* Hot receiver contract: retained work MUST drain before service, not after
  * a zero-wait service that can reject a nonempty queue. */
 if(c==GIN_HISTORY_OK) c=one_drain(o);
 if(c==GIN_HISTORY_OK) {
  if(clock_gettime(CLOCK_REALTIME,&wall) || wall.tv_sec<0 || wall.tv_nsec<0 || wall.tv_nsec>=1000000000L ||
     (uint64_t)wall.tv_sec>((uint64_t)INT64_MAX-999u)/1000u) c=GIN_HISTORY_INVALID;
  else if(gin_account_receive_service(o->in->receiver,0,(int64_t)wall.tv_sec*1000+wall.tv_nsec/1000000)!=GIN_CONNECT_OK) c=GIN_HISTORY_TRANSPORT;
 }
 if(c==GIN_HISTORY_OK) c=one_drain(o);
 if(c==GIN_HISTORY_OK) c=one_boundary(o,wait);
 if(c==GIN_HISTORY_OK && gin_account_receive_http_wait_budget(o->in->receiver,wait)!=GIN_CONNECT_OK) c=GIN_HISTORY_TRANSPORT;
 if(c==GIN_HISTORY_OK) c=one_reserve(o,0,0,0);
 if(c==GIN_HISTORY_OK && (uint64_t)*wait>o->in->deadline_ms-o->now) *wait=(unsigned)(o->in->deadline_ms-o->now);
 if(c!=GIN_HISTORY_OK) {o->result.code=c;return 1;}
 return 0;
}
static int one_joined(struct one_owner *o,size_t i)
{
 const struct gin_connect_subscription *sub=gin_account_registry_source_at(o->in->registry,i);
 const struct gin_connect_topic_status *source=gin_account_receive_source(o->in->receiver,i);
 char topic[GIN_WS_TOPIC_CAP];size_t length;
 return sub && source && source->state==GIN_CONNECT_TOPIC_JOINED && source->join_ref && *source->join_ref &&
  gin_ws_topic(o->in->scope->topic_subject,sub->network,sub->target,topic,sizeof(topic),&length)==GIN_WS_VALUE_OK && !strcmp(source->topic,topic);
}
static enum gin_history_code one_snapshot(struct one_owner *o)
{
 const struct gin_account_history_one_input *in=o->in;
 size_t nc=gin_account_registry_network_count(in->registry),wc=gin_account_registry_window_count(in->registry),i,j;
 if(nc>ONE_DEST || wc>ONE_DEST-nc) return GIN_HISTORY_LIMIT;
 o->selected=SIZE_MAX;
 for(i=0;i<nc+wc;i++) {
  struct one_destination *d=&o->destinations[o->count];
  const struct gin_topology_window *w=NULL;
  const struct gin_topology_network *net;
  struct gin_checkpoint_progress progress;
  struct gin_history_request request={GIN_HISTORY_AFTER,0,GIN_HISTORY_PAGE_DEFAULT};
  char path[GIN_HISTORY_PATH_MAX+1];size_t length;
  if(i>=nc) {
   struct gin_account_output_hot_result os=gin_account_output_hot_status(in->output,in->root,in->sinks,in->plan,in->receiver,in->registry,in->scope,in->generation,i-nc);
   struct gin_account_dispatch_hot_result ds=gin_account_dispatch_hot_status(in->dispatcher,in->receiver,in->registry,in->scope,in->generation,i-nc);
   if(os.code!=GIN_ACCOUNT_OUTPUT_HOT_OK || ds.code!=GIN_SESSION_OK || ds.proposal_pending) return GIN_HISTORY_ROUTE;
   if(!os.window_admitted || !ds.window_admitted) continue;
   w=gin_account_registry_window_at(in->registry,i-nc);
   if(!w) return GIN_HISTORY_ROUTE;
   for(j=0;j<nc;j++) if(gin_account_registry_network_at(in->registry,j)->id==w->network_id) break;
   if(j==nc) return GIN_HISTORY_ROUTE;
   d->destination.network_index=j;d->destination.window_index=i-nc;
  } else {d->destination.network_index=i;d->destination.window_index=SIZE_MAX;}
  net=gin_account_registry_network_at(in->registry,d->destination.network_index);
  if(!net) return GIN_HISTORY_ROUTE;
  d->scope.origin=in->scope->origin;d->scope.subject_id=in->scope->subject_uuid;
  d->scope.network_id=net->id;d->scope.network_slug=net->slug;d->scope.own_nick=net->own_nick;
  d->scope.target=w?w->name.original_wire_target:"$server";d->scope.chantypes=in->scope->chantypes;
  d->scope.socket_epoch=in->scope->epoch;d->scope.encryption=in->encryption;
  d->scope.role=w?(w->role==GIN_REMOTE_QUERY?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL):GIN_RENDER_STATUS;
  d->destination.role=d->scope.role;
  if(gin_history_scope_check(&d->scope)!=GIN_HISTORY_OK ||
     gin_history_path(&d->scope,&request,path,sizeof(path),&length)!=GIN_HISTORY_OK) return GIN_HISTORY_ROUTE;
  if(gin_account_output_progress(in->output,&d->destination,&progress) || progress.anchor<0) return GIN_HISTORY_ROUTE;
  d->anchor=progress.anchor;
  d->base=sizeof(struct gin_history_page)+strlen(d->scope.origin)+strlen(d->scope.subject_id)+strlen(d->scope.network_slug)+
   strlen(d->scope.target)+strlen(d->scope.chantypes)+strlen(d->scope.own_nick)+6u;
  if(w && i-nc==in->window_index) o->selected=o->count;
  o->count++;
 }
 if(o->selected==SIZE_MAX) return GIN_HISTORY_ROUTE;
 /* Validate actual selected mapping and the adopted HTTP route before any
  * queue consumption. No sorted ordinal is an authority for this destination. */
 {
  const struct gin_history_scope *s=&o->destinations[o->selected].scope;
  const struct gin_topology_network *net=gin_topology_find_id(o->topology,s->network_id);
  const struct gin_topology_window *w;
  struct gin_name name;int found=0;
  if(!net || strcmp(net->slug,s->network_slug) || strcmp(net->own_nick,s->own_nick) ||
     gin_topology_find_slug(o->topology,s->network_slug)!=net || !gin_topology_queries_known(o->topology) ||
     gin_name_prepare(s->network_slug,s->target,s->chantypes,&name)) return GIN_HISTORY_ROUTE;
  w=gin_topology_find_window(o->topology,s->network_id,name.canonical_target);
  if(!w || w->role!=GIN_REMOTE_QUERY) return GIN_HISTORY_ROUTE;
  for(i=0;i<gin_account_registry_source_count(in->registry);i++) {
   const struct gin_connect_subscription *sub=gin_account_registry_source_at(in->registry,i);
   if(!one_joined(o,i)) return GIN_HISTORY_ROUTE;
   if(sub->network && sub->target && !strcmp(sub->network,s->network_slug) && !strcmp(sub->target,s->target)) found=1;
  }
  if(!found) return GIN_HISTORY_ROUTE;
 }
 return GIN_HISTORY_OK;
}
struct gin_account_history_one_result gin_account_history_one(const struct gin_account_history_one_input *in)
{
 struct one_owner o;
 struct gin_http_control control={one_tick,&o,0};
 size_t wc,j;unsigned wait=0;
 memset(&o,0,sizeof(o));o.in=in;o.result.code=GIN_HISTORY_INVALID;o.result.incomplete=1;
 if(!in || !in->root || !in->sinks || !in->plan || !in->receiver || !in->dispatcher || !in->output ||
    !in->registry || !in->scope || !in->http || !in->policy || !in->credential ||
    !in->scope->origin || !in->scope->subject_uuid || !in->scope->topic_subject || !in->scope->chantypes || !in->scope->epoch) return o.result;
 if(!gin_http_origin_valid(in->http->origin) || gin_credential_validate(in->credential)!=GIN_CREDENTIAL_OK ||
    strcmp(in->http->origin,in->credential->origin) || in->http->connect_ms<=0 ||
    in->http->total_ms<=0 || in->http->connect_ms>in->http->total_ms || in->http->total_ms>GIN_HTTP_TIMEOUT_MAX ||
    in->policy->client_protocol<=0 || in->policy->required_server_protocol<=0) return o.result;
 if(in->encryption!=GIN_RENDER_MODE_PLAIN) {o.result.code=GIN_HISTORY_UNSUPPORTED;return o.result;}
 wc=gin_account_registry_window_count(in->registry);
 if(!in->new_window_count || in->new_window_first>wc || in->new_window_count>wc-in->new_window_first ||
    in->window_index<in->new_window_first || in->window_index-in->new_window_first>=in->new_window_count ||
    !gin_account_registry_window_at(in->registry,in->window_index) ||
    gin_account_registry_window_at(in->registry,in->window_index)->role!=GIN_REMOTE_QUERY) return o.result;
 o.result.code=one_check(&o);if(o.result.code!=GIN_HISTORY_OK) return o.result;
 if(o.now>UINT64_MAX-GIN_HISTORY_ATTEMPT_MS_MAX || in->deadline_ms-o.now>GIN_HISTORY_ATTEMPT_MS_MAX) {o.result.code=GIN_HISTORY_INVALID;return o.result;}
 o.topology=gin_account_dispatch_hot_topology(in->dispatcher);
 if(!o.topology || gin_account_sinks_hot_status(in->sinks,in->root,in->plan,in->registry,in->scope,in->generation).code!=GIN_ACCOUNT_SINKS_HOT_OK) {o.result.code=GIN_HISTORY_ROUTE;return o.result;}
 o.result.code=one_snapshot(&o);if(o.result.code!=GIN_HISTORY_OK) return o.result;
 o.result.code=one_boundary(&o,&wait);if(o.result.code!=GIN_HISTORY_OK) goto done;
 o.result.code=gin_history_recovery_new(&o.destinations[o.selected].scope,o.destinations[o.selected].anchor,GIN_HISTORY_PAGE_DEFAULT,o.now,&o.recovery);
 if(o.result.code!=GIN_HISTORY_OK) goto done;
 for(;;) {
  struct gin_history_request request;
  struct gin_history_page *page=NULL;
  struct gin_history_fetch_result fetched;
  struct gin_history_status status;
  if(one_tick(&o,&wait)) goto done;
  if(o.result.pages>=GIN_HISTORY_PAGES_MAX) {o.result.code=GIN_HISTORY_LIMIT;goto done;}
  o.result.code=gin_history_recovery_next(o.recovery,o.now,&request);if(o.result.code!=GIN_HISTORY_OK) goto done;
  o.inflight=1;
  o.result.code=one_reserve(&o,0,0,0);if(o.result.code!=GIN_HISTORY_OK) goto done;
  fetched=gin_history_fetch_controlled(in->http,in->policy,in->credential,o.topology,&o.destinations[o.selected].scope,&request,&page,&control);
  if(!control.aborted && !one_tick(&o,&wait)) o.result.code=fetched.code;
  if(o.result.code==GIN_HISTORY_OK) {
   /* The worst REST reservation covers this owned page and replacement. Keep
    * it live until free, but do not double-count the same page a third time. */
   o.result.code=gin_history_recovery_page(o.recovery,page,o.now);
  }
  gin_history_page_free(page);o.inflight=0;
  if(o.result.code!=GIN_HISTORY_OK) goto done;
  o.result.pages++;
  status=gin_history_recovery_status(o.recovery);
  if(status.state==GIN_HISTORY_INCOMPLETE_LIMIT) {o.result.code=GIN_HISTORY_LIMIT;goto done;}
  if(status.state!=GIN_HISTORY_VISIBLE_DRAINED) continue;
  {
   const struct gin_history_page *view=gin_history_recovery_view(o.recovery);
   for(j=0;j<gin_history_page_count(view);j++) {
    struct gin_account_history_output_result written;
    o.result.code=one_boundary(&o,&wait);if(o.result.code!=GIN_HISTORY_OK) goto done;
    written=gin_account_output_append_history(in->output,&o.destinations[o.selected].destination,view,j,o.destinations[o.selected].anchor);
    if(written.history==GIN_HISTORY_IGNORE) continue;
    if(written.history==GIN_HISTORY_OK) {o.result.output_attempted=1;o.result.output=written.output;}
    o.result.cleanup_failed|=written.output.cleanup_failed|written.output.checkpoint.cleanup_failed;
    o.result.code=written.history;
    if(o.result.code==GIN_HISTORY_OK && written.output.code!=GIN_CHECKPOINT_OK) o.result.code=GIN_HISTORY_REMOTE;
    if(o.result.code!=GIN_HISTORY_OK) goto done;
    o.result.appended++;
   }
  }
  o.result.code=one_boundary(&o,&wait);
  if(o.result.code==GIN_HISTORY_OK) o.result.drained=1;
  break;
 }
 done:
 o.result.cleanup_failed|=gin_account_receive_cleanup_failed(in->receiver);
 o.result.quarantined=gin_account_receive_quarantined(in->receiver);
 if(o.result.code==GIN_HISTORY_OK && (o.result.cleanup_failed || o.result.quarantined)) o.result.code=GIN_HISTORY_TRANSPORT;
 o.result.incomplete=o.result.code!=GIN_HISTORY_OK || !o.result.drained;
 if(!o.result.incomplete && !o.result.stopped) {o.result.retained=o.recovery;o.recovery=NULL;}
 gin_history_recovery_free(o.recovery);
 return o.result;
}
