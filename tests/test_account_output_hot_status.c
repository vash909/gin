/* First metadata-attestation vertical slice; no history recovery claim.
 * The receiver facade is PRIVATE and UNAUTHENTICATED. All other components
 * below are real. BASELINE_ONLY omits getter calls; it never supplies a shim.
 * Optional linker-wrap guards arm ONLY during the getter.
 * Byte snapshots cover output (including entries), root, receiver and scope,
 * plus typed opaque plan/registry/sinks/checkpoint inspection and exposed views.
 * Inspection helpers include actual production definitions in separate TUs. */
#include "account_output_hot.h"
#include "account_output.c"
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>
#ifdef GIN_STATUS_GUARDS
extern int gin_status_guard_armed;
extern int gin_status_fail_openat;
#endif
#ifdef GIN_STATUS_VALGRIND
#include <valgrind/memcheck.h>
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
struct gin_account_receive { struct gin_account_identity identity; uint64_t epoch; };
const struct gin_account_identity *gin_account_receive_identity(const struct gin_account_receive *r)
{ return r ? &r->identity : NULL; }
uint64_t gin_account_receive_epoch(const struct gin_account_receive *r)
{ return r ? r->epoch : 0; }
static const char *executable;
static char lock_path[4096];
static void held(void)
{
 pid_t child=fork(); int status;
 CHECK(child>=0);
 if (!child) { alarm(2); execl(executable,executable,"--lease-probe",lock_path,(char *)NULL); _exit(127); }
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
extern size_t gin_status_plan_copy(const struct gin_account_plan *,void *,size_t);
extern size_t gin_status_registry_copy(const struct gin_account_registry *,void *,size_t);
extern size_t gin_status_sinks_copy(const struct gin_account_sinks *,void *,size_t);
extern size_t gin_status_checkpoint_copy(const struct gin_checkpoint *,void *,size_t);
extern int gin_status_sinks_cleanup(struct gin_account_sinks *,int);
static int expected_cleanup;
static unsigned calls;
/* Only independent test-owned representations receive definedness annotation. */
static int bytes_equal(void *a,void *b,size_t n)
{
#ifdef GIN_STATUS_VALGRIND
 VALGRIND_MAKE_MEM_DEFINED(a,n); VALGRIND_MAKE_MEM_DEFINED(b,n);
#endif
 return !memcmp(a,b,n);
}
struct views { unsigned char data[4194304]; size_t used; };
static void save(struct views *v,const void *p,size_t n)
{ if(p) { CHECK(n<=sizeof(v->data)-v->used); memcpy(v->data+v->used,p,n);v->used+=n; } }
static void text(struct views *v,const char *p) { if(p) save(v,p,strlen(p)+1); }
/* Both operands land in independently owned buffers before comparison.
 * These bounds limit fixture storage, not guesses about production sizes. */
#define OPAQUE(kind,p) do { size_t z=gin_status_##kind##_copy(p,NULL,0); \
 CHECK(z<=sizeof(v->data)-v->used); \
 CHECK(gin_status_##kind##_copy(p,v->data+v->used,sizeof(v->data)-v->used)==z);v->used+=z; } while(0)
static void opaque_views(struct views *v,const struct gin_account_output *out,
 const struct gin_account_sinks *sinks,const struct gin_account_plan *plan,
 const struct gin_account_registry *reg)
{
 size_t i;
 OPAQUE(plan,plan);OPAQUE(registry,reg);OPAQUE(sinks,sinks);
 if(out) {
  OPAQUE(plan,out->plan);OPAQUE(registry,out->registry);OPAQUE(sinks,out->sinks);
  if(out->root) save(v,out->root,sizeof(*out->root));
  if(out->receiver) { save(v,out->receiver,sizeof(*out->receiver));
   text(v,out->receiver->identity.origin);text(v,out->receiver->identity.subject_id);text(v,out->receiver->identity.subject_name); }
  for(i=0;i<out->count && i<LIMIT;i++) OPAQUE(checkpoint,out->entries[i].checkpoint);
 }
}
#undef OPAQUE
static void network_view(struct views *v,const struct gin_topology_network *n)
{ if(n) { save(v,n,sizeof(*n));text(v,n->slug);text(v,n->own_nick);text(v,n->component); } }
static void window_view(struct views *v,const struct gin_topology_window *w)
{
 if(w) { save(v,w,sizeof(*w));text(v,w->opened_at);text(v,w->name.input_network_slug);
 text(v,w->name.original_wire_target);text(v,w->name.canonical_target);text(v,w->name.network_component);text(v,w->name.window_component); }
}
static void metadata(struct views *v,const struct gin_account_output *out,
 const struct gin_account_sinks *sinks,const struct gin_account_plan *plan,
 const struct gin_account_registry *reg,const struct gin_account_receive *recv,
 const struct gin_account_registry_scope *scope)
{
 size_t i,n; v->used=0;
 opaque_views(v,out,sinks,plan,reg);
 if(out) { text(v,out->types);for(i=0;i<out->count && i<LIMIT;i++) {
  const struct gin_history_scope *s=&out->entries[i].scope;
  text(v,s->origin);text(v,s->subject_id);text(v,s->network_slug);text(v,s->target);text(v,s->own_nick);text(v,s->chantypes);
 } }
 if(recv) { text(v,recv->identity.origin);text(v,recv->identity.subject_id);text(v,recv->identity.subject_name); }
 if(scope) { text(v,scope->origin);text(v,scope->subject_uuid);text(v,scope->topic_subject);text(v,scope->chantypes); }
 if(plan) {
  n=gin_account_plan_network_count(plan);save(v,&n,sizeof(n));for(i=0;i<n;i++) network_view(v,gin_account_plan_network_at(plan,i));
  n=gin_account_plan_window_count(plan);save(v,&n,sizeof(n));for(i=0;i<n;i++) window_view(v,gin_account_plan_window_at(plan,i));
 }
 if(reg) {
  const struct gin_account_registry_scope *s=gin_account_registry_scope(reg);
  uint64_t g=gin_account_registry_generation(reg);save(v,&g,sizeof(g));save(v,s,sizeof(*s));
  text(v,s->origin);text(v,s->subject_uuid);text(v,s->topic_subject);text(v,s->chantypes);
  n=gin_account_registry_network_count(reg);save(v,&n,sizeof(n));for(i=0;i<n;i++) network_view(v,gin_account_registry_network_at(reg,i));
  n=gin_account_registry_window_count(reg);save(v,&n,sizeof(n));for(i=0;i<n;i++) window_view(v,gin_account_registry_window_at(reg,i));
 }
 if(out && out->registry) {
  struct gin_account_sinks_hot_result hot=gin_account_sinks_hot_status(out->sinks,out->root,out->plan,out->registry,gin_account_registry_scope(out->registry),gin_account_registry_generation(out->registry));
  save(v,&hot.code,sizeof(hot.code));save(v,&hot.added,sizeof(hot.added));save(v,&hot.paths_attempted,sizeof(hot.paths_attempted));
  save(v,&hot.cleanup_failed,sizeof(hot.cleanup_failed));save(v,&hot.error,sizeof(hot.error));save(v,&hot.window_count,sizeof(hot.window_count));save(v,&hot.generation,sizeof(hot.generation));
 }
 if(sinks) {
  int fd; n=gin_account_sinks_network_count(sinks);save(v,&n,sizeof(n));
  for(i=0;i<n;i++) { fd=gin_account_sinks_network_input(sinks,i);save(v,&fd,sizeof(fd));fd=gin_account_sinks_network_output(sinks,i);save(v,&fd,sizeof(fd)); }
  n=gin_account_sinks_window_count(sinks);save(v,&n,sizeof(n));
  for(i=0;i<n;i++) { fd=gin_account_sinks_window_input(sinks,i);save(v,&fd,sizeof(fd));fd=gin_account_sinks_window_output(sinks,i);save(v,&fd,sizeof(fd)); }
 }
}
static void attest(const struct gin_account_output *out,const struct gin_root *root,
 const struct gin_account_sinks *sinks,const struct gin_account_plan *plan,
 const struct gin_account_receive *receiver,const struct gin_account_registry *registry,
 const struct gin_account_registry_scope *scope,uint64_t generation,size_t index,
 size_t count,enum gin_account_output_hot_code code,int admitted)
{
 static unsigned char before[sizeof(struct gin_account_output)],after[sizeof(struct gin_account_output)];
 unsigned char rb[sizeof(struct gin_root)],ra[sizeof(struct gin_root)],vb[sizeof(struct gin_account_receive)],va[sizeof(struct gin_account_receive)];
 unsigned char sb[sizeof(struct gin_account_registry_scope)],sa[sizeof(struct gin_account_registry_scope)];
 static struct views mb,ma;
 struct gin_account_output_hot_result result;
 uint64_t actual=out && out->registry?gin_account_registry_generation(out->registry):0;
 if(out) memcpy(before,out,sizeof(before));
 if(root) memcpy(rb,root,sizeof(rb));
 if(receiver) memcpy(vb,receiver,sizeof(vb));
 if(scope) memcpy(sb,scope,sizeof(sb));
 metadata(&mb,out,sinks,plan,registry,receiver,scope);
 errno=EDOM;
#ifdef GIN_STATUS_GUARDS
 gin_status_guard_armed=1;
#endif
 result=gin_account_output_hot_status(out,root,sinks,plan,receiver,registry,scope,generation,index);
#ifdef GIN_STATUS_GUARDS
 gin_status_guard_armed=0;
#endif
 CHECK(errno==EDOM);
 if(out) { memcpy(after,out,sizeof(after));CHECK(bytes_equal(before,after,sizeof(before))); }
 if(root) { memcpy(ra,root,sizeof(ra));CHECK(bytes_equal(rb,ra,sizeof(rb))); }
 if(receiver) { memcpy(va,receiver,sizeof(va));CHECK(bytes_equal(vb,va,sizeof(vb))); }
 if(scope) { memcpy(sa,scope,sizeof(sa));CHECK(bytes_equal(sb,sa,sizeof(sb))); }
 metadata(&ma,out,sinks,plan,registry,receiver,scope);
 CHECK(mb.used==ma.used && bytes_equal(mb.data,ma.data,mb.used));
 if(result.code!=code) fprintf(stderr,"case %u code got=%d expected=%d\n",calls,result.code,code);
 CHECK(result.code==code && result.generation==actual && result.window_count==count);
 CHECK(result.window_admitted==admitted && result.cleanup_failed==expected_cleanup);
 /* Named-field mutation control on independent test-owned output copies. */
 if(out) { struct gin_account_output *control=malloc(sizeof(*control));CHECK(control);
 memcpy(control,out,sizeof(*control));control->epoch^=1;memcpy(after,control,sizeof(after));
 CHECK(!bytes_equal(before,after,sizeof(before)));free(control); }
 calls++;
}
static void edges(struct gin_account_output *out,struct gin_root *root,
 struct gin_account_sinks *sinks,struct gin_account_plan *plan,
 struct gin_account_receive *receiver,struct gin_account_registry *reg,
 struct gin_account_registry_scope *scope,const struct gin_topology *cold)
{
 struct gin_account_registry_scope sc=*scope;
 struct gin_account_receive rv=*receiver;
 struct gin_root rt=*root;
 struct gin_account_plan *otherplan=NULL;
 struct gin_account_registry *otherreg=NULL;
 struct gin_account_sinks *othersinks=NULL;
 struct gin_credential cred={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct entry original=out->entries[2]; size_t oldcount=out->count;
 const struct gin_account_registry *savedreg=out->registry;
#define A(o,r,s,p,v,g,c,gen,idx,n,code,admit) attest(o,r,s,p,v,g,c,gen,idx,n,GIN_ACCOUNT_OUTPUT_HOT_##code,admit)
#define E(code) A(out,root,sinks,plan,receiver,reg,scope,1,1,2,code,0)
 A(NULL,root,sinks,plan,receiver,reg,scope,1,1,0,INVALID,0);
 A(out,NULL,sinks,plan,receiver,reg,scope,1,1,2,INVALID,0);
 A(out,root,NULL,plan,receiver,reg,scope,1,1,2,INVALID,0);
 A(out,root,sinks,NULL,receiver,reg,scope,1,1,2,INVALID,0);
 A(out,root,sinks,plan,NULL,reg,scope,1,1,2,INVALID,0);
 A(out,root,sinks,plan,receiver,NULL,scope,1,1,2,INVALID,0);
 A(out,root,sinks,plan,receiver,reg,NULL,1,1,2,INVALID,0);
 A(out,root,sinks,plan,receiver,reg,scope,1,2,2,INVALID,0);
 out->registry=NULL; E(INVALID);out->registry=savedreg;
 out->count=LIMIT+1; A(out,root,sinks,plan,receiver,reg,scope,1,1,0,INVALID,0);out->count=oldcount;
 out->count=0; A(out,root,sinks,plan,receiver,reg,scope,1,1,0,INVALID,0);out->count=oldcount;
 A(out,root,sinks,plan,receiver,reg,scope,0,1,2,STALE,0);
 A(out,root,sinks,plan,receiver,reg,scope,UINT64_MAX,SIZE_MAX,2,STALE,0);
 A(out,root,sinks,plan,receiver,reg,scope,1,SIZE_MAX,2,OK,0);
 A(out,&rt,sinks,plan,receiver,reg,scope,1,1,2,SCOPE,0);
 A(out,root,sinks,plan,&rv,reg,scope,1,1,2,SCOPE,0);
 CHECK(gin_account_plan_new(cold,scope->chantypes,scope->topic_subject,&otherplan)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(cold,scope,&otherreg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_sinks_new(root,&cred,plan,scope->chantypes,&othersinks).code==GIN_ACCOUNT_SINKS_OK);
 A(out,root,othersinks,plan,receiver,reg,scope,1,1,2,SCOPE,0);
 A(out,root,sinks,otherplan,receiver,reg,scope,1,1,2,SCOPE,0);
 A(out,root,sinks,plan,receiver,otherreg,scope,1,1,2,SCOPE,0);
 CHECK(gin_account_sinks_close(othersinks)==0);gin_account_registry_free(otherreg);gin_account_plan_free(otherplan);
#define S(field,value) sc=*scope;sc.field=value;A(out,root,sinks,plan,receiver,reg,&sc,1,1,2,SCOPE,0)
 S(origin,"https://other.invalid");S(subject_uuid,"22345678-1234-1234-1234-123456789abc");S(topic_subject,"Other");S(chantypes,"&");S(epoch,0);S(epoch,8);
#undef S
#define R(field,value) { const char *saved=receiver->identity.field;receiver->identity.field=value;E(SCOPE);receiver->identity.field=saved; }
 R(origin,"https://other.invalid");R(subject_id,"22345678-1234-1234-1234-123456789abc");R(subject_name,"Other");
#undef R
 receiver->epoch=0;E(SCOPE);receiver->epoch=7;out->epoch=8;E(SCOPE);out->epoch=7;
#define M(field,value) out->entries[2].field=value;E(SCOPE);out->entries[2]=original
 M(window,0);M(network,1);M(checkpoint,NULL);M(fd,-1);M(fd,out->entries[1].fd);
 M(scope.origin,"https://other.invalid");M(scope.subject_id,"other");M(scope.network_slug,"two");M(scope.network_id,2);
 M(scope.target,"Other");M(scope.own_nick,"Other");M(scope.chantypes,"&");M(scope.socket_epoch,8);
 M(scope.role,GIN_RENDER_CHANNEL);M(scope.encryption,GIN_RENDER_MODE_PLAIN+1);
#undef M
 out->entries[2].failed=1;E(FAILED);out->entries[2]=original;
 out->entries[1].failed=1;E(FAILED);
 A(out,NULL,NULL,NULL,NULL,NULL,NULL,99,99,2,FAILED,0);out->entries[1].failed=0;
 out->hot_failed=1;E(FAILED);A(out,NULL,NULL,NULL,NULL,NULL,NULL,99,99,2,FAILED,0);out->hot_failed=0;
 out->hot_cleanup=1;expected_cleanup=1;E(FAILED);A(out,NULL,NULL,NULL,NULL,NULL,NULL,99,99,2,FAILED,0);expected_cleanup=0;out->hot_cleanup=0;
 A(out,root,sinks,plan,receiver,reg,scope,1,1,2,OK,1);
#undef E
#undef A
}

#endif
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
static void channel_fixture(const char *tmp)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Self\"}],\"channels\":{\"one\":[{\"name\":\"#Room\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{}}";
 struct gin_credential c={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_account_registry_scope scope={c.origin,c.subject_id,c.subject_name,"#",7};
 struct gin_account_receive recv={{c.origin,c.subject_id,c.subject_name},7};
 struct gin_topology *base=NULL,*cold=NULL;
 struct gin_account_plan *plan=NULL;struct gin_account_registry *reg=NULL;
 struct gin_account_sinks *sinks=NULL;struct gin_account_output *out=NULL;
 struct gin_root root=GIN_ROOT_INITIALIZER;
 struct gin_checkpoint_progress progress;
 const struct gin_account_destination selected={0,0,GIN_RENDER_CHANNEL,0};
 char path[4096];
 CHECK(snprintf(path,sizeof(path),"%s/gin-output-channel-XXXXXX",tmp)>0 && mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&base)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(base,(const unsigned char *)queries,strlen(queries),&cold)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(cold,"#",c.subject_name,&plan)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(cold,&scope,&reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_registry_window_at(reg,0)->role==GIN_REMOTE_CHANNEL);
 CHECK(gin_root_acquire(path,&root)==0);
 CHECK(gin_account_sinks_new(&root,&c,plan,"#",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 CHECK(gin_account_output_new(&root,sinks,plan,&recv,"#",GIN_RENDER_MODE_PLAIN,&out).code==GIN_CHECKPOINT_OK);
 CHECK(gin_account_sinks_hot_bind(sinks,reg,&scope,0).code==GIN_ACCOUNT_SINKS_HOT_OK);
 CHECK(gin_account_output_hot_bind(out,reg,&scope,0).code==GIN_CHECKPOINT_OK);
 CHECK(gin_account_output_progress(out,&selected,&progress)==0 && progress.unknown_before && !progress.anchor);
 CHECK(out->entries[1].scope.role==GIN_RENDER_CHANNEL && out->entries[1].checkpoint);
 attest(out,&root,sinks,plan,&recv,reg,&scope,0,0,1,GIN_ACCOUNT_OUTPUT_HOT_OK,1);
 attest(out,&root,sinks,plan,&recv,reg,&scope,0,SIZE_MAX,1,GIN_ACCOUNT_OUTPUT_HOT_OK,0);
 CHECK(gin_account_output_close(&out)==0);
 CHECK(gin_account_sinks_close(sinks)==0);CHECK(gin_root_release(&root)==0);
 gin_account_registry_free(reg);gin_account_plan_free(plan);gin_topology_free(cold);gin_topology_free(base);
 puts("PASS ordinary CHANNEL constructor/bind/selected status and UNKNOWN checkpoint");
}
#endif
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Self\"}],\"channels\":{\"one\":[]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"old\"}]}}";
 const char extension[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"},{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"old\"}]}}";
 struct gin_credential credential={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_account_registry_scope active={credential.origin,credential.subject_id,credential.subject_name,"#",7};
 struct gin_account_receive receiver={{credential.origin,credential.subject_id,credential.subject_name},7};
 struct gin_topology *base=NULL,*cold=NULL,*proposal=NULL;
 struct gin_account_plan *plan=NULL;
 struct gin_account_registry *registry=NULL;
 struct gin_account_registry_delta delta;
 struct gin_root root=GIN_ROOT_INITIALIZER;
 struct gin_account_sinks *sinks=NULL;
 struct gin_account_output *out=NULL;
 const struct gin_account_destination old={0,0,GIN_RENDER_QUERY,0},added={0,1,GIN_RENDER_QUERY,0};
 struct gin_checkpoint_progress progress;
 struct stat old_before,old_after,new_before,new_after;
 off_t old_offset,new_offset;
 int old_fd,new_fd;
 char path[4096]; const char *tmp=getenv("TMPDIR");
 if(argc==3 && !strcmp(argv[1],"--lease-probe")) {
  struct flock lock; int probe,rc,saved;
  alarm(2); probe=open(argv[2],O_RDWR|O_NOFOLLOW|O_CLOEXEC); CHECK(probe>=0);
  memset(&lock,0,sizeof(lock)); lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
  rc=fcntl(probe,F_SETLK,&lock); saved=errno; CHECK(close(probe)==0);
  return rc==-1 && (saved==EACCES || saved==EAGAIN) ? 0 : 1;
 }
 CHECK(argc==1); executable=argv[0]; alarm(20);
 if(!tmp || !*tmp) tmp=".build";
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 channel_fixture(tmp);
#endif
 CHECK(snprintf(path,sizeof(path),"%s/gin-output-status-XXXXXX",tmp)>0 && mkdtemp(path));
 CHECK(snprintf(lock_path,sizeof(lock_path),"%s/.gin.lock",path)>0 && strlen(path)+10<sizeof(lock_path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&base)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(base,(const unsigned char *)queries,strlen(queries),&cold)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(base,(const unsigned char *)extension,strlen(extension),&proposal)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(cold,"#",credential.subject_name,&plan)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(cold,&active,&registry)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_root_acquire(path,&root)==0);
 CHECK(gin_account_sinks_new(&root,&credential,plan,"#",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 CHECK(gin_account_output_new(&root,sinks,plan,&receiver,"#",GIN_RENDER_MODE_PLAIN,&out).code==GIN_CHECKPOINT_OK);
 CHECK(gin_account_sinks_hot_bind(sinks,registry,&active,0).code==GIN_ACCOUNT_SINKS_HOT_OK);
 CHECK(gin_account_output_hot_bind(out,registry,&active,0).code==GIN_CHECKPOINT_OK);
 CHECK(gin_account_output_progress(out,&old,&progress)==0 && progress.anchor==0 && progress.unknown_before);
 CHECK(out->count==2 && out->entries[1].checkpoint!=NULL && out->entries[1].window==0);
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 attest(out,&root,sinks,plan,&receiver,registry,&active,0,SIZE_MAX,1,GIN_ACCOUNT_OUTPUT_HOT_OK,0);
 attest(out,&root,sinks,plan,&receiver,registry,&active,0,0,1,GIN_ACCOUNT_OUTPUT_HOT_OK,1);
#endif
 CHECK(gin_account_registry_apply(registry,proposal,&active,&delta)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(delta.windows_added==1 && delta.generation==1);
 CHECK(!strcmp(gin_account_registry_window_at(registry,0)->name.original_wire_target,"Peer"));
 CHECK(!strcmp(gin_account_registry_window_at(registry,1)->name.original_wire_target,"Aaron"));
 CHECK(gin_account_registry_window_at(registry,1)->role==GIN_REMOTE_QUERY);
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 attest(out,&root,sinks,plan,&receiver,registry,&active,1,1,1,GIN_ACCOUNT_OUTPUT_HOT_STALE,0);
#endif
 CHECK(gin_account_sinks_hot_append(sinks,&active,1,1).code==GIN_ACCOUNT_SINKS_HOT_OK);
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 attest(out,&root,sinks,plan,&receiver,registry,&active,1,1,1,GIN_ACCOUNT_OUTPUT_HOT_OK,0);
 attest(out,&root,sinks,plan,&receiver,registry,&active,1,SIZE_MAX,1,GIN_ACCOUNT_OUTPUT_HOT_OK,0);
#endif
 CHECK(gin_account_output_hot_append(out,&active,1,1).code==GIN_CHECKPOINT_OK);
 gin_topology_free(proposal); proposal=NULL;
 CHECK(gin_account_output_progress(out,&added,&progress)==0 && progress.anchor==0 && progress.unknown_before);
 CHECK(out->count==3 && out->entries[2].checkpoint!=NULL && out->entries[2].window==1);
 old_fd=gin_account_sinks_window_output(sinks,0); new_fd=gin_account_sinks_window_output(sinks,1);
 CHECK(old_fd>=0 && new_fd>=0 && out->entries[1].fd==old_fd && out->entries[2].fd==new_fd);
 CHECK(fstat(old_fd,&old_before)==0 && fstat(new_fd,&new_before)==0);
 old_offset=lseek(old_fd,0,SEEK_CUR); new_offset=lseek(new_fd,0,SEEK_CUR);
 CHECK(old_offset>=0 && new_offset>=0); held();
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 {
  struct gin_account_receive other; memcpy(&other,&receiver,sizeof(other));
  CHECK(&other!=&receiver && !memcmp(&other,&receiver,sizeof(other)));
  attest(out,&root,sinks,plan,&receiver,registry,&active,1,0,2,GIN_ACCOUNT_OUTPUT_HOT_OK,1);
  attest(out,&root,sinks,plan,&receiver,registry,&active,1,1,2,GIN_ACCOUNT_OUTPUT_HOT_OK,1);
  attest(out,&root,sinks,plan,&other,registry,&active,1,1,2,GIN_ACCOUNT_OUTPUT_HOT_SCOPE,0);
 }
#endif
#ifndef GIN_OUTPUT_STATUS_BASELINE_ONLY
 edges(out,&root,sinks,plan,&receiver,registry,&active,cold);
 {
  struct gin_account_sinks_hot_result delegated;
  int old_cleanup=gin_status_sinks_cleanup(sinks,1);
  CHECK(old_cleanup==0);
  errno=EDOM;
  delegated=gin_account_sinks_hot_status(sinks,&root,plan,registry,&active,1);
  CHECK(errno==EDOM && delegated.code==GIN_ACCOUNT_SINKS_HOT_OK && delegated.cleanup_failed && !delegated.error);
  expected_cleanup=1;
  attest(out,&root,sinks,plan,&receiver,registry,&active,1,1,2,GIN_ACCOUNT_OUTPUT_HOT_FAILED,0);
  attest(out,NULL,NULL,NULL,NULL,NULL,NULL,99,99,2,GIN_ACCOUNT_OUTPUT_HOT_FAILED,0);
  CHECK(gin_status_sinks_cleanup(sinks,old_cleanup)==1);expected_cleanup=0;
  delegated=gin_account_sinks_hot_status(sinks,&root,plan,registry,&active,1);
  CHECK(!delegated.cleanup_failed && delegated.code==GIN_ACCOUNT_SINKS_HOT_OK);
  attest(out,&root,sinks,plan,&receiver,registry,&active,1,1,2,GIN_ACCOUNT_OUTPUT_HOT_OK,1);
  puts("PASS independently controlled sink cleanup METADATA FAULT and restoration; not failed-close/quarantine/FD reclamation");
 }
#endif
 /* These observations supplement the opt-in forbidden-operation guard. */
 CHECK(fstat(old_fd,&old_after)==0 && fstat(new_fd,&new_after)==0);
 CHECK(old_before.st_dev==old_after.st_dev && old_before.st_ino==old_after.st_ino && old_before.st_size==old_after.st_size);
 CHECK(new_before.st_dev==new_after.st_dev && new_before.st_ino==new_after.st_ino && new_before.st_size==new_after.st_size);
 CHECK(lseek(old_fd,0,SEEK_CUR)==old_offset && lseek(new_fd,0,SEEK_CUR)==new_offset);
#if defined(GIN_STATUS_GUARDS) && !defined(GIN_OUTPUT_STATUS_BASELINE_ONLY)
 {
  const char third[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"},{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"old\"},{\"network_id\":1,\"target_nick\":\"Zed\",\"opened_at\":\"third\"}]}}";
  struct gin_topology *next=NULL;
  struct gin_account_sinks_hot_result failure;
  CHECK(gin_topology_apply_queries(base,(const unsigned char *)third,strlen(third),&next)==GIN_TOPOLOGY_OK);
  CHECK(gin_account_registry_apply(registry,next,&active,&delta)==GIN_ACCOUNT_REGISTRY_OK && delta.generation==2);
  gin_status_fail_openat=1;
  failure=gin_account_sinks_hot_append(sinks,&active,2,2);
  gin_status_fail_openat=0;
  CHECK(failure.code==GIN_ACCOUNT_SINKS_HOT_IO && failure.error!=0 && !failure.cleanup_failed);
  attest(out,&root,sinks,plan,&receiver,registry,&active,2,1,2,GIN_ACCOUNT_OUTPUT_HOT_FAILED,0);
  attest(out,NULL,NULL,NULL,NULL,NULL,NULL,99,SIZE_MAX-1,2,GIN_ACCOUNT_OUTPUT_HOT_FAILED,0);
  gin_topology_free(next);
 }
#endif
 CHECK(gin_account_output_close(&out)==0 && out==NULL); held();
 CHECK(fcntl(old_fd,F_GETFD)>=0 && fcntl(new_fd,F_GETFD)>=0);
 CHECK(gin_account_plan_window_count(plan)==1);
#if defined(GIN_STATUS_GUARDS) && !defined(GIN_OUTPUT_STATUS_BASELINE_ONLY)
 CHECK(gin_account_registry_window_count(registry)==3);
#else
 CHECK(gin_account_registry_window_count(registry)==2);
#endif
 CHECK(gin_account_receive_epoch(&receiver)==7);
 CHECK(gin_account_sinks_close(sinks)==0); held();
 CHECK(gin_root_release(&root)==0);
 gin_account_registry_free(registry); gin_account_plan_free(plan); gin_topology_free(cold); gin_topology_free(base);
#ifdef GIN_OUTPUT_STATUS_BASELINE_ONLY
 puts("PASS BASELINE_ONLY: real cold binding; contiguous quiet QUERY hot admission; UNKNOWN checkpoint; borrowers and root lease preserved; ordered cleanup. No getter executed; unauthenticated receiver facade.");
#else
 printf("PASS %u output-status characterizations; typed opaque representations, exposed views and errno preserved.\n",calls);
#endif
 printf("Retained tree: %s\n",path);
 return 0;
}
