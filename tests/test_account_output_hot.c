/* Component-composition baseline, NOT a defect in the legacy output API.
 * Unauthenticated private receiver identity/epoch facade only. All topology,
 * registry, sinks, output, history records and checkpoints are real components.
 * Retain test trees as evidence; never unlink a lock or truncate output. */
#include "account_output_hot.h"
#include "account_output.c"
#ifdef GIN_OUTPUT_SNAPSHOT_VG
#include <valgrind/memcheck.h>
#endif
#include "account_sinks_hot.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
/* This test implements a private facade, not the production receiver layout. */
struct gin_account_receive { struct gin_account_identity identity; uint64_t epoch; };
const struct gin_account_identity *gin_account_receive_identity(const struct gin_account_receive *r)
{ return r ? &r->identity : NULL; }
uint64_t gin_account_receive_epoch(const struct gin_account_receive *r)
{ return r ? r->epoch : 0; }
static const char *executable;
static char lock_path[4096];
static void held(const struct gin_root *r)
{
 pid_t child=fork(); int status;
 (void)r;
 CHECK(child>=0);
 if (!child) {
  /* Exec discards inherited heap; strict child leak checks remain enabled. */
  alarm(2); execl(executable,executable,"--lease-probe",lock_path,(char *)NULL);
  _exit(127);
 }
 CHECK(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void append_old(struct gin_account_output *out, int64_t anchor)
{
 const struct gin_history_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","one","$server","#","Self",1,7,GIN_RENDER_STATUS,GIN_RENDER_MODE_PLAIN};
 const struct gin_history_request request={GIN_HISTORY_AFTER,0,50};
 const char json[]="[{\"id\":99,\"server_time\":123,\"network\":\"one\",\"channel\":\"$server\",\"kind\":\"notice\",\"sender\":\"trusted\",\"body\":\"prefix\"}]";
 struct gin_history_page *page=NULL;
 const struct gin_account_destination dest={0,SIZE_MAX,GIN_RENDER_STATUS,0};
 struct gin_account_history_output_result wr;
 CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)json,strlen(json),&page)==GIN_HISTORY_OK);
 wr=gin_account_output_append_history(out,&dest,page,0,anchor);
 CHECK(wr.history==GIN_HISTORY_OK && wr.output.code==GIN_CHECKPOINT_OK && wr.output.appended>0 && wr.output.checkpoint.published && !wr.output.cleanup_failed && !wr.output.checkpoint.cleanup_failed);
 gin_history_page_free(page);
}
static void append_new(struct gin_account_output *out)
{
 const struct gin_history_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","one","Aaron","#","Self",1,7,GIN_RENDER_QUERY,GIN_RENDER_MODE_PLAIN};
 const struct gin_history_request request={GIN_HISTORY_AFTER,0,50};
 const char json[]="[{\"id\":999,\"server_time\":123,\"network\":\"one\",\"channel\":\"Aaron\",\"kind\":\"privmsg\",\"sender\":\"Aaron\",\"body\":\"new-query\",\"dm_with\":\"Aaron\"}]";
 const struct gin_account_destination dest={0,1,GIN_RENDER_QUERY,0};
 struct gin_history_page *page=NULL;
 struct gin_account_history_output_result wr;
 struct gin_checkpoint_progress p;
 CHECK(gin_account_output_progress(out,&dest,&p)==0 && p.anchor==0 && p.unknown_before);
 CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)json,strlen(json),&page)==GIN_HISTORY_OK);
 /* Explicit independent processing prefix, deliberately not record ID999. */
 wr=gin_account_output_append_history(out,&dest,page,0,13);
 CHECK(wr.history==GIN_HISTORY_OK && wr.output.code==GIN_CHECKPOINT_OK && wr.output.appended>0 && wr.output.checkpoint.published && !wr.output.cleanup_failed && !wr.output.checkpoint.cleanup_failed);
 gin_history_page_free(page);
 CHECK(gin_account_output_progress(out,&dest,&p)==0 && p.anchor==13);
}
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
 const struct gin_account_destination old={0,SIZE_MAX,GIN_RENDER_STATUS,0}, added={0,1,GIN_RENDER_QUERY,0};
 struct gin_checkpoint_progress progress={999,0};
 struct stat before,after; off_t offset; int fd,missing,control;
 struct entry *old_address; unsigned char old_bytes[sizeof(struct entry)*2],now_bytes[sizeof(struct entry)*2];
 struct gin_account_registry_scope wrong;
 unsigned char prefix[4096],retained[4096];ssize_t prefix_len;
 char path[4096]; const char *tmp=getenv("TMPDIR");
 if(argc==3 && !strcmp(argv[1],"--lease-probe")) {
  struct flock lock; int probe,rc,saved;
  alarm(2); probe=open(argv[2],O_RDWR|O_NOFOLLOW|O_CLOEXEC); CHECK(probe>=0);
  memset(&lock,0,sizeof(lock)); lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
  rc=fcntl(probe,F_SETLK,&lock); saved=errno; CHECK(close(probe)==0);
  return rc==-1 && (saved==EACCES || saved==EAGAIN) ? 0 : 1;
 }
 executable=argv[0]; alarm(20); control=argc==2 && !strcmp(argv[1],"--control"); CHECK(argc==1 || control);
 if (!tmp || !*tmp) tmp=".build";
 CHECK(snprintf(path,sizeof(path),"%s/gin-output-composition-XXXXXX",tmp)>0 && mkdtemp(path));
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
 CHECK(gin_account_output_hot_bind(out,registry,&active,0).code==GIN_CHECKPOINT_INVALID);
 append_old(out,7);
 CHECK(gin_account_output_progress(out,&old,&progress)==0 && progress.anchor==7);
 fd=gin_account_sinks_network_output(sinks,0); CHECK(fstat(fd,&before)==0); offset=lseek(fd,0,SEEK_CUR); CHECK(offset>=0); held(&root);
 old_address=&out->entries[0];memcpy(old_bytes,out->entries,sizeof(old_bytes));
 { int readfd=openat(root.rootfd,"one/out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);CHECK(readfd>=0);
 prefix_len=pread(readfd,prefix,sizeof(prefix),0);CHECK(prefix_len>0);CHECK(close(readfd)==0); }
 CHECK(gin_account_registry_apply(registry,proposal,&active,&delta)==GIN_ACCOUNT_REGISTRY_OK && delta.windows_added==1 && delta.generation==1);
 CHECK(!strcmp(gin_account_registry_window_at(registry,1)->name.original_wire_target,"Aaron"));
 CHECK(gin_account_output_hot_append(out,&active,1,1).code==GIN_CHECKPOINT_INVALID);
 CHECK(gin_account_output_progress(out,&added,&progress)==-1);
 CHECK(gin_account_sinks_hot_append(sinks,&active,1,1).code==GIN_ACCOUNT_SINKS_HOT_OK);
 wrong=active;wrong.topic_subject="Different";
 CHECK(gin_account_output_hot_append(out,&wrong,1,1).code==GIN_CHECKPOINT_SCOPE);
 CHECK(gin_account_output_hot_append(out,&active,0,1).code==GIN_CHECKPOINT_INVALID);
 CHECK(gin_account_output_hot_append(out,&active,1,2).code==GIN_CHECKPOINT_INVALID);
 CHECK(gin_account_output_hot_append(out,&active,1,1).code==GIN_CHECKPOINT_OK);
 CHECK(&out->entries[0]==old_address);memcpy(now_bytes,out->entries,sizeof(now_bytes));
#ifdef GIN_OUTPUT_SNAPSHOT_VG
 /* Only copied test-owned representations: production state is never marked. */
 VALGRIND_MAKE_MEM_DEFINED(old_bytes,sizeof(old_bytes));
 VALGRIND_MAKE_MEM_DEFINED(now_bytes,sizeof(now_bytes));
#endif
 CHECK(!memcmp(old_bytes,now_bytes,sizeof(old_bytes)));
 { int readfd=openat(root.rootfd,"one/out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);CHECK(readfd>=0);
 CHECK(pread(readfd,retained,sizeof(retained),0)==prefix_len && !memcmp(prefix,retained,(size_t)prefix_len));CHECK(close(readfd)==0); }
 CHECK(gin_account_output_hot_append(out,&active,1,1).code==GIN_CHECKPOINT_OK);
 CHECK(gin_account_output_hot_append(out,&active,1,0).code==GIN_CHECKPOINT_OK);
 { const struct gin_account_destination invalid={0,1,GIN_RENDER_QUERY,2};
   CHECK(gin_account_output_progress(out,&invalid,&progress)==-1); }
 gin_topology_free(proposal); proposal=NULL;
 CHECK(gin_account_sinks_window_output(sinks,1)>=0);
 CHECK(fstat(fd,&after)==0 && before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size && lseek(fd,0,SEEK_CUR)==offset);
 CHECK(gin_account_output_progress(out,&old,&progress)==0 && progress.anchor==7);
 missing=gin_account_output_progress(out,&added,&progress)!=0;
 CHECK(!missing); append_new(out);
 CHECK(gin_account_output_progress(out,&old,&progress)==0 && progress.anchor==7);
 { const char expected[]="[1970-01-01T00:00:00.123Z] privmsg sender=\"Aaron\" body=\"new-query\"\n";
   char actual[256];int readfd=openat(root.rootfd,"one/@aaron/out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
   CHECK(readfd>=0);CHECK(read(readfd,actual,sizeof(actual))==(ssize_t)(sizeof(expected)-1));
   CHECK(!memcmp(actual,expected,sizeof(expected)-1) && close(readfd)==0); }
 /* Healthy control after extension proves that legacy output remains usable. */
 append_old(out,8); CHECK(gin_account_output_progress(out,&old,&progress)==0 && progress.anchor==8);
 held(&root); CHECK(gin_account_output_close(&out)==0); held(&root);
 CHECK(gin_account_sinks_close(sinks)==0); held(&root); CHECK(gin_root_release(&root)==0);
 gin_account_registry_free(registry); gin_account_plan_free(plan); gin_topology_free(cold); gin_topology_free(base);
 puts("Healthy actual cold output/checkpoint anchor7; real registry+hot-sinks QUERY append; caller proposal freed; old output anchor8 control; ordered cleanup complete.");
 printf("Retained tree: %s\n",path); fflush(stdout);
 if(control) { CHECK(!missing); puts("PASS hot-output control"); return 0; }
 /* New GREEN; original component-composition RED remains frozen separately. */
 CHECK(!missing);puts("PASS hot-output typed history anchor13; scope/generation/order/repeat/invalid guards; stable old entries and bytes");
 return 0;
}
