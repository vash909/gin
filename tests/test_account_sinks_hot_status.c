/* Getter-only private state injection is explicitly separated below.
 * Real cold construction, registry advance and sink append are public APIs. */
#include "../src/account_registry.c"
#include "../src/account_sinks.c"
#define main status_legacy_main
#include "test_account_sinks.c"
#undef main
#ifdef GIN_STATUS_VALGRIND
#include <valgrind/memcheck.h>
#endif

/* Constructors can copy indeterminate struct padding. Define only test-owned
 * byte snapshots under Memcheck, never the production objects, before comparing
 * every representation byte. This is not an error suppression or getter seam. */
static int representation_equal(void *saved, const void *object, size_t length)
{
 unsigned char *after=malloc(length); int equal;
 CHECK(after); memcpy(after,object,length);
#ifdef GIN_STATUS_VALGRIND
 VALGRIND_MAKE_MEM_DEFINED(saved,length);
 VALGRIND_MAKE_MEM_DEFINED(after,length);
#endif
 equal=!memcmp(saved,after,length); free(after); return equal;
}

static void
status_check(const struct gin_account_sinks *s, const struct gin_root *root,
 const struct gin_account_plan *plan, const struct gin_account_registry *reg,
 const struct gin_account_registry_scope *scope, uint64_t generation,
 enum gin_account_sinks_hot_code code)
{
 unsigned char *saved = NULL, *registry_saved = NULL;
 struct gin_root root_saved;
 struct gin_account_registry_scope scope_saved;
 struct gin_account_sinks_hot_result h;
 struct stat before[2 * SINK_LIMIT * 2], after;
 int fds[2 * SINK_LIMIT * 2], fl[2 * SINK_LIMIT * 2], fdfl[2 * SINK_LIMIT * 2];
 off_t offsets[2 * SINK_LIMIT * 2];
 size_t i, n=0;
 if(s) {
  saved=malloc(sizeof(*s)); CHECK(saved); memcpy(saved,s,sizeof(*s));
  for(i=0;i<s->networks;i++) { fds[n++]=s->network[i].input; fds[n++]=s->network[i].output; }
  for(i=0;i<s->windows;i++) { fds[n++]=s->window[i].input; fds[n++]=s->window[i].output; }
 }
 if(reg) { registry_saved=malloc(sizeof(*reg)); CHECK(registry_saved); memcpy(registry_saved,reg,sizeof(*reg)); }
 if(root) memcpy(&root_saved,root,sizeof(root_saved));
 if(scope) memcpy(&scope_saved,scope,sizeof(scope_saved));
 for(i=0;i<n;i++) { CHECK(fstat(fds[i],&before[i])==0); fl[i]=fcntl(fds[i],F_GETFL); fdfl[i]=fcntl(fds[i],F_GETFD); offsets[i]=lseek(fds[i],0,SEEK_CUR); }
 errno=EDOM;
 h=gin_account_sinks_hot_status(s,root,plan,reg,scope,generation);
 CHECK(errno==EDOM);
 CHECK(h.code==code&&!h.added&&!h.paths_attempted);
 CHECK(h.window_count==(s?s->windows:0)&&h.generation==(s?s->generation:0));
 CHECK(h.cleanup_failed==(s?s->hot_cleanup:false));
 CHECK(h.error==(s&&s->hot_failed?s->hot_error:0));
 if(s) CHECK(representation_equal(saved,s,sizeof(*s)));
 if(reg) CHECK(representation_equal(registry_saved,reg,sizeof(*reg)));
 if(root) CHECK(!memcmp(&root_saved,root,sizeof(root_saved)));
 if(scope) CHECK(!memcmp(&scope_saved,scope,sizeof(scope_saved)));
 for(i=0;i<n;i++) {
  CHECK(fstat(fds[i],&after)==0&&after.st_dev==before[i].st_dev&&after.st_ino==before[i].st_ino&&after.st_size==before[i].st_size);
  CHECK(fcntl(fds[i],F_GETFL)==fl[i]&&fcntl(fds[i],F_GETFD)==fdfl[i]&&lseek(fds[i],0,SEEK_CUR)==offsets[i]);
 }
 free(registry_saved); free(saved);
}

