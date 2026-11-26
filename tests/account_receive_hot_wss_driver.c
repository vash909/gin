/* Owned-facade loopback characterization; no routing or plaintext authority. */
#include "account_receive_hot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static const char *const topics[]={
 "grappa:user:FreshAlice", "grappa:user:FreshAlice/network:one",
 "grappa:user:FreshAlice/network:two",
 "grappa:user:FreshAlice/network:one/channel:$server",
 "grappa:user:FreshAlice/network:two/channel:$server",
 "grappa:user:FreshAlice/network:one/channel:#old",
 "grappa:user:FreshAlice/network:one/channel:peer",
 "grappa:user:FreshAlice/network:one/channel:own1",
 "grappa:user:FreshAlice/network:two/channel:own2",
 "grappa:user:FreshAlice/network:one/channel:newpeer"};
static size_t sources(struct gin_account_receive *a)
{ size_t i=0;while(gin_account_receive_source(a,i)) { CHECK(i<64);i++; }return i; }
static void service(struct gin_account_receive *a)
{ CHECK(gin_account_receive_service(a,25,1700000123456LL)==GIN_CONNECT_OK); }
static void record(const struct gin_live_view *v,size_t index,const char *body,int null_join)
{
 char ref[21];snprintf(ref,sizeof(ref),"%zu",index+1);
 CHECK(v && v->source_index==index && v->observed_epoch==1 && v->receive_ms==1700000123456LL);
 CHECK(v->kind==GIN_LIVE_UNSUPPORTED && !strcmp(v->source_join_ref,ref));
 CHECK(!strcmp(v->fields.topic,topics[index]) && !strcmp(v->fields.event,"hot_marker"));
 CHECK(v->fields.ref==NULL);
 if(null_join) CHECK(v->fields.join_ref==NULL);
 else CHECK(v->fields.join_ref && !strcmp(v->fields.join_ref,ref));
 CHECK(v->fields.payload.length==strlen(body) && !memcmp(v->fields.payload.data,body,strlen(body)));
}
int main(int argc,char **argv)
{
 struct gin_connect_settings s={0};struct gin_credential c={0};
 struct gin_topology *b=NULL,*t=NULL;struct gin_account_plan *p=NULL;
 struct gin_account_receive *a=NULL;
 const struct gin_connect_topic_status *old[9];
 unsigned char saved[9][sizeof(struct gin_connect_topic_status)];
 char refs[9][21];size_t i,index=999,initial;unsigned n;int positive,recovery;
 const struct gin_live_view *head;unsigned char headbytes[sizeof(struct gin_live_view)];
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old\",\"joined\":true,\"source\":\"joined\"}],\"two\":[]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
 char ct[]="#&";
 CHECK((argc==4 || (argc==5 && !strcmp(argv[4],"--instrumented-setup"))) && (!strcmp(argv[3],"positive") || !strcmp(argv[3],"reject") || !strcmp(argv[3],"timeout") || !strcmp(argv[3],"recovery") || !strcmp(argv[3],"recovery_close") || !strcmp(argv[3],"preflight")));alarm(14);
 recovery=!strcmp(argv[3],"recovery") || !strcmp(argv[3],"recovery_close") || !strcmp(argv[3],"preflight");
 positive=!strcmp(argv[3],"positive");
 CHECK(strlen(argv[1])<sizeof(c.origin));strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin;s.http.ca_file=argv[2];s.http.connect_ms=700;s.http.total_ms=1500;
 /* Valgrind's first TLS setup exceeded 700 ms before any HTTP request.
  * A connect-only 1400 ms probe passed with total_ms still 1500. Keep
  * HTTP, JOIN/ACK negatives and independent parent/server bounds unchanged. */
 if(argc==5) s.http.connect_ms=1400;
 s.policy.client_protocol=4;s.policy.required_server_protocol=4;s.join_ms=!strcmp(argv[3],"timeout")?500:2500;
 CHECK(gin_http_global_init()==GIN_HTTP_OK);
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),ct,&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,ct,"Alice",&p)==GIN_ACCOUNT_PLAN_OK);
 initial=gin_account_plan_topic_count(p);CHECK(initial==9);
 CHECK(gin_account_receive_new_hot(&s,&c,p,ct,&a)==GIN_CONNECT_OK);
 CHECK(sources(a)==initial && !strcmp(gin_account_receive_identity(a)->subject_name,"Alice"));
 gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);
 CHECK(!strcmp(c.subject_name,"Alice") && !strcmp(c.token,"fixture-token.DO-NOT-LOG"));
 gin_credential_clear(&c);memset(&s,0,sizeof(s));memset(ct,'x',2);
 if(recovery) {
  enum gin_connect_code first=gin_account_receive_start(a),second;
  CHECK(first==(!strcmp(argv[3],"preflight")?GIN_CONNECT_PREFLIGHT:GIN_CONNECT_FORBIDDEN));
  CHECK(gin_account_receive_state(a)==GIN_CONNECT_FAILED && !gin_account_receive_count(a));
  CHECK(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a));
  if(!strcmp(argv[3],"recovery_close")) gin_account_receive_close(a);
  second=gin_account_receive_start(a);
  fprintf(stderr,"START_RECOVERY first=%d second=%d\n",(int)first,(int)second);
  CHECK(second==GIN_CONNECT_OK);
  CHECK(!strcmp(gin_account_receive_identity(a)->subject_name,"FreshAlice"));
  CHECK(!strcmp(gin_account_receive_identity(a)->subject_id,"22345678-1234-1234-1234-123456789abc"));
  for(n=0;gin_account_receive_state(a)!=GIN_CONNECT_READY;n++) { CHECK(n<200);service(a); }
  CHECK(gin_account_receive_epoch(a)==2 && sources(a)==initial && !gin_account_receive_count(a));
  for(i=0;i<initial;i++) {
   const struct gin_connect_topic_status *topic=gin_account_receive_source(a,i);
   snprintf(refs[i],sizeof(refs[i]),"%zu",i+1);
   CHECK(topic && !strcmp(topic->topic,topics[i]) && topic->state==GIN_CONNECT_TOPIC_JOINED && !strcmp(topic->join_ref,refs[i]));
  }
  gin_account_receive_close(a);
  CHECK(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a));
  gin_account_receive_free(a);gin_http_global_cleanup();puts("PASS OWNED START RECOVERY");return 0;
 }
 CHECK(gin_account_receive_start(a)==GIN_CONNECT_OK);
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_name,"FreshAlice"));
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_id,"22345678-1234-1234-1234-123456789abc"));
 for(n=0;gin_account_receive_state(a)!=GIN_CONNECT_READY || !gin_account_receive_peek(a);n++) { CHECK(n<200);service(a); }
 CHECK(gin_account_receive_epoch(a)==1 && gin_account_receive_count(a)==1);
 head=gin_account_receive_peek(a);record(head,5,"{\"stage\":\"queued\"}",0);
 memcpy(headbytes,head,sizeof(headbytes));
 for(i=0;i<initial;i++) {
  old[i]=gin_account_receive_source(a,i);CHECK(old[i] && !strcmp(old[i]->topic,topics[i]));
  CHECK(old[i]->state==GIN_CONNECT_TOPIC_JOINED);snprintf(refs[i],sizeof(refs[i]),"%zu",i+1);
  CHECK(!strcmp(old[i]->join_ref,refs[i]));memcpy(saved[i],old[i],sizeof(saved[i]));
 }
 CHECK(gin_account_receive_subscribe_query_live(a,"one","NewPeer",1,&index)==GIN_CONNECT_OK && index==initial);
 CHECK(sources(a)==initial+1 && gin_account_receive_source(a,index)->state==GIN_CONNECT_TOPIC_PENDING);
 CHECK(!strcmp(gin_account_receive_source(a,index)->topic,topics[9]) && !strcmp(gin_account_receive_source(a,index)->join_ref,"10"));
 CHECK(gin_account_receive_peek(a)==head && gin_account_receive_count(a)==1 && !memcmp(headbytes,head,sizeof(headbytes)));
 record(head,5,"{\"stage\":\"queued\"}",0);
 for(i=0;i<initial;i++) CHECK(old[i]==gin_account_receive_source(a,i) && !memcmp(saved[i],old[i],sizeof(saved[i])) && !strcmp(old[i]->join_ref,refs[i]) && !strcmp(old[i]->topic,topics[i]));
 CHECK(gin_account_receive_epoch(a)==1);CHECK(gin_account_receive_drop(a)==GIN_LIVE_OK);
 for(n=0;!gin_account_receive_peek(a);n++) { CHECK(n<100);service(a); }
 CHECK(gin_account_receive_count(a)==1);record(gin_account_receive_peek(a),5,"{\"stage\":\"held\"}",0);
 CHECK(gin_account_receive_state(a)==GIN_CONNECT_JOINING && gin_account_receive_source(a,9)->state==GIN_CONNECT_TOPIC_PENDING);
 CHECK(gin_account_receive_drop(a)==GIN_LIVE_OK);
 puts("OLD_RECEIVED_JOINING_NEW_ZERO");fflush(stdout);
 if(!positive) {
  enum gin_connect_code rc=GIN_CONNECT_OK;
  for(n=0;rc==GIN_CONNECT_OK;n++) { CHECK(n<200);rc=gin_account_receive_service(a,25,1700000123456LL); }
  CHECK(rc==(!strcmp(argv[3],"reject")?GIN_CONNECT_JOIN_REJECTED:GIN_CONNECT_TIMEOUT));
  CHECK(gin_account_receive_state(a)==GIN_CONNECT_FAILED && !gin_account_receive_peek(a) && !gin_account_receive_count(a));
 } else {
  unsigned seen=0;
  for(n=0;seen<2;n++) {
   const struct gin_live_view *v;CHECK(n<200);service(a);
   while((v=gin_account_receive_peek(a))!=NULL) {
    CHECK(seen<2);record(v,seen==0?9:5,seen==0?"{\"stage\":\"post\"}":"{\"stage\":\"continued\"}",seen==0);
    CHECK(gin_account_receive_source(a,v->source_index)->state==GIN_CONNECT_TOPIC_JOINED);
    CHECK(!strcmp(v->source_join_ref,gin_account_receive_source(a,v->source_index)->join_ref));
    seen++;CHECK(gin_account_receive_drop(a)==GIN_LIVE_OK);
   }
  }
  CHECK(gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_epoch(a)==1);
  for(i=0;i<initial;i++) CHECK(old[i]==gin_account_receive_source(a,i) && !memcmp(saved[i],old[i],sizeof(saved[i])) && !strcmp(old[i]->join_ref,refs[i]) && !strcmp(old[i]->topic,topics[i]));
  CHECK(gin_account_receive_subscribe_query_live(a,"one","NEWPEER",1,&index)==GIN_CONNECT_OK && index==9);
  CHECK(gin_account_receive_subscribe_query_live(a,"one","oWN1",1,&index)==GIN_CONNECT_OK && index==7);
  CHECK(sources(a)==10 && gin_account_receive_epoch(a)==1);
 }
 gin_account_receive_close(a);CHECK(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a));
 gin_account_receive_free(a);gin_http_global_cleanup();puts("PASS OWNED HOT SAME_EPOCH");return 0;
}
