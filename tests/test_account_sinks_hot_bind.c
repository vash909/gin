/* BIND qualification: public constructors only; no opaque-state mutation. */
#include "account_registry.h"
#include "account_sinks_hot.h"
#include "fs_private.h"
#include "account_naming.h"
static int root_calls, pair_calls;
static struct gin_account_root_check_result counted_root(const struct gin_root *r,
 const char *o,const char *u,const char *t)
{ ++root_calls; return gin_account_root_check(r,o,u,t); }
static struct gin_fs_pair_result counted_pair(const struct gin_root *r,
 const char *n,const char *w,const char *t,gin_fs_name_prepare_fn f,struct gin_fs_pair *p)
{ ++pair_calls; return gin_root_pair_prepare_named(r,n,w,t,f,p); }
#define gin_account_root_check counted_root
#define gin_root_pair_prepare_named counted_pair
#include "../src/account_sinks.c"
#undef gin_account_root_check
#undef gin_root_pair_prepare_named
#define main inherited_legacy_main
#include "test_account_sinks.c"
#undef main
static const char solo_boot[] = "{\"networks\":[{\"id\":2,\"slug\":\"two\",\"nick\":\"Peer\"}],\"channels\":{\"two\":[]},\"heads\":{}}";
static const char empty_queries[] = "{\"kind\":\"query_windows_list\",\"windows\":{}}";
struct observed_fd { int fd, flags, fdflags; off_t offset; struct stat st; };
struct observed_path { struct stat st; ssize_t length; unsigned char bytes[8192]; };
static const char *const paths[]={".",".gin.account",".gin.lock","one","one/in","one/out","two","two/in","two/out","one/#in","one/#in/in","one/#in/out","one/#out","one/#out/in","one/#out/out","two/@peer","two/@peer/in","two/@peer/out"};
struct observation { struct observed_fd fd[12]; struct observed_path path[18]; int count; };
static void observe(struct gin_account_sinks *s,const struct gin_root *r,struct observation *o)
{
 size_t i; int fds[12],n=0;
 for(i=0;i<2;i++) { fds[n++]=gin_account_sinks_network_input(s,i); fds[n++]=gin_account_sinks_network_output(s,i); }
 for(i=0;i<3;i++) { fds[n++]=gin_account_sinks_window_input(s,i); fds[n++]=gin_account_sinks_window_output(s,i); }
 fds[n++]=r->rootfd; fds[n++]=r->lockfd;
 o->count=fdcount();
 for(i=0;i<12;i++) { o->fd[i].fd=fds[i]; CHECK(fstat(fds[i],&o->fd[i].st)==0); o->fd[i].flags=fcntl(fds[i],F_GETFL); o->fd[i].fdflags=fcntl(fds[i],F_GETFD); o->fd[i].offset=lseek(fds[i],0,SEEK_CUR); }
 for(i=0;i<18;i++) {
  int fd; CHECK(fstatat(r->rootfd,paths[i],&o->path[i].st,AT_SYMLINK_NOFOLLOW)==0); o->path[i].length=0;
  /* Never reopen the process-associated lock inode. */
  if(S_ISREG(o->path[i].st.st_mode)&&strcmp(paths[i],".gin.lock")) {
   fd=openat(r->rootfd,paths[i],O_RDONLY|O_NOFOLLOW); CHECK(fd>=0);
   o->path[i].length=read(fd,o->path[i].bytes,sizeof(o->path[i].bytes)); CHECK(o->path[i].length>=0&&o->path[i].length<(ssize_t)sizeof(o->path[i].bytes)&&close(fd)==0);
  }
 }
}
static void same_stat(const struct stat *a,const struct stat *b)
{ CHECK(a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size); }
static void unchanged(struct gin_account_sinks *s,const struct gin_root *r,const struct observation *before)
{
 struct observation after; size_t i; size_t dirs=0,files=0,fifos=0;
 observe(s,r,&after); CHECK(after.count==before->count);
 for(i=0;i<12;i++) { CHECK(after.fd[i].fd==before->fd[i].fd&&after.fd[i].flags==before->fd[i].flags&&after.fd[i].fdflags==before->fd[i].fdflags&&after.fd[i].offset==before->fd[i].offset); same_stat(&after.fd[i].st,&before->fd[i].st); }
 for(i=0;i<18;i++) { same_stat(&after.path[i].st,&before->path[i].st); CHECK(after.path[i].length==before->path[i].length&&!memcmp(after.path[i].bytes,before->path[i].bytes,(size_t)before->path[i].length)); }
 /* A fresh directory description avoids readdir's shared dup offset. */
 { int fd=openat(r->rootfd,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW); CHECK(fd>=0); tree(fd,&dirs,&files,&fifos); CHECK(close(fd)==0); }
 CHECK(dirs==5&&files==7&&fifos==5);
}
static void characterization(void)
{
 struct gin_topology *b=NULL,*t=NULL,*other=NULL;
 struct gin_account_plan *p=NULL;
 struct gin_account_registry *reg=NULL,*alternate=NULL;
 struct gin_account_registry_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","SyntheticName","#",7},bad;
 struct gin_credential c={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_root r=GIN_ROOT_INITIALIZER; struct gin_account_sinks *s=NULL;
 struct gin_account_sinks_hot_result h; struct gin_account_registry_delta d;
 struct observation before; char path[4096]; const char *tmp=getenv("TMPDIR"); size_t i;
 const char *different="{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Someone\",\"opened_at\":\"x\"}]}}";
 if(!tmp||!*tmp) tmp=".build";
 CHECK(snprintf(path,sizeof(path),"%s/bind-guards-XXXXXX",tmp)>0&&mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,"#","SyntheticName",&p)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(t,&scope,&reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_registry_generation(reg)==0&&gin_account_registry_window_count(reg)==3&&gin_account_registry_source_count(reg)==10);
 CHECK(!strcmp(gin_account_registry_source_at(reg,7)->target,"Peer"));
 CHECK(gin_root_acquire(path,&r)==0&&gin_account_sinks_new(&r,&c,p,"#",&s).code==GIN_ACCOUNT_SINKS_OK);
 CHECK(write(gin_account_sinks_window_output(s,2),"independent-prefix\n",19)==19);
 observe(s,&r,&before); root_calls=pair_calls=0;
#define REJECT(expr,code_) do { h=(expr); CHECK(h.code==(code_)&&!h.added&&!h.paths_attempted&&!h.cleanup_failed&&!h.error&&h.generation==0&&h.window_count==3); CHECK(root_calls==0&&pair_calls==0); unchanged(s,&r,&before); } while(0)
 REJECT(gin_account_sinks_hot_append(s,&scope,0,0),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: append-before-bind characterization");
 h=gin_account_sinks_hot_bind(NULL,reg,&scope,0); CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_INVALID&&!h.window_count&&!h.paths_attempted); puts("bind-case: null-sinks characterization");
 REJECT(gin_account_sinks_hot_bind(s,NULL,&scope,0),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: null-registry characterization");
 REJECT(gin_account_sinks_hot_bind(s,reg,NULL,0),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: null-scope characterization");
 for(i=0;i<5;i++) {
  bad=scope;
  if(i==0) bad.origin="https://other.invalid";
  if(i==1) bad.subject_uuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  if(i==2) bad.topic_subject="OtherDisplay";
  if(i==3) bad.epoch=8;
  if(i==4) bad.chantypes="#&";
  CHECK(gin_account_registry_new(t,&bad,&alternate)==GIN_ACCOUNT_REGISTRY_OK);
  gin_account_registry_free(alternate); alternate=NULL;
  REJECT(gin_account_sinks_hot_bind(s,reg,&bad,0),GIN_ACCOUNT_SINKS_HOT_SCOPE);
  printf("bind-case: legal-scope-field-%lu characterization\n",(unsigned long)i);
 }
 for(i=0;i<4;i++) {
  bad=scope; if(i==0) bad.origin=NULL; if(i==1) bad.subject_uuid=NULL; if(i==2) bad.topic_subject=NULL; if(i==3) bad.chantypes=NULL;
  REJECT(gin_account_sinks_hot_bind(s,reg,&bad,0),GIN_ACCOUNT_SINKS_HOT_SCOPE);
  printf("bind-case: null-scope-field-%lu characterization\n",(unsigned long)i);
 }
 bad=scope; bad.epoch=0;
 REJECT(gin_account_sinks_hot_bind(s,reg,&bad,0),GIN_ACCOUNT_SINKS_HOT_SCOPE); puts("bind-case: zero-epoch characterization");
 REJECT(gin_account_sinks_hot_bind(s,reg,&scope,1),GIN_ACCOUNT_SINKS_HOT_STALE); puts("bind-case: wrong-generation characterization");
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)different,strlen(different),&other)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_registry_new(other,&scope,&alternate)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(!strcmp(gin_account_registry_window_at(alternate,2)->name.original_wire_target,"Someone")&&!strcmp(gin_account_registry_source_at(alternate,7)->target,"Someone"));
 CHECK(!strcmp(gin_account_plan_window_at(p,2)->name.original_wire_target,"Peer"));
 REJECT(gin_account_sinks_hot_bind(s,alternate,&scope,0),GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED); puts("bind-case: different-cold-window characterization");
 gin_account_registry_free(alternate); alternate=NULL;
 { char changed[sizeof(boot)]; char *at; struct gin_topology *b2=NULL,*t2=NULL;
 memcpy(changed,boot,sizeof(boot)); at=strstr(changed,"Other"); CHECK(at); memcpy(at,"Fresh",5);
 CHECK(gin_topology_parse_boot((const unsigned char *)changed,strlen(changed),"#",&b2)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b2,(const unsigned char *)queries,strlen(queries),&t2)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_registry_new(t2,&scope,&alternate)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_registry_window_count(alternate)==3&&gin_account_registry_source_count(alternate)==10);
 CHECK(!strcmp(gin_account_registry_window_at(alternate,2)->name.original_wire_target,"Peer"));
 CHECK(!strcmp(gin_account_registry_source_at(alternate,9)->target,"Fresh")&&!strcmp(gin_account_registry_source_at(reg,9)->target,"Other"));
 REJECT(gin_account_sinks_hot_bind(s,alternate,&scope,0),GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED); puts("bind-case: different-cold-own-nick-source characterization");
 gin_account_registry_free(alternate); alternate=NULL; gin_topology_free(t2); gin_topology_free(b2);
 }
 CHECK(gin_account_registry_apply(reg,t,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK&&!d.windows_added&&!d.sources_added&&!d.generation);
 h=gin_account_sinks_hot_bind(s,reg,&scope,0);
 CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_OK&&!h.added&&!h.paths_attempted&&!h.error&&h.window_count==3&&!h.generation&&root_calls==1&&!pair_calls);
 unchanged(s,&r,&before); puts("bind-case: cold-bind-after-noop-apply characterization");
 root_calls=0;
 REJECT(gin_account_sinks_hot_bind(s,reg,&scope,0),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: repeat-bind-invalid-noop characterization");
 REJECT(gin_account_sinks_hot_append(s,NULL,0,0),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: bound-null-append characterization");
 REJECT(gin_account_sinks_hot_append(s,&scope,0,2),GIN_ACCOUNT_SINKS_HOT_OK); puts("bind-case: existing-append-noop characterization");
 REJECT(gin_account_sinks_hot_bind(s,reg,&scope,1),GIN_ACCOUNT_SINKS_HOT_INVALID); puts("bind-case: bound-state-before-stale characterization");
 lease(path,1);
#undef REJECT
 CHECK(gin_account_sinks_close(s)==0&&gin_root_release(&r)==0);
 gin_account_registry_free(reg); gin_account_plan_free(p); gin_topology_free(other); gin_topology_free(t); gin_topology_free(b);
}
static void preextended(void)
{
 struct gin_topology *b=NULL,*empty=NULL,*full=NULL;
 struct gin_account_plan *p=NULL;
 struct gin_account_registry *reg=NULL;
 struct gin_account_registry_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","SyntheticName","#",7};
 struct gin_credential c={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_account_registry_delta d;
 struct gin_account_sinks *s=NULL;
 struct gin_root r=GIN_ROOT_INITIALIZER;
 struct gin_account_sinks_hot_result h;
 char path[4096]; const char *tmp=getenv("TMPDIR");
 if(!tmp||!*tmp) tmp=".build";
 CHECK(snprintf(path,sizeof(path),"%s/bind-preextended-XXXXXX",tmp)>0&&mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)solo_boot,strlen(solo_boot),"#",&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)empty_queries,strlen(empty_queries),&empty)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&full)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_registry_new(empty,&scope,&reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_registry_apply(reg,full,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(d.windows_added==1&&!d.sources_added&&d.generation==1);
 CHECK(gin_account_plan_new(full,"#","SyntheticName",&p)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_window_count(reg)==1&&gin_account_registry_source_count(reg)==4);
 CHECK(!strcmp(gin_account_registry_source_at(reg,3)->target,"Peer"));
 CHECK(gin_root_acquire(path,&r)==0);
 CHECK(gin_account_sinks_new(&r,&c,p,"#",&s).code==GIN_ACCOUNT_SINKS_OK);
 { struct stat old,now; int out=gin_account_sinks_window_output(s,0),in=gin_account_sinks_window_input(s,0),fl=fcntl(out,F_GETFL),ff=fcntl(out,F_GETFD),count=fdcount(); off_t offset;
 CHECK(write(out,"frozen",6)==6&&fstat(out,&old)==0); offset=lseek(out,0,SEEK_CUR);
 root_calls=pair_calls=0;
 h=gin_account_sinks_hot_bind(s,reg,&scope,0);
 CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_STALE&&!h.paths_attempted&&!root_calls&&!pair_calls); puts("bind-case: preextended-stale characterization");
 h=gin_account_sinks_hot_bind(s,reg,&scope,1);
 fprintf(stderr,"preextended matching cold layout: code=%d expected=%d\n",h.code,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
 CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED&&!h.added&&!h.paths_attempted&&!h.error&&!h.cleanup_failed&&h.window_count==1&&!h.generation);
 CHECK(!root_calls&&!pair_calls&&fdcount()==count&&gin_account_sinks_window_input(s,0)==in&&gin_account_sinks_window_output(s,0)==out);
 CHECK(fcntl(out,F_GETFL)==fl&&fcntl(out,F_GETFD)==ff&&lseek(out,0,SEEK_CUR)==offset&&fstat(out,&now)==0); same_stat(&old,&now);
 { char bytes[6]; int fd=openat(r.rootfd,"two/@peer/out",O_RDONLY|O_NOFOLLOW); CHECK(fd>=0&&read(fd,bytes,6)==6&&!memcmp(bytes,"frozen",6)&&close(fd)==0); }
 lease(path,1);
 }
 CHECK(gin_account_sinks_close(s)==0&&gin_root_release(&r)==0);
 gin_account_plan_free(p); gin_account_registry_free(reg); gin_topology_free(full); gin_topology_free(empty); gin_topology_free(b);
 puts("bind-case: preextended matching layout rejected");
}
int main(int argc,char **argv)
{
 int probe=sinks_probe_dispatch(argc,argv);
 if(probe>=0) return probe;
 if(argc!=1) return 2;
 sinks_test_executable=argv[0];
 characterization();
 preextended();
 return 0;
}