int main(int argc,char **argv)
{
 struct gin_topology *b=NULL,*t=NULL,*a=NULL;
 struct gin_account_plan *p=NULL,*equivalent_plan=NULL;
 struct gin_account_registry *reg=NULL,*equivalent_reg=NULL;
 struct gin_account_registry_delta delta;
 struct gin_account_registry_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","SyntheticName","#",7}, bad;
 struct gin_credential credential={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_root root=GIN_ROOT_INITIALIZER, root_copy;
 struct gin_account_sinks *s=NULL;
 const char *tmp=getenv("TMPDIR"); char path[4096], bytes[6]; int fd;
 const char *extra="{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"}]}}";
 size_t i; int probe=sinks_probe_dispatch(argc,argv);
 if(probe>=0) return probe;
 if(argc!=1) return 2;
 sinks_test_executable=argv[0]; alarm(25);
 if(!tmp||!*tmp) tmp=".build";
 CHECK(snprintf(path,sizeof(path),"%s/hot-status-XXXXXX",tmp)>0&&mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)extra,strlen(extra),&a)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,"#","SyntheticName",&p)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_plan_new(t,"#","SyntheticName",&equivalent_plan)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(t,&scope,&reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_registry_new(t,&scope,&equivalent_reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_root_acquire(path,&root)==0); root_copy=root;
 CHECK(gin_account_sinks_new(&root,&credential,p,"#",&s).code==GIN_ACCOUNT_SINKS_OK);
 CHECK(write(gin_account_sinks_window_output(s,2),"prefix",6)==6);
 status_check(s,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 CHECK(gin_account_sinks_hot_bind(s,reg,&scope,0).code==GIN_ACCOUNT_SINKS_HOT_OK);
 status_check(s,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_OK);
 status_check(s,&root_copy,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_SCOPE);
 status_check(s,&root,equivalent_plan,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_SCOPE);
 status_check(s,&root,p,equivalent_reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_SCOPE);
 status_check(NULL,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 status_check(s,NULL,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 status_check(s,&root,NULL,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 status_check(s,&root,p,NULL,&scope,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 status_check(s,&root,p,reg,NULL,0,GIN_ACCOUNT_SINKS_HOT_INVALID);
 for(i=0;i<10;i++) {
  bad=scope;
  if(i==0) bad.origin="https://other.invalid";
  if(i==1) bad.subject_uuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa";
  if(i==2) bad.topic_subject="Other";
  if(i==3) bad.chantypes="#&";
  if(i==4) bad.epoch++;
  if(i==5) bad.origin=NULL;
  if(i==6) bad.subject_uuid=NULL;
  if(i==7) bad.topic_subject=NULL;
  if(i==8) bad.chantypes=NULL;
  if(i==9) bad.epoch=0;
  status_check(s,&root,p,reg,&bad,0,GIN_ACCOUNT_SINKS_HOT_SCOPE);
 }
 status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_STALE);
 status_check(s,&root,p,reg,&scope,UINT64_MAX,GIN_ACCOUNT_SINKS_HOT_STALE);
 CHECK(gin_account_registry_apply(reg,a,&scope,&delta)==GIN_ACCOUNT_REGISTRY_OK&&delta.generation==1&&delta.windows_added==1);
 status_check(s,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_STALE);
 status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_STALE);
 CHECK(gin_account_sinks_hot_append(s,&scope,1,3).code==GIN_ACCOUNT_SINKS_HOT_OK);
 status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_OK);
 status_check(s,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_STALE);
 /* Private state injection: getter coverage ONLY, no FS failure claim. */
 { struct gin_account_registry_scope saved=reg->scope;
  reg->scope.epoch++; status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_SCOPE); reg->scope=saved;
  s->scope.epoch++; status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_SCOPE); s->scope.epoch--;
  s->generation=2; status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_STALE); s->generation=1;
  s->hot_cleanup=true; status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_OK);
  s->hot_failed=true; s->hot_error=ENOSPC;
  status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_FAILED);
  status_check(s,NULL,NULL,NULL,NULL,0,GIN_ACCOUNT_SINKS_HOT_FAILED);
  status_check(s,&root_copy,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_FAILED);
  status_check(s,&root,p,reg,&scope,0,GIN_ACCOUNT_SINKS_HOT_FAILED);
  s->hot_cleanup=false; status_check(s,&root,p,reg,&scope,1,GIN_ACCOUNT_SINKS_HOT_FAILED);
 }
 fd=openat(root.rootfd,"two/@peer/out",O_RDONLY|O_NOFOLLOW);
 CHECK(fd>=0&&read(fd,bytes,6)==6&&!memcmp(bytes,"prefix",6)&&close(fd)==0);
 lease(path,1); CHECK(gin_account_sinks_close(s)==0); lease(path,1);
 CHECK(gin_root_release(&root)==0); lease(path,0);
 gin_account_registry_free(equivalent_reg); gin_account_registry_free(reg);
 gin_account_plan_free(equivalent_plan); gin_account_plan_free(p);
 gin_topology_free(a); gin_topology_free(t); gin_topology_free(b);
 puts("hot status: public construction/append and private getter-state coverage: ok");
 return 0;
}
