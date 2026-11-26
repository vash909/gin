/* Quiet QUERY focused history composition. Zero/UNKNOWN anchors remain zero:
 * LIVE IDs are never promoted to processing prefixes. Not account READY. */
#include "account_history_one.h"
#include "account_receive_hot.h"
#include "account_dispatch_hot.h"
#include "account_output_hot.h"
#include "account_sinks_hot.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"SETUP line %d: %s errno=%d\n",__LINE__,#x,errno); exit(2); } } while(0)
/* Private one-shot observation of the actual child descriptor slot while HTTP
 * is held. No pathname reopen or OFD-identity claim. Only fstat/write (POSIX
 * async-signal-safe) run in the handler; both pipe ends are nonblocking, and
 * this fixed 48-byte record is below POSIX's minimum PIPE_BUF. SIGUSR1 masks
 * itself. Preserve errno, use no stdio/allocation/curl, never retry a write. */
static volatile sig_atomic_t probe_output=-1,probe_target=-1;
static void probe_stat(int signo)
{
 int saved=errno,rc; struct stat st; unsigned char wire[48];
 uint64_t values[6]; size_t i,j; ssize_t sent;
 (void)signo;
 values[0]=0x47494e4644503031ULL; values[2]=(uint64_t)probe_target;
 rc=fstat((int)probe_target,&st); values[1]=rc==0?0u:(uint64_t)errno;
 values[3]=rc==0?(uint64_t)st.st_dev:0u;
 values[4]=rc==0?(uint64_t)st.st_ino:0u;
 values[5]=rc==0?(uint64_t)st.st_size:0u;
 for(i=0;i<6;i++) for(j=0;j<8;j++) wire[i*8+j]=(unsigned char)(values[i]>>(56-8*j));
 sent=write((int)probe_output,wire,sizeof(wire)); (void)sent;
 errno=saved;
}
static void service(struct gin_account_receive *a)
{ R(!gin_account_receive_peek(a)); R(gin_account_receive_service(a,20,123)==GIN_CONNECT_OK); }
static void waithead(struct gin_account_receive *a)
{ unsigned i; for(i=0;!gin_account_receive_peek(a);i++) { R(i<150); service(a); } }
static void drain(struct gin_account_receive *a)
{ unsigned i; for(i=0;!gin_account_receive_drained(a);i++) { R(i<150); service(a); } }
static void progress(struct gin_account_output *o,size_t i)
{
 struct gin_account_destination d={0,0,GIN_RENDER_QUERY,0}; struct gin_checkpoint_progress p;
 d.window_index=i; R(gin_account_output_progress(o,&d,&p)==0 && !p.anchor && p.unknown_before);
}
static void append_old(struct gin_account_receive *a,struct gin_account_dispatch *d,
 struct gin_account_output *o,const char *body,int64_t id)
{
 struct gin_account_destination dest; struct gin_render_record *r=NULL;
 struct gin_history_page *p=NULL; struct gin_account_inventory *inventory=NULL;
 struct gin_account_output_result written; struct gin_phoenix_view bytes;
 const struct gin_history_scope *scope; char expected[256];
 waithead(a);
 R(gin_account_dispatch_next_inventory(d,&dest,&r,&p,&inventory)==GIN_SESSION_OK);
 R(r && p && !inventory && dest.network_index==0 && dest.window_index==1 && dest.role==GIN_RENDER_QUERY && !dest.terminal);
 R(gin_history_page_source(p)==GIN_HISTORY_LIVE && gin_history_page_count(p)==1 && gin_history_page_row(p,0)->id==id);
 scope=gin_history_page_scope(p);
 R(scope && scope->network_id==1 && !strcmp(scope->subject_id,"22345678-1234-1234-1234-123456789abc") && !strcmp(scope->target,"Peer") && scope->socket_epoch==1);
 snprintf(expected,sizeof(expected),"[1970-01-01T00:00:00.123Z] privmsg sender=\"sender\" body=\"%s\"\n",body);
 bytes=gin_render_record_bytes(r); R(bytes.length==strlen(expected) && !memcmp(bytes.data,expected,bytes.length));
 /* Zero was independently established as UNKNOWN before the first LIVE row. */
 written=gin_account_output_append(o,&dest,r,0);
 R(written.code==GIN_CHECKPOINT_OK && written.appended==bytes.length && written.checkpoint.code==GIN_CHECKPOINT_OK && written.checkpoint.published && !written.cleanup_failed && !written.checkpoint.cleanup_failed);
 progress(o,1); gin_render_record_free(r); gin_history_page_free(p);
}
static void attest(struct gin_account_output *out,struct gin_root *root,
 struct gin_account_sinks *sinks,struct gin_account_plan *plan,
 struct gin_account_receive *a,struct gin_account_registry *registry,
 const struct gin_account_registry_scope *scope,struct gin_account_dispatch *d,int admitted)
{
 struct gin_account_output_hot_result os; struct gin_account_dispatch_hot_result ds;
 errno=EDOM; os=gin_account_output_hot_status(out,root,sinks,plan,a,registry,scope,1,2);
 R(errno==EDOM && os.code==GIN_ACCOUNT_OUTPUT_HOT_OK && os.window_admitted && os.window_count==3 && os.generation==1 && !os.cleanup_failed);
 ds=gin_account_dispatch_hot_status(d,a,registry,scope,1,2);
 R(errno==EDOM && ds.code==GIN_SESSION_OK && ds.generation==1 && !ds.proposal_pending && ds.window_admitted==admitted);
}
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old\",\"joined\":true,\"source\":\"joined\"}],\"two\":[]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
 struct gin_connect_settings settings={0}; struct gin_credential c={0};
 struct gin_topology *base=NULL,*cold=NULL; struct gin_account_plan *plan=NULL,*sorted=NULL;
 struct gin_account_receive *a=NULL; struct gin_account_dispatch *d=NULL;
 struct gin_account_registry *registry=NULL; struct gin_account_registry_scope scope;
 struct gin_account_registry_delta delta; struct gin_account_inventory *inventory=NULL;
 struct gin_account_destination dest; struct gin_render_record *record=NULL; struct gin_history_page *page=NULL;
 struct gin_root root=GIN_ROOT_INITIALIZER; struct gin_account_sinks *sinks=NULL; struct gin_account_output *out=NULL;
 const struct gin_topology_window *old; const char *old_name;
 const struct gin_connect_topic_status *old_sources[9];
 struct stat before,after,root_before,root_after,lock_before,lock_after;
 int held,probe,held_negative; int oldfd,rootfd,lockfd; size_t index=SIZE_MAX,i; unsigned n;
 /* Closed private positive setup profile; never inferred from a wrapper.
  * The shared HTTP settings cover startup AND fresh history preflights.
  * HTTP total, JOIN, recovery/held/negative and parent bounds stay unchanged. */
 int instrumented=argc==6 && !strcmp(argv[5],"--instrumented-positive-setup");
 enum gin_connect_code start_code;
 struct sigaction probe_action,previous_probe_action;
 const char *probe_text=getenv("GIN_HISTORY_ONE_PROBE_FD"); char *probe_end;
 long probe_number; struct stat probe_meta; int probe_flags;
 R(probe_text && *probe_text); errno=0; probe_number=strtol(probe_text,&probe_end,10);
 R(!errno && !*probe_end && probe_number>=0 && probe_number<=SIG_ATOMIC_MAX);
 probe_output=(sig_atomic_t)probe_number;
 R(fstat((int)probe_output,&probe_meta)==0 && S_ISFIFO(probe_meta.st_mode));
 probe_flags=fcntl((int)probe_output,F_GETFL);
 R(probe_flags>=0 && (probe_flags&O_NONBLOCK)!=0);
 memset(&probe_action,0,sizeof(probe_action)); probe_action.sa_handler=probe_stat;
 R(sigemptyset(&probe_action.sa_mask)==0 && sigaction(SIGUSR1,&probe_action,&previous_probe_action)==0);
 R(argc==5 || instrumented); held_negative=!strcmp(argv[4],"held-unsupported"); held=!strcmp(argv[4],"held") || held_negative; probe=!strcmp(argv[4],"unsupported") || !strcmp(argv[4],"metadata"); R(held || probe || !strcmp(argv[4],"quiet")); alarm(14); R(strlen(argv[1])<sizeof(c.origin)); strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc"); strcpy(c.subject_name,"Alice"); strcpy(c.token,"fixture-token.DO-NOT-LOG");
 settings.http.origin=c.origin; settings.http.ca_file=argv[2]; settings.http.connect_ms=instrumented ? 1400 : 700; settings.http.total_ms=1500;
 settings.policy.client_protocol=4; settings.policy.required_server_protocol=4; settings.join_ms=2500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&base)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(base,(const unsigned char *)queries,strlen(queries),&cold)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(cold,"#&","Alice",&plan)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_plan_topic_count(plan)==9 && gin_account_plan_window_count(plan)==2);
 R(gin_account_receive_new_hot(&settings,&c,plan,"#&",&a)==GIN_CONNECT_OK);
 start_code=gin_account_receive_start(a);
 if(start_code!=GIN_CONNECT_OK)
  fprintf(stderr,"NATIVE_START phase=startup code=%d name=%s state=%d\n",
   start_code,gin_connect_error(start_code),gin_account_receive_state(a));
 R(start_code==GIN_CONNECT_OK);
 waithead(a); R(gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_epoch(a)==1);
 R(!strcmp(gin_account_receive_identity(a)->subject_name,"FreshAlice"));
 scope.origin=c.origin; scope.subject_uuid=c.subject_id; scope.topic_subject="FreshAlice"; scope.chantypes="#&"; scope.epoch=1;
 R(gin_account_registry_new(cold,&scope,&registry)==GIN_ACCOUNT_REGISTRY_OK);
 R(gin_account_dispatch_new_hot(a,plan,cold,registry,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(gin_root_acquire(argv[3],&root)==0);
 R(gin_account_sinks_new(&root,&c,plan,"#&",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 R(gin_account_output_new(&root,sinks,plan,a,"#&",GIN_RENDER_MODE_PLAIN,&out).code==GIN_CHECKPOINT_OK);
 R(gin_account_sinks_hot_bind(sinks,registry,&scope,0).code==GIN_ACCOUNT_SINKS_HOT_OK);
 R(gin_account_output_hot_bind(out,registry,&scope,0).code==GIN_CHECKPOINT_OK);
 progress(out,1); append_old(a,d,out,"initial-old",101); drain(a);
 old=gin_account_registry_window_at(registry,1); old_name=old->name.canonical_target;
 R(!strcmp(old_name,"peer")); oldfd=gin_account_sinks_window_output(sinks,1);
 rootfd=root.rootfd; lockfd=root.lockfd;
 R(fstat(oldfd,&before)==0 && fstat(rootfd,&root_before)==0 && fstat(lockfd,&lock_before)==0);
 for(i=0;i<9;i++) { old_sources[i]=gin_account_receive_source(a,i); R(old_sources[i] && old_sources[i]->state==GIN_CONNECT_TOPIC_JOINED); }
 puts("INITIAL_READY_OLD_COMMITTED"); fflush(stdout);
 waithead(a);
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory)==GIN_SESSION_OK && inventory && !record && !page);
 R(gin_account_inventory_epoch(inventory)==1 && !strcmp(gin_account_inventory_identity(inventory)->subject_name,"FreshAlice"));
 R(gin_account_plan_new(gin_account_inventory_topology(inventory),"#&","FreshAlice",&sorted)==GIN_ACCOUNT_PLAN_OK);
 R(!strcmp(gin_account_plan_window_at(sorted,1)->name.canonical_target,"aaron") && !strcmp(gin_account_plan_window_at(sorted,2)->name.canonical_target,"peer"));
 R(gin_account_dispatch_resolve_inventory(d,&inventory,0,&delta)==GIN_SESSION_OK && !inventory);
 R(delta.windows_added==1 && delta.sources_added==1 && delta.generation==1);
 scope=*gin_account_registry_scope(registry);
 R(gin_account_registry_window_at(registry,1)==old && old->name.canonical_target==old_name && !strcmp(old_name,"peer"));
 R(!strcmp(gin_account_registry_window_at(registry,2)->name.original_wire_target,"Aaron"));
 R(gin_account_sinks_hot_append(sinks,&scope,1,2).code==GIN_ACCOUNT_SINKS_HOT_OK);
 R(gin_account_output_hot_append(out,&scope,1,2).code==GIN_CHECKPOINT_OK);
 progress(out,2); attest(out,&root,sinks,plan,a,registry,&scope,d,0);
 R(gin_account_receive_subscribe_query_live(a,"one","Aaron",1,&index)==GIN_CONNECT_OK && index==9);
 R(gin_account_receive_source(a,9)->state==GIN_CONNECT_TOPIC_PENDING);
 for(n=0;gin_account_receive_source(a,9)->state!=GIN_CONNECT_TOPIC_JOINED;n++) { R(n<150); service(a); }
 R(gin_account_receive_state(a)==GIN_CONNECT_READY && !gin_account_receive_count(a));
 R(!strcmp(gin_account_receive_source(a,9)->join_ref,"10")); drain(a);
 R(gin_account_dispatch_admit_window(d,&scope,1,2)==GIN_SESSION_OK);
 attest(out,&root,sinks,plan,a,registry,&scope,d,1); progress(out,2);
 for(i=0;i<9;i++) R(gin_account_receive_source(a,i)==old_sources[i] && old_sources[i]->state==GIN_CONNECT_TOPIC_JOINED);
 R(gin_account_sinks_window_output(sinks,1)==oldfd && fstat(oldfd,&after)==0);
 R(before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size);
 printf("NEW_QUIET_ADMITTED stable_index=2 sorted_index=1 source=9 ref=10 after=0 old_fd=%d old_size=%lld\n",oldfd,(long long)before.st_size); fflush(stdout);
 /* Parent releases only the old destination after this actual client receipt. */
 append_old(a,d,out,"after-new-old",102); drain(a);
 progress(out,2); attest(out,&root,sinks,plan,a,registry,&scope,d,1);
 R(gin_account_sinks_window_output(sinks,1)==oldfd && fstat(oldfd,&after)==0);
 R(before.st_dev==after.st_dev && before.st_ino==after.st_ino && after.st_size>before.st_size);
 R(root.rootfd==rootfd && root.lockfd==lockfd && fstat(rootfd,&root_after)==0 && fstat(lockfd,&lock_after)==0);
 R(root_before.st_dev==root_after.st_dev && root_before.st_ino==root_after.st_ino && lock_before.st_dev==lock_after.st_dev && lock_before.st_ino==lock_after.st_ino);
 printf("OLD_POST_ADMISSION_COMMITTED fd=%d dev=%llu ino=%llu before=%lld after=%lld anchor=0 unknown=1 checkpoint_published=1\n",oldfd,(unsigned long long)after.st_dev,(unsigned long long)after.st_ino,(long long)before.st_size,(long long)after.st_size); fflush(stdout);
 {
  struct gin_account_history_one_input input={0};
  struct gin_account_history_one_result result;
  struct gin_history_status status;
  struct timespec now;
  R(clock_gettime(CLOCK_MONOTONIC,&now)==0);
  input.root=&root;input.sinks=sinks;input.plan=plan;input.receiver=a;
  input.dispatcher=d;input.output=out;input.registry=registry;input.scope=&scope;
  input.generation=1;input.deadline_ms=(uint64_t)now.tv_sec*1000u+(uint64_t)now.tv_nsec/1000000u+5000u;
  input.new_window_first=2;input.new_window_count=1;input.window_index=2;
  input.http=&settings.http;input.policy=&settings.policy;input.credential=&c;
  input.encryption=GIN_RENDER_MODE_PLAIN;
  if(probe) waithead(a);
  R(oldfd>=0 && oldfd<=SIG_ATOMIC_MAX); probe_target=(sig_atomic_t)oldfd;
  result=gin_account_history_one(&input);
  probe_target=-1;
  if(!strcmp(argv[4],"unsupported") || held_negative) {
   if(held_negative) {
    const struct gin_live_view *head=gin_account_receive_peek(a);
    const struct gin_connect_topic_status *source=gin_account_receive_source(a,9);
    R(result.code==GIN_HISTORY_ROUTE && !result.drained && result.incomplete && !result.retained);
    R(result.pages==0 && result.old_appended==1 && !result.appended && !result.cleanup_failed && !result.quarantined);
    R(gin_account_receive_count(a)==1 && head && head->source_index==9 && head->observed_epoch==1);
    R(source && source->state==GIN_CONNECT_TOPIC_JOINED && !strcmp(source->join_ref,"10"));
    R(!strcmp(head->source_join_ref,"10") && !strcmp(head->fields.join_ref,"10") && !strcmp(head->fields.topic,source->topic));
    R(gin_account_receive_epoch(a)==1 && gin_account_receive_state(a)==GIN_CONNECT_READY);
    progress(out,1);progress(out,2);
    printf("HELD_REJECTED source=9 ref=10 epoch=1 old_appended=1 selected_appended=0\n");fflush(stdout);
   }
   printf("TYPED_RESULT code=%d drained=%d incomplete=%d retained=%d queued=%lu pages=%lu cleanup=%d quarantine=%d\n",result.code,result.drained,result.incomplete,result.retained!=NULL,(unsigned long)gin_account_receive_count(a),(unsigned long)result.pages,result.cleanup_failed,result.quarantined);fflush(stdout);
   gin_history_recovery_free(result.retained);
   goto cleanup;
  }
  if(result.code!=GIN_HISTORY_OK) fprintf(stderr,"HISTORY code=%d pages=%lu\n",result.code,(unsigned long)result.pages);
  R(result.code==GIN_HISTORY_OK && result.drained && !result.incomplete && !result.stopped);
  R(result.pages==1 && result.appended==(held?2u:0u) && result.old_appended==(held?1u:0u) && result.output_attempted==held);
  if(held) R(result.output.code==GIN_CHECKPOINT_OK && result.output.checkpoint.published && !result.output.cleanup_failed && !result.output.checkpoint.cleanup_failed);
  R(!result.pending && result.retained && !result.cleanup_failed && !result.quarantined);
  status=gin_history_recovery_status(result.retained);
  R(status.state==GIN_HISTORY_VISIBLE_DRAINED && status.pages==1 && status.rows==(held?2u:0u) && !status.live_rows && status.fetch_after==(held?20:0) && status.unknown_before);
  R(gin_history_page_count(gin_history_recovery_view(result.retained))==(held?2u:0u));
  if(held) {
   const struct gin_history_page *v=gin_history_recovery_view(result.retained);
   const struct gin_history_scope *s=gin_history_page_scope(v);
   R(gin_history_page_source(v)==GIN_HISTORY_MERGED && s && !strcmp(s->target,"Aaron") && s->network_id==1 && s->socket_epoch==1 && s->role==GIN_RENDER_QUERY);
   R(gin_history_page_row(v,0)->id==10 && gin_history_page_row(v,1)->id==20);
  }
  progress(out,1);progress(out,2);
  R(gin_account_sinks_window_output(sinks,1)==oldfd && fstat(oldfd,&before)==0);
  R(before.st_dev==after.st_dev && before.st_ino==after.st_ino && before.st_size==after.st_size+(held?(off_t)strlen("[1970-01-01T00:00:00.123Z] privmsg sender=\"sender\" body=\"held-old\"\n"):0));
  R(root.rootfd==rootfd && root.lockfd==lockfd && fstat(rootfd,&root_after)==0 && fstat(lockfd,&lock_after)==0);
  R(root_before.st_dev==root_after.st_dev && root_before.st_ino==root_after.st_ino && lock_before.st_dev==lock_after.st_dev && lock_before.st_ino==lock_after.st_ino);
  for(i=0;i<9;i++) R(gin_account_receive_source(a,i)==old_sources[i] && old_sources[i]->state==GIN_CONNECT_TOPIC_JOINED);
  R(!strcmp(gin_account_receive_source(a,6)->join_ref,"7") && gin_account_receive_epoch(a)==1);
  printf("NEW_HISTORY_VISIBLE_DRAINED pages=1 rows=%u after=0 unknown=1 cursor=%u old_appended=%u selected_appended=%u\n",held?2u:0u,held?20u:0u,held?1u:0u,held?2u:0u);fflush(stdout);
  /* This quiet tracer does not establish the separate live handoff policy. */
  gin_history_recovery_free(result.retained);
 }
cleanup:
 R(sigaction(SIGUSR1,&previous_probe_action,NULL)==0);
 R(close((int)probe_output)==0); probe_output=-1;
 R(gin_account_output_close(&out)==0); R(gin_account_sinks_close(sinks)==0); R(gin_root_release(&root)==0);
 gin_account_dispatch_free(d); gin_account_receive_close(a);
 R(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a)); gin_account_receive_free(a);
 gin_account_registry_free(registry); gin_account_plan_free(sorted); gin_account_plan_free(plan); gin_topology_free(cold); gin_topology_free(base);
 gin_credential_clear(&c); gin_http_global_cleanup(); puts("CLEANUP_COMPLETE"); fflush(stdout); return 0;
}
