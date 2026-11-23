/* LIVE LOCAL FAKE characterization: real Native HTTPS/WSS, one owner. */
#include "connect_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define REQUIRE(x) do { if (!(x)) { fprintf(stderr,"check failed line %d\n",__LINE__); exit(1); } } while (0)
static unsigned markers;
static int tick(void *u,unsigned *wait) { (void)u; (void)wait; return 0; }
static void frame(void *u,const struct gin_phoenix_frame *f,uint64_t e) {
 const struct gin_phoenix_fields *v=gin_phoenix_frame_fields(f);
 (void)u; REQUIRE(e>0 && v); if(!strcmp(v->event,"tracer_marker")) markers++;
}
static void ready(struct gin_connect *s) {
 unsigned n=0;
 do { REQUIRE(gin_connect_service(s,50)==GIN_CONNECT_OK); REQUIRE(++n<100); }
 while(gin_connect_get_state(s)!=GIN_CONNECT_READY || !gin_connect_receive_drained(s));
}
static void marker(struct gin_connect *s,unsigned target) {
 unsigned n=0; while(markers<target) { REQUIRE(gin_connect_service(s,50)==GIN_CONNECT_OK); REQUIRE(++n<100); }
}
int main(int argc,char **argv) {
 struct gin_connect_settings s={0}; struct gin_credential c={0},saved;
 struct gin_connect *p=NULL,*same; struct gin_topology *t=NULL;
 struct gin_connect_subscription a[]={{"one","#old1"},{"two","#old2"}};
 struct gin_connect_subscription b[]={{"one","#new1"},{"two","#new2"}};
 struct gin_http_control ctl={tick,NULL,0}; struct gin_query_result q,old; uint64_t epoch; unsigned n;
 char rootref[21];
 REQUIRE(argc==3); alarm(15);
 REQUIRE(strlen(argv[1])<sizeof(c.origin)); strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc"); strcpy(c.subject_name,"Alice");
 strcpy(c.token,"fixture-token.DO-NOT-LOG"); saved=c;
 s.http.origin=c.origin; s.http.ca_file=argv[2]; s.http.connect_ms=700; s.http.total_ms=1500;
 s.policy.client_protocol=4; s.policy.required_server_protocol=4; s.join_ms=1500;
 REQUIRE(gin_http_global_init()==GIN_HTTP_OK);
 REQUIRE(gin_topology_fetch_controlled(&s.http,&s.policy,&c,"#&",&t,&ctl)==GIN_TOPOLOGY_OK);
 REQUIRE(gin_topology_network_count(t)==2 && gin_topology_window_count(t)==2);
 REQUIRE(gin_connect_new(&s,&c,a,2,frame,NULL,&p)==GIN_CONNECT_OK); same=p;
 REQUIRE(gin_connect_topic_count(p)==5);
 REQUIRE(gin_connect_start_controlled(p,&ctl)==GIN_CONNECT_OK); ready(p);
 epoch=gin_connect_last_result(p).socket_epoch;
 strcpy(rootref,gin_connect_topic_at(p,0)->join_ref);
 REQUIRE(gin_connect_open_query(p,t,"one","Peer","#&",GIN_QUERY_MODE_PLAIN,101,5000)==GIN_QUERY_OK);
 marker(p,1); REQUIRE(gin_connect_query_result(p).outcome==GIN_QUERY_PENDING);
 gin_connect_close(p); REQUIRE(gin_connect_get_state(p)==GIN_CONNECT_STOPPED);
 old=gin_connect_query_result(p); REQUIRE(old.outcome==GIN_QUERY_UNCERTAIN && old.operation_id==101);
 REQUIRE(gin_connect_replan_closed(p,b,2)==GIN_CONNECT_INVALID);
 REQUIRE(gin_connect_query_result(p).operation_id==101);
 REQUIRE(gin_connect_query_take(p,&q)==1 && q.outcome==GIN_QUERY_UNCERTAIN);
 REQUIRE(gin_connect_replan_closed(p,b,2)==GIN_CONNECT_OK && p==same);
 REQUIRE(gin_connect_topic_count(p)==5 && gin_connect_last_result(p).socket_epoch==epoch);
 REQUIRE(gin_connect_start_controlled(p,&ctl)==GIN_CONNECT_OK); ready(p);
 REQUIRE(gin_connect_last_result(p).socket_epoch>epoch);
 REQUIRE(strtoull(gin_connect_topic_at(p,0)->join_ref,NULL,10)>strtoull(rootref,NULL,10));
 REQUIRE(!strcmp(gin_connect_topic_at(p,2)->topic,"grappa:user:Alice/network:one/channel:#new1"));
 REQUIRE(!strcmp(gin_connect_topic_at(p,4)->topic,"grappa:user:Alice/network:two/channel:#new2"));
 REQUIRE(gin_connect_open_query(p,t,"two","Other","#&",GIN_QUERY_MODE_PLAIN,202,5000)==GIN_QUERY_OK);
 marker(p,2); q=gin_connect_query_result(p);
 REQUIRE(q.outcome==GIN_QUERY_PENDING && q.operation_id==202 && q.socket_epoch>old.socket_epoch);
 REQUIRE(strcmp(q.request_ref,old.request_ref) && strcmp(q.join_ref,old.join_ref));
 puts("PENDING202_AFTER_STALE_MARKER"); fflush(stdout);
 for(n=0;gin_connect_query_result(p).outcome==GIN_QUERY_PENDING;n++) {
  REQUIRE(n<100); REQUIRE(gin_connect_service(p,50)==GIN_CONNECT_OK);
 }
 REQUIRE(gin_connect_query_take(p,&q)==1 && q.outcome==GIN_QUERY_ACCEPTED && q.operation_id==202);
 gin_connect_close(p); REQUIRE(!gin_connect_cleanup_failed(p) && !gin_connect_quarantined(p));
 REQUIRE(!memcmp(&c,&saved,sizeof(c))); gin_connect_free(p); gin_topology_free(t); gin_http_global_cleanup();
 puts("PASS SAME_OBJECT UNCERTAIN_TAKE REPLAN STALE_REJECT ACCEPTED202"); return 0;
}
