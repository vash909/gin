#include "account_output.h"
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
static enum gin_history_code map_render(enum gin_render_code c){switch(c){case GIN_RENDER_INVALID:return GIN_HISTORY_INVALID;case GIN_RENDER_MALFORMED:return GIN_HISTORY_MALFORMED;case GIN_RENDER_LIMIT:return GIN_HISTORY_LIMIT;case GIN_RENDER_NOMEM:return GIN_HISTORY_NOMEM;case GIN_RENDER_RANGE:return GIN_HISTORY_INVALID;case GIN_RENDER_ROUTE_MISMATCH:return GIN_HISTORY_ROUTE;case GIN_RENDER_UNSUPPORTED_ENCRYPTION:return GIN_HISTORY_UNSUPPORTED;default:return GIN_HISTORY_MALFORMED;}}
struct gin_account_history_output_result gin_account_output_append_history(struct gin_account_output *h,const struct gin_account_destination *d,const struct gin_history_page *page,size_t index,int64_t anchor){struct gin_account_history_output_result res;struct entry *e;const struct gin_history_scope *sc;enum gin_render_code rc;struct gin_render_record *rec=NULL;res.history=GIN_HISTORY_INVALID;res.output=result();if(!h||!d||!page||anchor<0) return res;if(index>=gin_history_page_count(page)) return res;e=entry_for(h,d);if(!e) {res.history=GIN_HISTORY_ROUTE;return res;}
if(d->terminal!=0){res.history=GIN_HISTORY_ROUTE;return res;}
if(gin_account_receive_epoch(h->receiver)!=h->epoch){res.history=GIN_HISTORY_ROUTE;return res;}
if(gin_history_page_source(page)!=GIN_HISTORY_REST&&gin_history_page_source(page)!=GIN_HISTORY_MERGED){res.history=GIN_HISTORY_ROUTE;return res;}
sc=gin_history_page_scope(page);if(!scope_match(e,sc)){res.history=GIN_HISTORY_ROUTE;return res;}
rc=gin_history_render(page,index,&rec);if(rc==GIN_RENDER_IGNORE){res.history=GIN_HISTORY_IGNORE;return res;}
if(rc!=GIN_RENDER_OK){res.history=map_render(rc);if(rec) gin_render_record_free(rec);return res;}
res.history=GIN_HISTORY_OK;res.output=append_record(h,d,rec,anchor);gin_render_record_free(rec);return res;}
