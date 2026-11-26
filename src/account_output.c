#include "account_output_hot.h"
#include "account_sinks_hot.h"
#include "account_naming.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define LIMIT GIN_CONNECT_TOPICS_MAX
struct entry {
 struct gin_checkpoint *checkpoint;
 struct gin_checkpoint_progress progress;
 struct gin_history_scope scope;
 struct stat expected;
 int fd,failed;
 size_t network,window;
};
struct gin_account_output {
 const struct gin_root *root;
 const struct gin_account_sinks *sinks;
 const struct gin_account_plan *plan;
 const struct gin_account_receive *receiver;
 const char *types;
 uint64_t epoch;
 const struct gin_account_registry *registry;
 int hot_failed,hot_cleanup;
 size_t count;
 struct entry entries[LIMIT];
};
static int same(const struct stat *a,const struct stat *b)
{ return a->st_dev==b->st_dev && a->st_ino==b->st_ino; }
static int private_file(const struct stat *s)
{ return S_ISREG(s->st_mode) && s->st_uid==geteuid() && (s->st_mode&07777)==0600 && s->st_nlink==1; }
/* Validate existing binding only: never create/adopt an unbound root. */
static int binding(const struct gin_root *root,const struct gin_account_identity *id,const char *types)
{
 unsigned char expected[GIN_ACCOUNT_ROOT_MAX],actual[GIN_ACCOUNT_ROOT_MAX+1];
 const char *fields[3]; struct stat before,opened,after; size_t n=8,i,used=0;
 int fd,ok=0; unsigned attempts=0;
 memcpy(expected,"GINACC\0\1",8); fields[0]=id->origin;fields[1]=id->subject_id;fields[2]=types;
 for(i=0;i<3;i++) { size_t len=strlen(fields[i]); if(len>65535 || len+2>sizeof(expected)-n) return 0; expected[n++]=(unsigned char)(len>>8);expected[n++]=(unsigned char)len;memcpy(expected+n,fields[i],len);n+=len; }
 if(fstatat(root->rootfd,GIN_ACCOUNT_ROOT_FILENAME,&before,AT_SYMLINK_NOFOLLOW) || !private_file(&before)) return 0;
 fd=openat(root->rootfd,GIN_ACCOUNT_ROOT_FILENAME,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);if(fd<0) return 0;
 if(fstat(fd,&opened) || !private_file(&opened) || !same(&before,&opened)) goto done;
 while(used<sizeof(actual) && attempts++<GIN_ACCOUNT_ROOT_MAX+128u) { ssize_t r=read(fd,actual+used,sizeof(actual)-used);if(r<0 && errno==EINTR) continue;if(r<0) goto done;if(!r) break;used+=(size_t)r; }
 if(fstatat(root->rootfd,GIN_ACCOUNT_ROOT_FILENAME,&after,AT_SYMLINK_NOFOLLOW) || !private_file(&after) || !same(&opened,&after) || fstat(fd,&opened) || !private_file(&opened) || opened.st_size!=(off_t)used) goto done;
 ok=used==n && !memcmp(expected,actual,n);
done:
 if(close(fd)) ok=0;
 return ok;
}
int gin_account_output_close(struct gin_account_output **out)
{
 size_t i; int failed=0;struct gin_account_output *h;
 if(!out) return -1;
 h=*out;if(!h) return 0;*out=NULL;
 for(i=0;i<h->count;i++) if(h->entries[i].checkpoint && gin_checkpoint_close(&h->entries[i].checkpoint)) failed=-1;
 free(h);return failed;
}
static struct gin_account_output_result result(void)
{ struct gin_account_output_result r={GIN_CHECKPOINT_INVALID,0,{GIN_CHECKPOINT_INVALID,0,0},0};return r; }
struct gin_account_output_result gin_account_output_new(const struct gin_root *root,
 const struct gin_account_sinks *sinks,const struct gin_account_plan *plan,
 const struct gin_account_receive *receiver,const char *types,enum gin_render_encryption mode,
 struct gin_account_output **out)
{
 struct gin_account_output_result r=result();struct gin_account_output *h;
 const struct gin_account_identity *id=gin_account_receive_identity(receiver);
 size_t nc,wc,i;
 if(!out || *out || !root || !sinks || !plan || !id || !types || mode!=GIN_RENDER_MODE_PLAIN || !gin_account_receive_epoch(receiver)) return r;
 nc=gin_account_plan_network_count(plan);wc=gin_account_plan_window_count(plan);
 if(nc+wc>LIMIT || nc!=gin_account_sinks_network_count(sinks) || wc!=gin_account_sinks_window_count(sinks) || !binding(root,id,types)) return r;
 h=calloc(1,sizeof(*h));if(!h) { r.code=GIN_CHECKPOINT_NOMEM;return r; }
 h->root=root;h->sinks=sinks;h->plan=plan;h->receiver=receiver;h->types=types;h->epoch=gin_account_receive_epoch(receiver);
 for(i=0;i<nc+wc;i++) {
  struct entry *e=&h->entries[i]; const struct gin_topology_network *net;const struct gin_topology_window *w=NULL;size_t j;
  h->count=i+1;e->window=SIZE_MAX;
  if(i<nc) { e->network=i;e->fd=gin_account_sinks_network_output(sinks,i); }
  else { w=gin_account_plan_window_at(plan,i-nc);if(!w) goto fail;for(j=0;j<nc;j++) if(gin_account_plan_network_at(plan,j)->id==w->network_id) break;if(j==nc) goto fail;e->network=j;e->window=i-nc;e->fd=gin_account_sinks_window_output(sinks,i-nc); }
  net=gin_account_plan_network_at(plan,e->network);if(!net) goto fail;
  e->scope.origin=id->origin;e->scope.subject_id=id->subject_id;e->scope.network_slug=net->slug;e->scope.network_id=net->id;e->scope.own_nick=net->own_nick;e->scope.chantypes=types;e->scope.socket_epoch=h->epoch;e->scope.encryption=mode;
  e->scope.role=w?(w->role==GIN_REMOTE_CHANNEL?GIN_RENDER_CHANNEL:GIN_RENDER_QUERY):GIN_RENDER_STATUS;
  e->scope.target=w?w->name.original_wire_target:"$server";
  r.code=w?gin_checkpoint_open_named(root,&e->scope,e->fd,&e->checkpoint,&e->progress,gin_account_name_prepare):gin_checkpoint_open_network(root,&e->scope,e->fd,&e->checkpoint,&e->progress);
  if(r.code!=GIN_CHECKPOINT_OK && r.code!=GIN_CHECKPOINT_UNKNOWN) goto fail;
  if(fstat(e->fd,&e->expected)) { r.code=GIN_CHECKPOINT_IO;goto fail; }
 }
 r.code=GIN_CHECKPOINT_OK;*out=h;return r;
fail:
 if(r.code==GIN_CHECKPOINT_OK || r.code==GIN_CHECKPOINT_UNKNOWN) r.code=GIN_CHECKPOINT_INVALID;
 r.cleanup_failed=gin_account_output_close(&h)!=0;return r;
}
static struct entry *entry_for(const struct gin_account_output *h,const struct gin_account_destination *d)
{
 size_t i;if(!h || !d || (d->terminal!=0 && d->terminal!=1)) return NULL;
 for(i=0;i<h->count;i++) { struct entry *e=(struct entry *)&h->entries[i];if(e->network==d->network_index && e->window==d->window_index && e->scope.role==d->role) return e; }
 return NULL;
}
int gin_account_output_progress(const struct gin_account_output *h,const struct gin_account_destination *d,struct gin_checkpoint_progress *p)
{ struct entry *e=entry_for(h,d);if(!e || !p || e->failed) return -1;*p=e->progress;return 0; }
static struct gin_account_output_result append_record(struct gin_account_output *h,
 const struct gin_account_destination *d,const struct gin_render_record *record,int64_t anchor)
{
 struct gin_account_output_result r=result();struct entry *e=entry_for(h,d);struct stat actual,path;int fd;
 if(!e || !record || e->failed || anchor<0 || anchor<e->progress.anchor || gin_account_receive_epoch(h->receiver)!=h->epoch) return r;
 /* Reuse authoritative existing-only before/open/after path guards. No lock open. */
 fd=e->window==SIZE_MAX?gin_root_network_output_open(h->root,e->scope.network_slug):gin_root_output_open_named(h->root,e->scope.network_slug,e->scope.target,h->types,gin_account_name_prepare);
 r.code=GIN_CHECKPOINT_UNSAFE;
 if(fd<0) goto stop;
 if(fstat(fd,&path) || fstat(e->fd,&actual) || !same(&path,&actual) || !same(&actual,&e->expected) || actual.st_size!=e->expected.st_size) { if(close(fd)) r.cleanup_failed=1;goto stop; }
 if(close(fd)) { r.cleanup_failed=1;goto stop; }
 r.code=GIN_CHECKPOINT_IO;
 if(gin_render_append(e->fd,record,&r.appended)!=GIN_RENDER_OK) goto stop;
 r.checkpoint=gin_checkpoint_commit(e->checkpoint,anchor);r.code=r.checkpoint.code;
 if(r.code!=GIN_CHECKPOINT_OK) goto stop;
 e->progress.anchor=anchor;
 if(fstat(e->fd,&e->expected)) { r.code=GIN_CHECKPOINT_IO;goto stop; }
 if(d->terminal) e->failed=1;
 return r;
stop:
 e->failed=1;return r;
}
struct gin_account_output_result gin_account_output_append(struct gin_account_output *h,const struct gin_account_destination *d,const struct gin_render_record *record,int64_t anchor){return append_record(h,d,record,anchor);}
static int same_str(const char *a,const char *b)
{ return a && b && !strcmp(a,b); }
static int scope_match(const struct entry *e,const struct gin_history_scope *s){if(!e||!s) return 0;if(e->scope.network_id!=s->network_id) return 0;if(e->scope.socket_epoch!=s->socket_epoch) return 0;if(e->scope.role!=s->role) return 0;if(e->scope.encryption!=GIN_RENDER_MODE_PLAIN||s->encryption!=GIN_RENDER_MODE_PLAIN) return 0;if(!same_str(e->scope.origin,s->origin)) return 0;if(!same_str(e->scope.subject_id,s->subject_id)) return 0;if(!same_str(e->scope.network_slug,s->network_slug)) return 0;if(!same_str(e->scope.target,s->target)) return 0;if(!same_str(e->scope.chantypes,s->chantypes)) return 0;if(!same_str(e->scope.own_nick,s->own_nick)) return 0;return 1;}
static struct gin_account_output_result hot_authority(struct gin_account_output *h,
 const struct gin_account_registry *reg,const struct gin_account_registry_scope *s,uint64_t gen)
{
 struct gin_account_output_result r=result();
 struct gin_account_sinks_hot_result status;
 const struct gin_account_identity *id;
 size_t i;
 if(!h || !reg || !s) return r;
 if(h->hot_failed) { r.code=GIN_CHECKPOINT_UNSAFE;r.cleanup_failed=h->hot_cleanup;return r; }
 for(i=0;i<h->count;i++) if(h->entries[i].failed) { r.code=GIN_CHECKPOINT_UNSAFE;return r; }
 id=gin_account_receive_identity(h->receiver);
 if(!id || !same_str(id->origin,s->origin) || !same_str(id->subject_id,s->subject_uuid) ||
    !same_str(id->subject_name,s->topic_subject) || !same_str(h->types,s->chantypes) ||
    !s->epoch || s->epoch!=h->epoch || gin_account_receive_epoch(h->receiver)!=h->epoch) {
  r.code=GIN_CHECKPOINT_SCOPE;return r;
 }
 status=gin_account_sinks_hot_status(h->sinks,h->root,h->plan,reg,s,gen);
 if(status.code!=GIN_ACCOUNT_SINKS_HOT_OK || status.cleanup_failed || status.error) {
  if(status.code==GIN_ACCOUNT_SINKS_HOT_FAILED || status.cleanup_failed || status.error) {
   h->hot_failed=1;h->hot_cleanup=status.cleanup_failed;
   r.code=GIN_CHECKPOINT_UNSAFE;r.cleanup_failed=status.cleanup_failed;
  } else if(status.code==GIN_ACCOUNT_SINKS_HOT_SCOPE) r.code=GIN_CHECKPOINT_SCOPE;
  return r;
 }
 if(status.window_count!=gin_account_sinks_window_count(h->sinks) ||
    status.window_count>gin_account_registry_window_count(reg)) return r;
 r.code=GIN_CHECKPOINT_OK;return r;
}
struct gin_account_output_result gin_account_output_hot_bind(struct gin_account_output *h,
 const struct gin_account_registry *reg,const struct gin_account_registry_scope *s,uint64_t gen)
{
 struct gin_account_output_result r=result();size_t nc,wc;
 if(!h || h->registry || gen!=0) return r;
 r=hot_authority(h,reg,s,gen);if(r.code!=GIN_CHECKPOINT_OK) return r;
 nc=gin_account_plan_network_count(h->plan);wc=gin_account_plan_window_count(h->plan);
 if(nc!=gin_account_registry_network_count(reg) || wc!=gin_account_registry_window_count(reg) ||
    nc+wc!=h->count || wc!=gin_account_sinks_window_count(h->sinks)) return result();
 /* Status attests the same sinks' exact cold root/plan/registry binding; its
  * generation-zero constructor validated the network/window/source layout. */
 h->registry=reg;return r;
}
struct gin_account_output_result gin_account_output_hot_append(struct gin_account_output *h,
 const struct gin_account_registry_scope *s,uint64_t gen,size_t index)
{
 struct gin_account_output_result r=result();struct entry staged;
 const struct gin_topology_window *w;const struct gin_topology_network *net;
 const struct gin_account_registry_scope *rs;
 struct gin_account_root_check_result check;size_t nc,wc,i;
 if(!h || !h->registry) return r;
 r=hot_authority(h,h->registry,s,gen);if(r.code!=GIN_CHECKPOINT_OK) return r;
 nc=gin_account_registry_network_count(h->registry);
 if(h->count<nc) return result();
 wc=h->count-nc;
 if(index>wc || index>=gin_account_registry_window_count(h->registry) ||
    index>=gin_account_sinks_window_count(h->sinks)) return result();
 if(index<wc) return r;
 if(h->count==LIMIT) return result();
 w=gin_account_registry_window_at(h->registry,index);
 if(!w || w->role!=GIN_REMOTE_QUERY || w->joined || w->source!=GIN_CHANNEL_NONE || !w->opened_at) return result();
 for(i=0;i<nc;i++) {
  net=gin_account_registry_network_at(h->registry,i);
  if(net && net->id==w->network_id && same_str(net->slug,w->name.input_network_slug)) break;
 }
 if(i==nc) return result();
 rs=gin_account_registry_scope(h->registry);net=gin_account_registry_network_at(h->registry,i);
 memset(&staged,0,sizeof(staged));staged.network=i;staged.window=index;
 staged.fd=gin_account_sinks_window_output(h->sinks,index);
 staged.scope.origin=rs->origin;staged.scope.subject_id=rs->subject_uuid;
 staged.scope.network_slug=net->slug;staged.scope.network_id=net->id;
 staged.scope.target=w->name.original_wire_target;staged.scope.own_nick=net->own_nick;
 staged.scope.chantypes=rs->chantypes;staged.scope.socket_epoch=h->epoch;
 staged.scope.role=GIN_RENDER_QUERY;staged.scope.encryption=GIN_RENDER_MODE_PLAIN;
 check=gin_account_root_check(h->root,rs->origin,rs->subject_uuid,rs->chantypes);
 if(check.code!=GIN_ACCOUNT_ROOT_OK || check.cleanup_failed) {
  r.code=GIN_CHECKPOINT_UNSAFE;r.cleanup_failed=check.cleanup_failed;goto fail;
 }
 r.code=gin_checkpoint_open_named(h->root,&staged.scope,staged.fd,
     &staged.checkpoint,&staged.progress,gin_account_name_prepare);
 if(r.code!=GIN_CHECKPOINT_OK && r.code!=GIN_CHECKPOINT_UNKNOWN) goto fail;
 if(fstat(staged.fd,&staged.expected)) { r.code=GIN_CHECKPOINT_IO;goto fail; }
 h->entries[h->count]=staged;++h->count;r.code=GIN_CHECKPOINT_OK;return r;
fail:
 if(staged.checkpoint && gin_checkpoint_close(&staged.checkpoint)) r.cleanup_failed=1;
 h->hot_failed=1;h->hot_cleanup=r.cleanup_failed;return r;
}
static enum gin_history_code map_render(enum gin_render_code c){switch(c){case GIN_RENDER_INVALID:return GIN_HISTORY_INVALID;case GIN_RENDER_MALFORMED:return GIN_HISTORY_MALFORMED;case GIN_RENDER_LIMIT:return GIN_HISTORY_LIMIT;case GIN_RENDER_NOMEM:return GIN_HISTORY_NOMEM;case GIN_RENDER_RANGE:return GIN_HISTORY_INVALID;case GIN_RENDER_ROUTE_MISMATCH:return GIN_HISTORY_ROUTE;case GIN_RENDER_UNSUPPORTED_ENCRYPTION:return GIN_HISTORY_UNSUPPORTED;default:return GIN_HISTORY_MALFORMED;}}
struct gin_account_history_output_result gin_account_output_append_history(struct gin_account_output *h,const struct gin_account_destination *d,const struct gin_history_page *page,size_t index,int64_t anchor){struct gin_account_history_output_result res;struct entry *e;const struct gin_history_scope *sc;enum gin_render_code rc;struct gin_render_record *rec=NULL;res.history=GIN_HISTORY_INVALID;res.output=result();if(!h||!d||!page||anchor<0) return res;if(index>=gin_history_page_count(page)) return res;e=entry_for(h,d);if(!e) {res.history=GIN_HISTORY_ROUTE;return res;}
if(d->terminal!=0){res.history=GIN_HISTORY_ROUTE;return res;}
if(gin_account_receive_epoch(h->receiver)!=h->epoch){res.history=GIN_HISTORY_ROUTE;return res;}
if(gin_history_page_source(page)!=GIN_HISTORY_REST&&gin_history_page_source(page)!=GIN_HISTORY_MERGED){res.history=GIN_HISTORY_ROUTE;return res;}
sc=gin_history_page_scope(page);if(!scope_match(e,sc)){res.history=GIN_HISTORY_ROUTE;return res;}
rc=gin_history_render(page,index,&rec);if(rc==GIN_RENDER_IGNORE){res.history=GIN_HISTORY_IGNORE;return res;}
if(rc!=GIN_RENDER_OK){res.history=map_render(rc);if(rec) gin_render_record_free(rec);return res;}
res.history=GIN_HISTORY_OK;res.output=append_record(h,d,rec,anchor);gin_render_record_free(rec);return res;}

