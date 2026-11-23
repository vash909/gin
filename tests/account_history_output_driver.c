#define _XOPEN_SOURCE 700
#include "account_output.h"
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void lease(const struct gin_root *r) { pid_t p=fork(); int s; if(!p) { struct flock f={0}; f.l_type=F_WRLCK; f.l_whence=SEEK_SET; _exit(fcntl(r->lockfd,F_SETLK,&f)==-1 && (errno==EACCES || errno==EAGAIN)?0:1); } if(p<0 || waitpid(p,&s,0)!=p || s) exit(1); }
/* Exercise the real opt-in API; no compatibility stubs or injected records. */
static struct gin_history_page *decode(const struct gin_history_scope *s,const char *kind)
{
 char json[1024]; struct gin_history_page *p=NULL;
 struct gin_history_request req={GIN_HISTORY_AFTER,0,50};
 int n=snprintf(json,sizeof(json),"[{\"id\":99,\"server_time\":123,\"network\":\"%s\",\"channel\":\"%s\",\"kind\":\"%s\",\"sender\":\"sender\",\"body\":\"hello\"%s}]",s->network_slug,s->target,kind,s->role==GIN_RENDER_QUERY?",\"dm_with\":\"Peer\"":"");
 R(n>0 && (size_t)n<sizeof(json));
 R(gin_history_page_decode(s,&req,(const unsigned char *)json,(size_t)n,&p)==GIN_HISTORY_OK && p);
 R(gin_history_page_source(p)==GIN_HISTORY_REST); return p;
}
static struct gin_history_page *decode_id(const struct gin_history_scope *s,const char *kind,const char *sender,const char *body,int64_t id,int64_t server_time)
{
 char json[1024]; struct gin_history_page *p=NULL;
 struct gin_history_request req={GIN_HISTORY_AFTER,0,50};
 int n=snprintf(json,sizeof(json),"[{\"id\":%lld,\"server_time\":%lld,\"network\":\"%s\",\"channel\":\"%s\",\"kind\":\"%s\",\"sender\":\"%s\",\"body\":\"%s\"%s}]",(long long)id,(long long)server_time,s->network_slug,s->target,kind,sender,body,s->role==GIN_RENDER_QUERY?",\"dm_with\":\"Peer\"":"");
 R(n>0 && (size_t)n<sizeof(json));
 R(gin_history_page_decode(s,&req,(const unsigned char *)json,(size_t)n,&p)==GIN_HISTORY_OK && p);
 R(gin_history_page_source(p)==GIN_HISTORY_REST); return p;
}
/* Snapshot the actual held sink plus its checkpoint: rejection must not mutate
 * bytes, inode, size or caller-visible progress, including missing metadata. */
