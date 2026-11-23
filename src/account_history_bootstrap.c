#define _POSIX_C_SOURCE 200809L
#include "account_history_bootstrap.h"
#include "history_values.h"
#include <string.h>
#include <time.h>

#define BOOT_DEST 64u
struct boot_destination {
 struct gin_account_destination destination;
 struct gin_history_scope scope;
 struct gin_history_recovery *recovery;
 int64_t anchor;
 size_t base;
 int done;
};
struct boot_owner {
 const struct gin_account_history_bootstrap_input *in;
 struct gin_account_history_bootstrap_result result;
 struct boot_destination destinations[BOOT_DEST];
 struct gin_history_page *late[GIN_HISTORY_LIVE_ROWS_MAX];
 size_t late_destination[GIN_HISTORY_LIVE_ROWS_MAX], late_count;
 struct { size_t destination; int64_t id; } identities[GIN_HISTORY_LIVE_ROWS_MAX];
 size_t identities_count;
 uint64_t epoch, now, deadline;
 size_t inflight; /* SIZE_MAX unless one controlled fetch owns a reservation. */
};
static enum gin_history_code boot_check(struct boot_owner *o)
{
 struct timespec ts;
 const struct gin_account_identity *id;
 uint64_t now;
 if(clock_gettime(CLOCK_MONOTONIC,&ts) || ts.tv_sec<0 ||
    (uint64_t)ts.tv_sec>UINT64_MAX/1000u) return GIN_HISTORY_INVALID;
 now=(uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
 if(now<o->now) return GIN_HISTORY_INVALID;
 o->now=now;
 if(o->deadline && now>=o->deadline) return GIN_HISTORY_DEADLINE;
 if(gin_account_receive_epoch(o->in->receiver)!=o->epoch ||
    gin_account_receive_state(o->in->receiver)!=GIN_CONNECT_READY) return GIN_HISTORY_STALE;
 id=gin_account_receive_identity(o->in->receiver);
 if(!id || strcmp(id->origin,o->in->credential->origin) ||
    strcmp(id->origin,o->in->http->origin) ||
    strcmp(id->subject_id,o->in->credential->subject_id) ||
    strcmp(id->subject_name,o->in->credential->subject_name)) return GIN_HISTORY_ROUTE;
 return GIN_HISTORY_OK;
}
/* Owner-only boundary: no Native service/drain recursion or callback I/O. */
static enum gin_history_code boot_boundary(struct boot_owner *o,unsigned *wait)
{
 enum gin_history_code c=boot_check(o);
 if(c==GIN_HISTORY_OK && o->in->stop &&
    (o->in->stop->aborted || (o->in->stop->tick && o->in->stop->tick(o->in->stop->opaque,wait)))) {
  o->in->stop->aborted=1;o->result.stopped=1;c=GIN_HISTORY_IGNORE;
 }
 return c;
}
static size_t boot_incoming(const struct boot_destination *d, size_t rows)
{
 /* history_page.c copies one object, six scope strings, a row array and
  * six nullable row strings. Decoded string bytes cannot exceed valid wire
  * bytes. Eight NULs per row is deliberately conservative. No opaque mutation. */
 return d->base+GIN_HISTORY_RESPONSE_MAX+rows*(sizeof(struct gin_history_row)+8u);
}
static void boot_totals(struct boot_owner *o,size_t *bytes,size_t *rows)
{
 size_t i;
 *bytes=0;*rows=0;
 for(i=0;i<o->result.destinations;i++) {
  const struct gin_history_page *p=gin_history_recovery_view(o->destinations[i].recovery);
  *bytes+=gin_history_page_owned_bytes(p);*rows+=gin_history_page_count(p);
 }
 for(i=0;i<o->late_count;i++) {
  *bytes+=gin_history_page_owned_bytes(o->late[i]);*rows+=gin_history_page_count(o->late[i]);
 }
}
static int boot_room(size_t *bytes,size_t *rows,size_t b,size_t r)
{
 if(b>GIN_HISTORY_OWNED_MAX-*bytes || r>GIN_HISTORY_ROWS_MAX-*rows) return 0;
 *bytes+=b;*rows+=r;return 1;
}
static enum gin_history_code boot_reserve(struct boot_owner *o,size_t selected,size_t incoming,size_t rows,int replacement)
{
 size_t b,n;
 boot_totals(o,&b,&n);
 /* Peak, not retained-after-allocation: all current views plus incoming plus
  * a full replacement while recovery.c still owns its old view. The sum of
  * old and incoming owned bytes overestimates the replacement by one base.
  * Count duplicates conservatively too. Never evict or retry after LIMIT.
  * During HTTP every live admission also renews the pending REST reservation
  * against the changed views, before allocating or replacing anything. */
 if(!boot_room(&b,&n,incoming,rows)) return GIN_HISTORY_LIMIT;
 if(replacement) {
  const struct gin_history_page *p=gin_history_recovery_view(o->destinations[selected].recovery);
  if(!boot_room(&b,&n,gin_history_page_owned_bytes(p)+incoming,
      gin_history_page_count(p)+rows)) return GIN_HISTORY_LIMIT;
 }
 if(o->inflight!=SIZE_MAX) {
  const struct boot_destination *d=&o->destinations[o->inflight];
  const struct gin_history_page *p=gin_history_recovery_view(d->recovery);
  size_t add=boot_incoming(d,GIN_HISTORY_PAGE_ROWS_MAX);
  /* A live replacement for the in-flight destination may become its new
   * old view. Include the whole admitted candidate, without dedup credit. */
  size_t old=gin_history_page_owned_bytes(p), nr=gin_history_page_count(p);
  if(selected==o->inflight) { old+=incoming;nr+=rows; }
  if(!boot_room(&b,&n,2u*add+old,2u*GIN_HISTORY_PAGE_ROWS_MAX+nr)) return GIN_HISTORY_LIMIT;
 }
 return GIN_HISTORY_OK;
}
static int boot_seen_page(const struct gin_history_page *p,const struct gin_history_row *row)
{
 size_t i;
 for(i=0;i<gin_history_page_count(p);i++) {
  const struct gin_history_row *r=gin_history_page_row(p,i);
  if(r->id==row->id) return gin_history_row_equal(r,row)?1:-1;
 }
 return 0;
}
static enum gin_history_code boot_drain(struct boot_owner *o)
{
 while(gin_account_receive_peek(o->in->receiver)) {
  struct gin_account_destination destination;
  struct gin_render_record *record=NULL;
  struct gin_history_page *page=NULL;
  struct boot_destination *d;
  const struct gin_history_row *row;
  size_t i,j;int seen=0,known=0;
  unsigned wait=0;
  enum gin_history_code c=boot_boundary(o,&wait);
  if(c!=GIN_HISTORY_OK) return c;
  /* Admission precedes dispatcher allocations. Reserve worst valid one-row
   * decoded input and a replacement for any possible unfinished destination.
   * The pre-rendered bounded record, DOM and Native queue are separate bounds. */
  for(i=0;i<o->result.destinations;i++) {
   d=&o->destinations[i];
   c=boot_reserve(o,i,boot_incoming(d,1),1,!d->done);
   if(c!=GIN_HISTORY_OK) return c;
  }
  if(gin_account_dispatch_next(o->in->dispatcher,&destination,&record,&page)!=GIN_SESSION_OK) {
   gin_render_record_free(record);gin_history_page_free(page);return GIN_HISTORY_ROUTE;
  }
  if(!record && !page) continue; /* validated unchanged metadata only */
  c=GIN_HISTORY_UNSUPPORTED;
  if(!page || !record || destination.terminal) goto discard;
  for(i=0;i<o->result.destinations;i++) {
   const struct gin_account_destination *x=&o->destinations[i].destination;
   if(x->network_index==destination.network_index && x->window_index==destination.window_index && x->role==destination.role) break;
  }
  c=GIN_HISTORY_ROUTE;
  if(i==o->result.destinations) goto discard;
  d=&o->destinations[i];
  if(!gin_history_scope_equal(&d->scope,gin_history_page_scope(page)) ||
     gin_history_page_source(page)!=GIN_HISTORY_LIVE || gin_history_page_count(page)!=1) goto discard;
  row=gin_history_page_row(page,0);
  seen=boot_seen_page(gin_history_recovery_view(d->recovery),row);
  for(j=0;j<o->late_count && !seen;j++) if(o->late_destination[j]==i) seen=boot_seen_page(o->late[j],row);
  c=GIN_HISTORY_CONFLICT;if(seen<0) goto discard;
  for(j=0;j<o->identities_count;j++) if(o->identities[j].destination==i && o->identities[j].id==row->id) known=1;
  c=GIN_HISTORY_LIMIT;
  if(!known && o->identities_count==GIN_HISTORY_LIVE_ROWS_MAX) goto discard;
  if(!known) {
   o->identities[o->identities_count].destination=i;
   o->identities[o->identities_count++].id=row->id;
  }
  c=boot_boundary(o,&wait);if(c!=GIN_HISTORY_OK) goto discard;
  if(!d->done) {
   gin_render_record_free(record);record=NULL;
   c=gin_history_recovery_live(d->recovery,page,o->now);
  } else if(!seen) {
   struct gin_account_output_result written;
   if(o->late_count==GIN_HISTORY_LIVE_ROWS_MAX) {c=GIN_HISTORY_LIMIT;goto discard;}
   written=gin_account_output_append(o->in->output,&destination,record,d->anchor);
   o->result.cleanup_failed|=written.cleanup_failed;
   c=written.code==GIN_CHECKPOINT_OK?GIN_HISTORY_OK:GIN_HISTORY_REMOTE;
   if(c==GIN_HISTORY_OK) {
    o->result.appended++;
    o->late_destination[o->late_count]=i;o->late[o->late_count++]=page;page=NULL;
   }
  } else c=GIN_HISTORY_OK;
 discard:
  gin_render_record_free(record);gin_history_page_free(page);
  if(c!=GIN_HISTORY_OK) return c;
 }
 return GIN_HISTORY_OK;
}
static int boot_tick(void *opaque,unsigned *wait)
{
 struct boot_owner *o=opaque;struct timespec wall;
 enum gin_history_code c=boot_boundary(o,wait);
 if(c==GIN_HISTORY_OK) {
  if(clock_gettime(CLOCK_REALTIME,&wall) || wall.tv_sec<0 || (uint64_t)wall.tv_sec>(uint64_t)INT64_MAX/1000u) c=GIN_HISTORY_INVALID;
  else if(gin_account_receive_service(o->in->receiver,0,(int64_t)wall.tv_sec*1000+wall.tv_nsec/1000000)!=GIN_CONNECT_OK) c=GIN_HISTORY_TRANSPORT;
 }
 if(c==GIN_HISTORY_OK) c=boot_drain(o);
 if(c==GIN_HISTORY_OK) c=boot_boundary(o,wait);
 if(c==GIN_HISTORY_OK && gin_account_receive_http_wait_budget(o->in->receiver,wait)!=GIN_CONNECT_OK) c=GIN_HISTORY_TRANSPORT;
 if(c==GIN_HISTORY_OK && o->inflight!=SIZE_MAX) c=boot_reserve(o,SIZE_MAX,0,0,0);
 if(c==GIN_HISTORY_OK && (uint64_t)*wait>o->deadline-o->now) *wait=(unsigned)(o->deadline-o->now);
 if(c!=GIN_HISTORY_OK) {o->result.code=c;return 1;}
 return 0;
}
static size_t boot_base(const struct gin_history_scope *s)
{
 return sizeof(struct gin_history_page)+strlen(s->origin)+strlen(s->subject_id)+
  strlen(s->network_slug)+strlen(s->target)+strlen(s->chantypes)+strlen(s->own_nick)+6u;
}
struct gin_account_history_bootstrap_result gin_account_history_bootstrap(
 const struct gin_account_history_bootstrap_input *in)
{
 struct boot_owner o;
 size_t nc,wc,i,j;unsigned wait=0;
 struct gin_http_control control={boot_tick,&o,0};
 memset(&o,0,sizeof(o));o.in=in;o.inflight=SIZE_MAX;o.result.code=GIN_HISTORY_INVALID;o.result.incomplete=1;
 if(!in || !in->receiver || !in->dispatcher || !in->output || !in->plan || !in->topology ||
    !in->http || !in->policy || !in->credential || !in->chantypes) return o.result;
 if(!gin_http_origin_valid(in->http->origin) ||
    gin_credential_validate(in->credential)!=GIN_CREDENTIAL_OK) return o.result;
 if(in->encryption!=GIN_RENDER_MODE_PLAIN) {o.result.code=GIN_HISTORY_UNSUPPORTED;return o.result;}
 o.epoch=gin_account_receive_epoch(in->receiver);
 o.result.code=boot_check(&o);if(o.result.code!=GIN_HISTORY_OK) return o.result;
 if(o.now>UINT64_MAX-GIN_HISTORY_ATTEMPT_MS_MAX) {o.result.code=GIN_HISTORY_INVALID;return o.result;}
 o.deadline=o.now+GIN_HISTORY_ATTEMPT_MS_MAX;
 nc=gin_account_plan_network_count(in->plan);wc=gin_account_plan_window_count(in->plan);
 if(nc>BOOT_DEST || wc>BOOT_DEST-nc) {o.result.code=GIN_HISTORY_LIMIT;goto done;}
 o.result.destinations=nc+wc;
 for(i=0;i<o.result.destinations;i++) {
  struct boot_destination *d=&o.destinations[i];
  const struct gin_topology_network *net;
  const struct gin_topology_window *w=NULL;
  struct gin_checkpoint_progress progress;
  d->destination.window_index=SIZE_MAX;
  if(i<nc) d->destination.network_index=i;
  else {
   w=gin_account_plan_window_at(in->plan,i-nc);if(!w) {o.result.code=GIN_HISTORY_ROUTE;goto done;}
   for(j=0;j<nc;j++) if(gin_account_plan_network_at(in->plan,j)->id==w->network_id) break;
   if(j==nc) {o.result.code=GIN_HISTORY_ROUTE;goto done;}
   d->destination.network_index=j;d->destination.window_index=i-nc;
  }
  net=gin_account_plan_network_at(in->plan,d->destination.network_index);
  if(!net) {o.result.code=GIN_HISTORY_ROUTE;goto done;}
  d->scope.origin=in->credential->origin;d->scope.subject_id=in->credential->subject_id;
  d->scope.network_slug=net->slug;d->scope.network_id=net->id;d->scope.own_nick=net->own_nick;
  d->scope.target=w?w->name.original_wire_target:"$server";d->scope.chantypes=in->chantypes;
  d->scope.socket_epoch=o.epoch;d->scope.encryption=in->encryption;
  d->scope.role=w?(w->role==GIN_REMOTE_CHANNEL?GIN_RENDER_CHANNEL:GIN_RENDER_QUERY):GIN_RENDER_STATUS;
  d->destination.role=d->scope.role;
  o.result.code=gin_history_scope_check(&d->scope);if(o.result.code!=GIN_HISTORY_OK) goto done;
  if(gin_account_output_progress(in->output,&d->destination,&progress) || progress.anchor<0) {o.result.code=GIN_HISTORY_ROUTE;goto done;}
  d->anchor=progress.anchor;d->base=boot_base(&d->scope);
  o.result.code=boot_reserve(&o,i,d->base,0,0);if(o.result.code!=GIN_HISTORY_OK) goto done;
  o.result.code=gin_history_recovery_new(&d->scope,d->anchor,GIN_HISTORY_PAGE_DEFAULT,o.now,&d->recovery);
  if(o.result.code!=GIN_HISTORY_OK) goto done;
 }
 while(o.result.drained<o.result.destinations) {
  for(i=0;i<o.result.destinations;i++) {
   struct boot_destination *d=&o.destinations[i];
   struct gin_history_request request;
   struct gin_history_page *page=NULL;
   struct gin_history_fetch_result fetched;
   struct gin_history_status status;
   if(d->done) continue;
   if(boot_tick(&o,&wait)) goto done;
   if(o.result.pages>=GIN_HISTORY_PAGES_MAX*o.result.destinations) {o.result.code=GIN_HISTORY_LIMIT;goto done;}
   o.result.code=gin_history_recovery_next(d->recovery,o.now,&request);
   if(o.result.code!=GIN_HISTORY_OK) goto done;
   o.inflight=i;
   o.result.code=boot_reserve(&o,SIZE_MAX,0,0,0);if(o.result.code!=GIN_HISTORY_OK) goto done;
   fetched=gin_history_fetch_controlled(in->http,in->policy,in->credential,in->topology,&d->scope,&request,&page,&control);
   if(!control.aborted) {
    o.result.code=boot_check(&o);
    /* Also observe owner stop and queued invalidation before accepting body. */
    if(o.result.code==GIN_HISTORY_OK && !boot_tick(&o,&wait)) o.result.code=fetched.code;
   }
   if(o.result.code==GIN_HISTORY_OK) {
    o.result.code=boot_reserve(&o,i,gin_history_page_owned_bytes(page),gin_history_page_count(page),1);
    if(o.result.code==GIN_HISTORY_OK) o.result.code=gin_history_recovery_page(d->recovery,page,o.now);
   }
   gin_history_page_free(page);o.inflight=SIZE_MAX;
   if(o.result.code!=GIN_HISTORY_OK) goto done;
   o.result.pages++;
   status=gin_history_recovery_status(d->recovery);
   if(status.state==GIN_HISTORY_INCOMPLETE_LIMIT) {o.result.code=GIN_HISTORY_LIMIT;goto done;}
   if(status.state==GIN_HISTORY_VISIBLE_DRAINED) {
    const struct gin_history_page *view=gin_history_recovery_view(d->recovery);
    for(j=0;j<gin_history_page_count(view);j++) {
     struct gin_account_history_output_result written;
     o.result.code=boot_boundary(&o,&wait);if(o.result.code!=GIN_HISTORY_OK) goto done;
     written=gin_account_output_append_history(in->output,&d->destination,view,j,d->anchor);
     if(written.history==GIN_HISTORY_IGNORE) continue;
     o.result.cleanup_failed|=written.output.cleanup_failed;
     o.result.code=written.history;
     if(o.result.code==GIN_HISTORY_OK && written.output.code!=GIN_CHECKPOINT_OK) o.result.code=GIN_HISTORY_REMOTE;
     if(o.result.code!=GIN_HISTORY_OK) goto done;
     o.result.appended++;
    }
    d->done=1;o.result.drained++;
   }
  }
 }
 o.result.code=boot_boundary(&o,&wait);
 done:
 o.result.incomplete=o.result.code!=GIN_HISTORY_OK;
 o.result.cleanup_failed|=gin_account_receive_cleanup_failed(in->receiver);
 o.result.quarantined=gin_account_receive_quarantined(in->receiver);
 for(i=0;i<o.result.destinations;i++) gin_history_recovery_free(o.destinations[i].recovery);
 for(i=0;i<o.late_count;i++) gin_history_page_free(o.late[i]);
 return o.result;
}