struct gin_account_output_hot_result gin_account_output_hot_status(
 const struct gin_account_output *h,const struct gin_root *root,
 const struct gin_account_sinks *sinks,const struct gin_account_plan *plan,
 const struct gin_account_receive *receiver,const struct gin_account_registry *reg,
 const struct gin_account_registry_scope *scope,uint64_t generation,size_t index)
{
 struct gin_account_output_hot_result r={GIN_ACCOUNT_OUTPUT_HOT_INVALID,0,0,0,0};
 struct gin_account_sinks_hot_result status;
 const struct gin_account_identity *identity;
 const struct gin_account_registry_scope *bound;
 const struct gin_topology_network *network;
 const struct gin_topology_window *window;
 const struct entry *selected;
 struct gin_history_scope expected;
 size_t networks=0,windows,i;
 int failed=0,saved_errno=errno;
 if(!h) goto done;
 if(h->registry) r.generation=gin_account_registry_generation(h->registry);
 if(h->plan) networks=gin_account_plan_network_count(h->plan);
 if(h->count<=LIMIT && h->count>=networks) r.window_count=h->count-networks;
 r.cleanup_failed=h->hot_cleanup!=0;
 failed=h->hot_failed || h->hot_cleanup;
 if(h->count<=LIMIT)
  for(i=0;i<h->count;i++) if(h->entries[i].failed) failed=1;
 /* Observe only the actual borrowed sink's already-stored failure state. */
 bound=h->registry?gin_account_registry_scope(h->registry):NULL;
 status=gin_account_sinks_hot_status(h->sinks,h->root,h->plan,h->registry,
                                    bound,r.generation);
 if(status.code==GIN_ACCOUNT_SINKS_HOT_FAILED || status.error || status.cleanup_failed) {
  failed=1;
  if(status.cleanup_failed) r.cleanup_failed=1;
 }
 if(failed) { r.code=GIN_ACCOUNT_OUTPUT_HOT_FAILED;goto done; }
 if(!root || !sinks || !plan || !receiver || !reg || !scope || !h->registry)
  goto done;
 if(h->root!=root || h->sinks!=sinks || h->plan!=plan ||
    h->receiver!=receiver || h->registry!=reg) {
  r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;goto done;
 }
 if(h->count>LIMIT || networks>h->count ||
    networks!=gin_account_registry_network_count(reg) ||
    networks!=gin_account_sinks_network_count(sinks)) goto done;
 windows=gin_account_registry_window_count(reg);
 if(r.window_count>gin_account_sinks_window_count(sinks) ||
    gin_account_sinks_window_count(sinks)>windows) goto done;
 status=gin_account_sinks_hot_status(sinks,root,plan,reg,scope,generation);
 if(status.code!=GIN_ACCOUNT_SINKS_HOT_OK) {
  if(status.code==GIN_ACCOUNT_SINKS_HOT_SCOPE) r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;
  else if(status.code==GIN_ACCOUNT_SINKS_HOT_STALE) r.code=GIN_ACCOUNT_OUTPUT_HOT_STALE;
  else if(status.code==GIN_ACCOUNT_SINKS_HOT_FAILED) r.code=GIN_ACCOUNT_OUTPUT_HOT_FAILED;
  goto done;
 }
 identity=gin_account_receive_identity(receiver);
 if(!identity || !same_str(identity->origin,scope->origin) ||
    !same_str(identity->subject_id,scope->subject_uuid) ||
    !same_str(identity->subject_name,scope->topic_subject) ||
    !same_str(h->types,scope->chantypes) || !scope->epoch ||
    scope->epoch!=h->epoch || gin_account_receive_epoch(receiver)!=h->epoch) {
  r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;goto done;
 }
 if(generation!=r.generation) { r.code=GIN_ACCOUNT_OUTPUT_HOT_STALE;goto done; }
 if(index!=SIZE_MAX && index>=windows) goto done;
 r.code=GIN_ACCOUNT_OUTPUT_HOT_OK;
 if(index==SIZE_MAX || index>=r.window_count) goto done;
 selected=&h->entries[networks+index];
 window=gin_account_registry_window_at(reg,index);
 if(!window || selected->window!=index || selected->network>=networks ||
    !selected->checkpoint || selected->fd<0 ||
    selected->fd!=gin_account_sinks_window_output(sinks,index) ||
    (window->role!=GIN_REMOTE_QUERY && window->role!=GIN_REMOTE_CHANNEL)) {
  r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;goto done;
 }
 network=gin_account_registry_network_at(reg,selected->network);
 if(!network || network->id!=window->network_id ||
    !same_str(network->slug,window->name.input_network_slug)) {
  r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;goto done;
 }
 memset(&expected,0,sizeof(expected));
 expected.origin=scope->origin;expected.subject_id=scope->subject_uuid;
 expected.network_slug=network->slug;expected.network_id=network->id;
 expected.target=window->name.original_wire_target;expected.own_nick=network->own_nick;
 expected.chantypes=scope->chantypes;expected.socket_epoch=h->epoch;
 expected.role=window->role==GIN_REMOTE_QUERY?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
 expected.encryption=GIN_RENDER_MODE_PLAIN;
 if(!scope_match(selected,&expected)) { r.code=GIN_ACCOUNT_OUTPUT_HOT_SCOPE;goto done; }
 r.window_admitted=1;
 done:
 errno=saved_errno;
 return r;
}