struct snapshot { struct stat out,cp; unsigned char bytes[4096],meta[GIN_CHECKPOINT_BYTES_MAX]; ssize_t n,m; int present; struct gin_checkpoint_progress progress; };
static void snap(struct gin_account_output *o,const struct gin_account_destination *d,int fd,int dir,struct snapshot *s)
{
 int f; R(fstat(fd,&s->out)==0); s->n=pread(fd,s->bytes,sizeof(s->bytes),0);
 /* sinks are write-only; use independent existing path reader instead. */
 f=openat(dir,"out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC); R(f>=0);
 s->n=read(f,s->bytes,sizeof(s->bytes)); R(s->n>=0 && close(f)==0);
 f=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); s->present=f>=0;s->m=0;
 if(f>=0) { R(fstat(f,&s->cp)==0);s->m=read(f,s->meta,sizeof(s->meta));R(s->m>=0 && close(f)==0); }
 R(gin_account_output_progress(o,d,&s->progress)==0);
}
static void reject(struct gin_account_output *o,const struct gin_account_destination *valid,const struct gin_account_destination *test,const struct gin_history_page *p,size_t index,int64_t anchor,enum gin_history_code code,int fd,int dir)
{
 struct snapshot a,b; struct gin_account_history_output_result r;
 snap(o,valid,fd,dir,&a);r=gin_account_output_append_history(o,test,p,index,anchor);snap(o,valid,fd,dir,&b);
 R(r.history==code && r.output.code==GIN_CHECKPOINT_INVALID && !r.output.appended && !r.output.checkpoint.published && !r.output.cleanup_failed && !r.output.checkpoint.cleanup_failed);
 R(a.out.st_ino==b.out.st_ino && a.out.st_dev==b.out.st_dev && a.out.st_size==b.out.st_size && a.n==b.n && !memcmp(a.bytes,b.bytes,(size_t)a.n));
 R(a.present==b.present && a.m==b.m && !memcmp(a.meta,b.meta,(size_t)a.m));
 if(a.present) R(a.cp.st_ino==b.cp.st_ino && a.cp.st_size==b.cp.st_size);
 R(a.progress.anchor==b.progress.anchor && a.progress.unknown_before==b.progress.unknown_before);
}
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}]}}";
 struct gin_connect_settings settings={0};struct gin_credential c={0};
 struct gin_topology *booted=NULL,*top=NULL;struct gin_account_plan *plan=NULL;
 struct gin_account_receive *receiver=NULL;struct gin_account_dispatch *dispatcher=NULL;
 struct gin_root root=GIN_ROOT_INITIALIZER;struct gin_account_sinks *sinks=NULL;struct gin_account_output *output=NULL;
 struct gin_render_record *record=NULL;struct gin_history_page *live=NULL,*rest=NULL;
 struct gin_account_destination d;unsigned n=0,i;int dir,fd;
 const char *fault=getenv("GIN_HISTORY_OUTPUT_CASE");
 R(argc==4);alarm(15);strcpy(c.origin,argv[1]);strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 R(!fault || !strcmp(fault,"replace") || !strcmp(fault,"partial"));
 if(fault && strcmp(fault,"replace") && strcmp(fault,"partial")) { fprintf(stderr,"unknown GIN_HISTORY_OUTPUT_CASE\n"); exit(1); }
 settings.http.origin=c.origin;settings.http.ca_file=argv[2];settings.http.connect_ms=700;settings.http.total_ms=1500;settings.policy.client_protocol=4;settings.policy.required_server_protocol=4;settings.join_ms=1500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&booted)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(booted,(const unsigned char *)queries,strlen(queries),&top)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(top,"#&","Alice",&plan)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_receive_new(&settings,&c,plan,&receiver)==GIN_CONNECT_OK);
 R(gin_account_dispatch_new(receiver,plan,top,"#&",GIN_RENDER_MODE_PLAIN,&dispatcher)==GIN_SESSION_OK);
 R(gin_root_acquire(argv[3],&root)==0);R(gin_account_sinks_new(&root,&c,plan,"#&",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 R(gin_account_receive_start(receiver)==GIN_CONNECT_OK);
 R(gin_account_output_new(&root,sinks,plan,receiver,"#&",GIN_RENDER_MODE_PLAIN,&output).code==GIN_CHECKPOINT_OK);
 lease(&root);
 for(i=0;i<150;i++) {
  R(gin_account_receive_service(receiver,20,123)==GIN_CONNECT_OK);
  while(gin_account_receive_peek(receiver)) {
   struct gin_history_scope scope,other;struct gin_account_destination bad;unsigned k;
   R(gin_account_dispatch_next(dispatcher,&d,&record,&live)==GIN_SESSION_OK);
   if(!record) { R(!live);continue; }
   R(live && gin_history_page_source(live)==GIN_HISTORY_LIVE);scope=*gin_history_page_scope(live);
   fd=d.window_index==SIZE_MAX?gin_account_sinks_network_output(sinks,d.network_index):gin_account_sinks_window_output(sinks,d.window_index);
   { char path[128];snprintf(path,sizeof(path),"%s%s%s",scope.network_slug,d.window_index==SIZE_MAX?"":"/",d.window_index==SIZE_MAX?"":scope.role==GIN_RENDER_QUERY?"@peer":scope.target);dir=openat(root.rootfd,path,O_RDONLY|O_DIRECTORY|O_CLOEXEC);R(dir>=0); }
   rest=decode(&scope,scope.role==GIN_RENDER_STATUS?(n==0?"server_event":"notice"):"privmsg");
   reject(output,&d,&d,live,0,0,GIN_HISTORY_ROUTE,fd,dir);
   reject(output,&d,&d,NULL,0,0,GIN_HISTORY_INVALID,fd,dir);
   reject(output,&d,&d,rest,1,0,GIN_HISTORY_INVALID,fd,dir);
   reject(output,&d,&d,rest,0,-1,GIN_HISTORY_INVALID,fd,dir);
   bad=d;bad.terminal=1;reject(output,&d,&bad,rest,0,0,GIN_HISTORY_ROUTE,fd,dir);
   bad=d;bad.network_index=999;reject(output,&d,&bad,rest,0,0,GIN_HISTORY_ROUTE,fd,dir);
   for(k=0;k<9;k++) {
    struct gin_history_page *alternate=NULL;other=scope;
    switch(k) {
     case 0:other.origin="https://other.invalid";break;
     case 1:other.subject_id="32345678-1234-1234-1234-123456789abc";break;
     case 2:other.network_slug="alternate";break;
     case 3:other.target=scope.role==GIN_RENDER_STATUS?"$server":scope.role==GIN_RENDER_QUERY?"Other":"#other";break;
     case 4:other.chantypes="#&!";break;
     case 5:other.own_nick="OtherOwn";break;
     case 6:other.network_id=8;break;
     case 7:other.socket_epoch++;break;
     default:other.role=scope.role==GIN_RENDER_STATUS?GIN_RENDER_CHANNEL:GIN_RENDER_STATUS;other.target=other.role==GIN_RENDER_STATUS?"$server":"#other";break;
    }
    if(k==3 && scope.role!=GIN_RENDER_CHANNEL) continue;
    alternate=decode(&other,"notice");reject(output,&d,&d,alternate,0,0,GIN_HISTORY_ROUTE,fd,dir);gin_history_page_free(alternate);
   }
   { struct gin_history_page *ignored=decode(&scope,"future_kind");reject(output,&d,&d,ignored,0,100,GIN_HISTORY_IGNORE,fd,dir);gin_history_page_free(ignored); }
   if(n==2) {
    struct gin_history_recovery *recovery=NULL;const struct gin_history_page *merged;
    R(gin_history_recovery_new(&scope,0,50,100,&recovery)==GIN_HISTORY_OK);
    R(gin_history_recovery_live(recovery,live,100)==GIN_HISTORY_OK);
    R(gin_history_recovery_page(recovery,rest,100)==GIN_HISTORY_OK);
    merged=gin_history_recovery_view(recovery);R(gin_history_page_source(merged)==GIN_HISTORY_MERGED && gin_history_page_count(merged)==1);
    { struct gin_account_history_output_result r=gin_account_output_append_history(output,&d,merged,0,0);R(r.history==GIN_HISTORY_OK && r.output.code==GIN_CHECKPOINT_OK && r.output.checkpoint.published); }
    gin_history_recovery_free(recovery);
   } else {
    struct gin_account_history_output_result r=gin_account_output_append_history(output,&d,rest,0,0);
    R(r.history==GIN_HISTORY_OK && r.output.code==GIN_CHECKPOINT_OK && r.output.checkpoint.published && r.output.appended>0);
   }
   R(close(dir)==0);gin_history_page_free(rest);rest=NULL;gin_history_page_free(live);live=NULL;gin_render_record_free(record);record=NULL;n++;
  }
  if(n==6 && gin_account_receive_drained(receiver)) break;
 }
 R(n==6 && i<150);
 /* NULL handle / NULL destination pre-storage INVALID untouched */
 {
  struct gin_history_scope scope; struct gin_history_page *p=NULL; struct gin_account_destination sd={0,SIZE_MAX,GIN_RENDER_STATUS,0};
  int fddir;
  scope.origin=c.origin; scope.subject_id=c.subject_id; scope.network_slug="one"; scope.target="$server"; scope.chantypes="#&"; scope.own_nick="Own1"; scope.network_id=1; scope.socket_epoch=gin_account_receive_epoch(receiver); scope.role=GIN_RENDER_STATUS; scope.encryption=GIN_RENDER_MODE_PLAIN;
  p=decode_id(&scope,"notice","sender","hello",99,123);
  fd=gin_account_sinks_network_output(sinks,0); fddir=openat(root.rootfd,"one",O_RDONLY|O_DIRECTORY|O_CLOEXEC); R(fddir>=0);
  { struct snapshot a,b; struct gin_account_history_output_result r;
    snap(output,&sd,fd,fddir,&a);
    r=gin_account_output_append_history(NULL,&sd,p,0,0);
    snap(output,&sd,fd,fddir,&b);
    R(r.history==GIN_HISTORY_INVALID && r.output.code==GIN_CHECKPOINT_INVALID && !r.output.appended && !r.output.checkpoint.published);
    R(a.out.st_ino==b.out.st_ino && a.out.st_size==b.out.st_size && a.n==b.n && !memcmp(a.bytes,b.bytes,(size_t)a.n));
    R(a.present==b.present && a.m==b.m && !memcmp(a.meta,b.meta,(size_t)a.m));
    r=gin_account_output_append_history(output,NULL,p,0,0);
    snap(output,&sd,fd,fddir,&b);
    R(r.history==GIN_HISTORY_INVALID && r.output.code==GIN_CHECKPOINT_INVALID && !r.output.appended);
    R(a.out.st_ino==b.out.st_ino && a.out.st_dev==b.out.st_dev && a.out.st_size==b.out.st_size && a.n==b.n && !memcmp(a.bytes,b.bytes,(size_t)a.n));
    R(a.present==b.present && a.m==b.m && !memcmp(a.meta,b.meta,(size_t)a.m));
    R(a.progress.anchor==b.progress.anchor && a.progress.unknown_before==b.progress.unknown_before);
  }
  gin_history_page_free(p); R(close(fddir)==0);
 }
 lease(&root);
 if(!fault) {
  /* Healthy no-fault: establish trusted prefix via wrapper, then stale-epoch after receiver close */
  struct gin_history_scope scope; struct gin_history_page *prefix=NULL; struct gin_account_destination sd={0,SIZE_MAX,GIN_RENDER_STATUS,0}; struct gin_account_history_output_result wr;
  scope.origin=c.origin; scope.subject_id=c.subject_id; scope.network_slug="one"; scope.target="$server"; scope.chantypes="#&"; scope.own_nick="Own1"; scope.network_id=1; scope.socket_epoch=gin_account_receive_epoch(receiver); scope.role=GIN_RENDER_STATUS; scope.encryption=GIN_RENDER_MODE_PLAIN;
  prefix=decode_id(&scope,"notice","trusted","prefix",100,123);
  wr=gin_account_output_append_history(output,&sd,prefix,0,7);
  R(wr.history==GIN_HISTORY_OK && wr.output.code==GIN_CHECKPOINT_OK && wr.output.checkpoint.published);
  gin_history_page_free(prefix);
  lease(&root);
  /* child fcntl held root lease probes on valid borrowed root without reopening lock already done */
  /* actual receiver close at end of healthy case => stale epoch ROUTE unchanged */
  {
   struct snapshot before,after; struct gin_account_history_output_result r;
   struct gin_history_page *fault_page=NULL;
   int fdir; struct gin_history_scope fscope;
   fscope.origin=c.origin; fscope.subject_id=c.subject_id; fscope.network_slug="one"; fscope.target="$server"; fscope.chantypes="#&"; fscope.own_nick="Own1"; fscope.network_id=1; fscope.socket_epoch=gin_account_receive_epoch(receiver); fscope.role=GIN_RENDER_STATUS; fscope.encryption=GIN_RENDER_MODE_PLAIN;
   fault_page=decode_id(&fscope,"notice","fault","boundary",101,123);
   /* must preserve input page while helper uses it: duplicate check via still valid page */
   R(gin_history_page_count(fault_page)==1);
   gin_account_receive_close(receiver); R(gin_account_receive_epoch(receiver)==0);
   lease(&root);
   fd=gin_account_sinks_network_output(sinks,0); fdir=openat(root.rootfd,"one",O_RDONLY|O_DIRECTORY|O_CLOEXEC); R(fdir>=0);
   snap(output,&sd,fd,fdir,&before);
   r=gin_account_output_append_history(output,&sd,fault_page,0,8);
   snap(output,&sd,fd,fdir,&after);
   R(r.history==GIN_HISTORY_ROUTE && r.output.code==GIN_CHECKPOINT_INVALID && !r.output.appended && !r.output.checkpoint.published);
   R(before.out.st_ino==after.out.st_ino && before.out.st_size==after.out.st_size && before.n==after.n && !memcmp(before.bytes,after.bytes,(size_t)before.n));
   R(before.present==after.present && before.m==after.m && !memcmp(before.meta,after.meta,(size_t)before.m));
   R(before.progress.anchor==after.progress.anchor && before.progress.anchor==7);
   gin_history_page_free(fault_page);
   R(close(fdir)==0);
  }
  R(gin_account_output_close(&output)==0); lease(&root);
  R(gin_account_sinks_close(sinks)==0); R(gin_root_release(&root)==0);
  gin_account_dispatch_free(dispatcher); gin_account_receive_free(receiver); gin_account_plan_free(plan); gin_topology_free(top); gin_topology_free(booted); gin_http_global_cleanup();
  puts("PASS history REST MERGED scope no-mutation six-admissions five-destinations");return 0;
 }
 /* Fault paths: establish trusted prefix 7 through new wrapper, then fault row 101 anchor 8 */
 {
  struct gin_history_scope pscope,fscope;
  struct gin_history_page *ppage=NULL,*fpage=NULL;
  struct gin_account_destination sd={0,SIZE_MAX,GIN_RENDER_STATUS,0};
  struct gin_account_history_output_result pr;
  struct gin_checkpoint_progress progress={999,0};
  struct stat old,after,cpold,cpafter;
  unsigned char cpbytes[GIN_CHECKPOINT_BYTES_MAX],now[GIN_CHECKPOINT_BYTES_MAX],logbytes[4096],got[4096];
  ssize_t cpn,ln; int dir2,fd2; size_t prefix_len;
  struct rlimit saved,limited; struct sigaction previous,ignore;
  pscope.origin=c.origin; pscope.subject_id=c.subject_id; pscope.network_slug="one"; pscope.target="$server"; pscope.chantypes="#&"; pscope.own_nick="Own1"; pscope.network_id=1; pscope.socket_epoch=gin_account_receive_epoch(receiver); pscope.role=GIN_RENDER_STATUS; pscope.encryption=GIN_RENDER_MODE_PLAIN;
  ppage=decode_id(&pscope,"notice","trusted","prefix",100,123);
  pr=gin_account_output_append_history(output,&sd,ppage,0,7);
  R(pr.history==GIN_HISTORY_OK && pr.output.code==GIN_CHECKPOINT_OK && pr.output.checkpoint.published);
  gin_history_page_free(ppage);
  R(gin_account_output_progress(output,&sd,&progress)==0 && progress.anchor==7);
  lease(&root);
  fscope.origin=c.origin; fscope.subject_id=c.subject_id; fscope.network_slug="one"; fscope.target="$server"; fscope.chantypes="#&"; fscope.own_nick="Own1"; fscope.network_id=1; fscope.socket_epoch=gin_account_receive_epoch(receiver); fscope.role=GIN_RENDER_STATUS; fscope.encryption=GIN_RENDER_MODE_PLAIN;
  fpage=decode_id(&fscope,"notice","fault","boundary",101,123);
  R(gin_history_page_count(fpage)==1);
  dir2=openat(root.rootfd,"one",O_RDONLY|O_DIRECTORY|O_CLOEXEC); R(dir2>=0);
  fd2=openat(dir2,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_CLOEXEC); R(fd2>=0);
  R(fstat(fd2,&cpold)==0); cpn=read(fd2,cpbytes,sizeof(cpbytes)); R(cpn>0 && close(fd2)==0);
  fd2=gin_account_sinks_network_output(sinks,0); R(fstat(fd2,&old)==0);
  { int readfd=openat(dir2,"out",O_RDONLY|O_CLOEXEC); R(readfd>=0); ln=read(readfd,logbytes,sizeof(logbytes)); R(ln==old.st_size && close(readfd)==0); }
  if(!strcmp(fault,"replace")) {
   struct gin_account_history_output_result wr;
   R(renameat(dir2,"out",dir2,"out.saved")==0);
   { int replacement=openat(dir2,"out",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600); R(replacement>=0 && write(replacement,"replacement\n",12)==12 && close(replacement)==0); }
   wr=gin_account_output_append_history(output,&sd,fpage,0,8);
   R(wr.history==GIN_HISTORY_OK && wr.output.code==GIN_CHECKPOINT_UNSAFE && wr.output.appended==0 && !wr.output.checkpoint.published);
   R(!wr.output.cleanup_failed && !wr.output.checkpoint.cleanup_failed);
   R(fstat(fd2,&after)==0 && after.st_ino==old.st_ino && after.st_size==old.st_size);
   prefix_len=wr.output.appended;
   R(gin_account_output_progress(output,&sd,&progress)==-1 && progress.anchor==7);
   { struct gin_account_history_output_result again=gin_account_output_append_history(output,&sd,fpage,0,8);
     R(again.history==GIN_HISTORY_OK && again.output.code==GIN_CHECKPOINT_INVALID && !again.output.appended && !again.output.checkpoint.published);
   }
   R(fstat(fd2,&after)==0 && after.st_size==old.st_size+(off_t)prefix_len);
   fd2=openat(dir2,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_CLOEXEC); R(fd2>=0);
   R(fstat(fd2,&cpafter)==0 && cpafter.st_ino==cpold.st_ino && cpafter.st_size==cpold.st_size);
   R(read(fd2,now,sizeof(now))==cpn && !memcmp(now,cpbytes,(size_t)cpn) && close(fd2)==0);
  } else {
   struct gin_account_history_output_result wr;
   R(getrlimit(RLIMIT_FSIZE,&saved)==0); limited=saved; limited.rlim_cur=(rlim_t)old.st_size+5;
   memset(&ignore,0,sizeof(ignore)); ignore.sa_handler=SIG_IGN; R(sigemptyset(&ignore.sa_mask)==0);
   R(sigaction(SIGXFSZ,&ignore,&previous)==0 && setrlimit(RLIMIT_FSIZE,&limited)==0);
   wr=gin_account_output_append_history(output,&sd,fpage,0,8);
   R(setrlimit(RLIMIT_FSIZE,&saved)==0 && sigaction(SIGXFSZ,&previous,NULL)==0);
   R(wr.history==GIN_HISTORY_OK && wr.output.code==GIN_CHECKPOINT_IO && wr.output.appended==5 && !wr.output.checkpoint.published);
   R(!wr.output.cleanup_failed && !wr.output.checkpoint.cleanup_failed);
   R(fstat(fd2,&after)==0 && after.st_ino==old.st_ino && after.st_size==old.st_size+5);
   prefix_len=5;
   R(gin_account_output_progress(output,&sd,&progress)==-1 && progress.anchor==7);
   { struct gin_account_history_output_result again=gin_account_output_append_history(output,&sd,fpage,0,8);
     R(again.history==GIN_HISTORY_OK && again.output.code==GIN_CHECKPOINT_INVALID && !again.output.appended && !again.output.checkpoint.published);
   }
   R(fstat(fd2,&after)==0 && after.st_size==old.st_size+5);
   { int readfd=openat(dir2,"out",O_RDONLY|O_CLOEXEC); R(readfd>=0); R(read(readfd,got,sizeof(got))==ln+5 && close(readfd)==0); R(!memcmp(got,logbytes,(size_t)ln) && !memcmp(got+ln,"[1970",5)); }
   fd2=openat(dir2,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_CLOEXEC); R(fd2>=0);
   R(fstat(fd2,&cpafter)==0 && cpafter.st_ino==cpold.st_ino && cpafter.st_size==cpold.st_size);
   R(read(fd2,now,sizeof(now))==cpn && !memcmp(now,cpbytes,(size_t)cpn) && close(fd2)==0);
  }
  gin_history_page_free(fpage); R(close(dir2)==0); lease(&root);
  R(gin_account_output_close(&output)==0); lease(&root);
  R(gin_account_output_new(&root,sinks,plan,receiver,"#&",GIN_RENDER_MODE_PLAIN,&output).code==(!strcmp(fault,"replace")?GIN_CHECKPOINT_UNSAFE:GIN_CHECKPOINT_AMBIGUOUS) && !output);
  puts("PASS history REST MERGED scope no-mutation six-admissions five-destinations");
  puts("PASS storage boundary sticky unchanged-checkpoint ambiguous-reopen");
 }
 R(gin_account_output_close(&output)==0); lease(&root); R(gin_account_sinks_close(sinks)==0); R(gin_root_release(&root)==0);
 gin_account_dispatch_free(dispatcher); gin_account_receive_free(receiver); gin_account_plan_free(plan); gin_topology_free(top); gin_topology_free(booted); gin_http_global_cleanup(); return 0;
}
