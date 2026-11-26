/* Real strict loopback HTTPS/WSS tracer; no account facade. */
#include "connect_hot.h"
#include "connect_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check line %d\n",__LINE__); exit(1); } } while (0)
static struct gin_connect *owner;
static unsigned old_seen,new_seen;
static void frame(void *u,const struct gin_phoenix_frame *f,uint64_t epoch)
{
 const struct gin_phoenix_fields *v=gin_phoenix_frame_fields(f);(void)u;
 CHECK(epoch==1 && v);
 if(strcmp(v->event,"hot_marker"))return;
 if(!strcmp(v->topic,"grappa:user:FreshLabel/network:one/channel:#old1")) {
  CHECK(v->join_ref && !strcmp(v->join_ref,"3"));
  CHECK(v->payload.length==12 && !memcmp(v->payload.data,"{\"old\":true}",12)); old_seen++;
 } else {
  CHECK(!strcmp(v->topic,"grappa:user:FreshLabel/network:one/channel:newpeer"));
  CHECK(v->join_ref && !strcmp(v->join_ref,"7"));
  CHECK(gin_connect_topic_at(owner,6)->state==GIN_CONNECT_TOPIC_JOINED);
  CHECK(v->payload.length==12 && !memcmp(v->payload.data,"{\"new\":true}",12)); new_seen++;
 }
}
static void ready(struct gin_connect *p)
{ unsigned n=0;while(gin_connect_get_state(p)!=GIN_CONNECT_READY || !gin_connect_receive_drained(p)) { CHECK(++n<120);CHECK(gin_connect_service(p,25)==GIN_CONNECT_OK); } }
int main(int argc,char **argv)
{
 struct gin_connect_settings s={0};struct gin_credential c={0};
 struct gin_connect_subscription plan[]={{"one","#old1"},{"two","#old2"},{"one","Own1"}};
 const struct gin_connect_topic_status *views[6];
 unsigned char saved[6][sizeof(struct gin_connect_topic_status)];
 char topics[6][2049],refs[6][21];size_t i,index=999;unsigned n;
 CHECK(argc==4 && (!strcmp(argv[3],"positive") || !strcmp(argv[3],"reject") || !strcmp(argv[3],"timeout")));alarm(12);
 CHECK(strlen(argv[1])<sizeof(c.origin));strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"StaleLabel");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin;s.http.ca_file=argv[2];s.http.connect_ms=700;s.http.total_ms=1500;
 s.policy.client_protocol=4;s.policy.required_server_protocol=4;s.join_ms=!strcmp(argv[3],"timeout")?300:2500;
 CHECK(gin_http_global_init()==GIN_HTTP_OK);
 CHECK(gin_connect_new(&s,&c,plan,3,frame,NULL,&owner)==GIN_CONNECT_OK);
 CHECK(gin_connect_start(owner)==GIN_CONNECT_OK);ready(owner);
 CHECK(gin_connect_last_result(owner).socket_epoch==1 && gin_connect_topic_count(owner)==6);
 for(i=0;i<6;i++) {
  views[i]=gin_connect_topic_at(owner,i);memcpy(saved[i],views[i],sizeof(saved[i]));
  CHECK(strlen(views[i]->topic)<sizeof(topics[i]));strcpy(topics[i],views[i]->topic);strcpy(refs[i],views[i]->join_ref);
 }
 CHECK(gin_connect_subscribe_query_live(owner,"one","NewPeer","#&",1,&index)==GIN_CONNECT_OK && index==6);
 CHECK(gin_connect_get_state(owner)==GIN_CONNECT_JOINING && !gin_connect_receive_drained(owner));
 for(n=0;!old_seen;n++){CHECK(n<100);CHECK(gin_connect_service(owner,25)==GIN_CONNECT_OK);}
 CHECK(old_seen==1 && new_seen==0 && gin_connect_get_state(owner)==GIN_CONNECT_JOINING);
 puts("OLD_RECEIVED_JOINING_NEW_ZERO");fflush(stdout);
 if(strcmp(argv[3],"positive")) {
  enum gin_connect_code rc=GIN_CONNECT_OK;
  for(n=0;rc==GIN_CONNECT_OK;n++){CHECK(n<200);rc=gin_connect_service(owner,25);}
  CHECK(rc==(!strcmp(argv[3],"reject")?GIN_CONNECT_JOIN_REJECTED:GIN_CONNECT_TIMEOUT));
  CHECK(gin_connect_get_state(owner)==GIN_CONNECT_FAILED && new_seen==0);
 } else {
  for(n=0;new_seen<1 || old_seen<2;n++){CHECK(n<200);CHECK(gin_connect_service(owner,25)==GIN_CONNECT_OK);}
  ready(owner);CHECK(new_seen==1 && old_seen==2);
  for(i=0;i<6;i++)CHECK(views[i]==gin_connect_topic_at(owner,i) && !memcmp(saved[i],views[i],sizeof(saved[i])) && !strcmp(topics[i],views[i]->topic) && !strcmp(refs[i],views[i]->join_ref));
  CHECK(!strcmp(gin_connect_topic_at(owner,6)->topic,"grappa:user:FreshLabel/network:one/channel:newpeer"));
  CHECK(!strcmp(gin_connect_topic_at(owner,6)->join_ref,"7"));
  CHECK(gin_connect_subscribe_query_live(owner,"one","NEWPEER","#&",1,&index)==GIN_CONNECT_OK && index==6);
  CHECK(gin_connect_subscribe_query_live(owner,"one","oWN1","#&",1,&index)==GIN_CONNECT_OK && index==5);
  CHECK(gin_connect_topic_count(owner)==7 && gin_connect_last_result(owner).socket_epoch==1);
 }
 CHECK(!strcmp(c.subject_name,"StaleLabel"));gin_connect_close(owner);
 CHECK(!gin_connect_cleanup_failed(owner) && !gin_connect_quarantined(owner));
 gin_connect_free(owner);gin_http_global_cleanup();puts("PASS HOT SAME_EPOCH");return 0;
}
