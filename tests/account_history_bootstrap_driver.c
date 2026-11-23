#define _POSIX_C_SOURCE 200809L
#include "account_history_bootstrap.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define R(x) do { if(!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int statusfd,stopfd;
static void note(char c) { R(write(statusfd,&c,1)==1); }
static int stop_tick(void *unused,unsigned *wait) { char c; ssize_t n; (void)unused;(void)wait; n=read(stopfd,&c,1); R(n==1 || (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK))); return n==1; }
#ifndef BOOTSTRAP_BASELINE
struct boot_owner;
static struct boot_owner *postfetch_owner;
static struct gin_history_page *postfetch_page;
static unsigned postfetch_checks;
static unsigned stop_latched,appends_after_stop;
static struct gin_http_control *local_stop;
static struct gin_account_history_output_result stop_append(struct gin_account_output *,
 const struct gin_account_destination *,const struct gin_history_page *,size_t,int64_t);
static struct gin_history_fetch_result tracked_fetch(const struct gin_auth_settings *,
 const struct gin_protocol_policy *,const struct gin_credential *,const struct gin_topology *,
 const struct gin_history_scope *,const struct gin_history_request *,struct gin_history_page **,
 struct gin_http_control *);
static enum gin_connect_code tracked_service(struct gin_account_receive *,unsigned,int64_t);
static void tracked_page_free(struct gin_history_page *);
static enum gin_history_code probe_live(struct gin_history_recovery *r,const struct gin_history_page *p,uint64_t now) {
 enum gin_history_code c=gin_history_recovery_live(r,p,now);
 if(c==GIN_HISTORY_OK) { const struct gin_history_row *row=gin_history_page_row(p,0); R(row && (row->id==20 || row->id==30));note(row->id==20?'A':'B'); }
 return c;
}
#define gin_history_recovery_live probe_live
#define gin_history_fetch_controlled tracked_fetch
#define gin_account_receive_service tracked_service
#define gin_history_page_free tracked_page_free
#define gin_account_output_append_history stop_append
#include "../src/account_history_bootstrap.c"
#undef gin_history_recovery_live
#undef gin_history_fetch_controlled
#undef gin_account_receive_service
#undef gin_history_page_free
#undef gin_account_output_append_history
static struct gin_account_history_output_result stop_append(struct gin_account_output *output,
 const struct gin_account_destination *dest,const struct gin_history_page *page,size_t index,int64_t anchor)
{
 struct gin_account_history_output_result result=gin_account_output_append_history(output,dest,page,index,anchor);
 if(result.history==GIN_HISTORY_OK && result.output.code==GIN_CHECKPOINT_OK) {
  const struct gin_history_scope *scope=gin_history_page_scope(page);
  const struct gin_history_row *row=gin_history_page_row(page,index);
  if(stop_latched) appends_after_stop++;
  if(local_stop && !strcmp(scope->target,"#old1") && row->id==10) {
   local_stop->aborted=1;stop_latched=1;
  }
 }
 return result;
}
/* Real HTTP/page lifetimes: no fabricated response, page or Native source. */
static struct gin_history_fetch_result tracked_fetch(const struct gin_auth_settings *s,
 const struct gin_protocol_policy *policy,const struct gin_credential *credential,
 const struct gin_topology *topology,const struct gin_history_scope *scope,
 const struct gin_history_request *request,struct gin_history_page **page,
 struct gin_http_control *control)
{
 struct gin_history_fetch_result result;
 R(!postfetch_page);
 result=gin_history_fetch_controlled(s,policy,credential,topology,scope,request,page,control);
 if(result.code==GIN_HISTORY_OK && *page) { postfetch_page=*page;postfetch_owner=control->opaque; }
 return result;
}
static enum gin_connect_code tracked_service(struct gin_account_receive *receiver,unsigned wait,int64_t received)
{
 if(postfetch_page) {
  if(postfetch_owner->inflight==SIZE_MAX) {
   fputs("POST_FETCH_RESERVATION_ASSERTION\n",stderr);exit(1);
  }
  postfetch_checks++;
 }
 return gin_account_receive_service(receiver,wait,received);
}
static void tracked_page_free(struct gin_history_page *page)
{
 if(page && page==postfetch_page) { postfetch_page=NULL;postfetch_owner=NULL; }
 gin_history_page_free(page);
}
#endif
static void lease(const struct gin_root *r) {
 pid_t p=fork(); int s; R(p>=0); if(!p) {struct flock f={0};f.l_type=F_WRLCK;f.l_whence=SEEK_SET;_exit(fcntl(r->lockfd,F_SETLK,&f)==-1 && (errno==EACCES || errno==EAGAIN)?0:1);} R(waitpid(p,&s,0)==p && !s);
}
int main(int argc,char **argv) {
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#aquiet\",\"joined\":true,\"source\":\"joined\"},{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}],\"2\":[{\"network_id\":2,\"target_nick\":\"Other\",\"opened_at\":\"opaque\"}]}}";
 struct gin_connect_settings s={0}; struct gin_credential c={0};struct gin_topology *b=NULL,*t=NULL;
 struct gin_account_plan *p=NULL;struct gin_account_receive *a=NULL;struct gin_account_dispatch *d=NULL;
 struct gin_root root=GIN_ROOT_INITIALIZER;struct gin_account_sinks *sinks=NULL;struct gin_account_output *output=NULL;
 struct gin_http_control control={stop_tick,NULL,0};unsigned n;
 if(argc==2 && !strcmp(argv[1],"--null-input")) {
  struct gin_account_history_bootstrap_result result=gin_account_history_bootstrap(NULL);
  fprintf(stderr,"NULL_INPUT code=%d incomplete=%d\n",result.code,result.incomplete);
  R(result.code==GIN_HISTORY_INVALID && result.incomplete && !result.appended && !result.pages);
  return 0;
 }
 R(argc==6);alarm(18);statusfd=atoi(argv[4]);stopfd=atoi(argv[5]);
 R((fcntl(statusfd,F_GETFL)&O_NONBLOCK) && (fcntl(stopfd,F_GETFL)&O_NONBLOCK));
 R(strlen(argv[1])<sizeof(c.origin));strcpy(c.origin,argv[1]);strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin;s.http.ca_file=argv[2];s.http.connect_ms=700;s.http.total_ms=3000;s.policy.client_protocol=4;s.policy.required_server_protocol=4;s.join_ms=2000;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&p)==GIN_ACCOUNT_PLAN_OK && gin_account_plan_topic_count(p)==12);
 R(gin_account_receive_new(&s,&c,p,&a)==GIN_CONNECT_OK);
 R(gin_account_dispatch_new(a,p,t,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(gin_root_acquire(argv[3],&root)==0);R(gin_account_sinks_new(&root,&c,p,"#&",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 R(gin_account_receive_start(a)==GIN_CONNECT_OK);
 for(n=0;n<200;n++) { R(gin_account_receive_service(a,10,123)==GIN_CONNECT_OK);if(gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_drained(a)) break; }
 R(n<200 && gin_account_receive_epoch(a)>0);
 R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_PLAIN,&output).code==GIN_CHECKPOINT_OK);lease(&root);note('R');
#ifdef BOOTSTRAP_BASELINE
 /* Real live-only component composition: no history API or fabricated frame. */
 while(!stop_tick(NULL,&n)) {
  struct gin_account_destination dest;struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
  R(gin_account_receive_service(a,10,123)==GIN_CONNECT_OK);
  while(gin_account_receive_peek(a)) {
   R(gin_account_dispatch_next(d,&dest,&record,&page)==GIN_SESSION_OK);
   if(record) R(gin_account_output_append(output,&dest,record,0).code==GIN_CHECKPOINT_OK);
   gin_render_record_free(record);gin_history_page_free(page);record=NULL;page=NULL;
  }
 }
#else
 {struct gin_account_history_bootstrap_input in={a,d,output,p,t,&s.http,&s.policy,&c,"#&",GIN_RENDER_MODE_PLAIN,&control};
  int invalid=getenv("BOOTSTRAP_NULL_ORIGIN")!=NULL;
  int stopping=getenv("BOOTSTRAP_STOP_APPEND")!=NULL;
  struct gin_account_history_bootstrap_result r;
  if(invalid) s.http.origin=NULL;
  if(stopping) local_stop=&control;
  r=gin_account_history_bootstrap(&in);
  if(invalid) {
   R(r.code==GIN_HISTORY_INVALID && r.incomplete && !r.appended && !r.pages && !r.destinations);
  } else if(stopping) {
   if(!r.stopped || appends_after_stop) fprintf(stderr,"STOP_PROBE code=%d stopped=%d incomplete=%d after_stop=%u appended=%zu pages=%zu\n",r.code,r.stopped,r.incomplete,appends_after_stop,r.appended,r.pages);
   R(stop_latched && !appends_after_stop && r.stopped && r.incomplete && r.code!=GIN_HISTORY_OK);
   R(r.appended==3 && r.pages==4 && r.drained==3 && postfetch_checks==4 && !postfetch_page);
  } else {
  if(r.code!=GIN_HISTORY_OK) fprintf(stderr,"bootstrap code=%d drained=%zu pages=%zu\n",r.code,r.drained,r.pages);
  R(r.code==GIN_HISTORY_OK && !r.incomplete && r.destinations==7 && r.drained==7 && r.pages==7 && r.appended==8);
  R(postfetch_checks==7 && !postfetch_page);
  }
 }
#endif
 (void)control;lease(&root);R(gin_account_receive_state(a)==GIN_CONNECT_READY);
 R(gin_account_output_close(&output)==0);lease(&root);gin_account_dispatch_free(d);
 gin_account_receive_close(a);R(!gin_account_receive_quarantined(a) && !gin_account_receive_cleanup_failed(a));
 R(gin_account_sinks_close(sinks)==0);R(gin_root_release(&root)==0);gin_account_receive_free(a);gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);gin_http_global_cleanup();
 puts("PASS borrowed handles retained; seven-destination fixture cleanup");return 0;
}
