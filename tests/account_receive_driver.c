#include "account_receive.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void budget_current(struct gin_account_receive *a)
{
 unsigned wait=50;
 R(gin_account_receive_state(a)==GIN_CONNECT_JOINING || gin_account_receive_state(a)==GIN_CONNECT_READY);
 R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_OK && wait<=50);
}
static const char *topics[]={"grappa:user:Alice","grappa:user:Alice/network:one","grappa:user:Alice/network:two","grappa:user:Alice/network:one/channel:$server","grappa:user:Alice/network:two/channel:$server","grappa:user:Alice/network:one/channel:#old1","grappa:user:Alice/network:two/channel:#old2","grappa:user:Alice/network:one/channel:own1","grappa:user:Alice/network:two/channel:own2"};
int main(int argc,char **argv) {
 struct gin_connect_settings s={0}; struct gin_credential c={0},saved;
 struct gin_topology *b=NULL,*t=NULL; struct gin_account_plan *plan=NULL;
 struct gin_account_receive *a=NULL,*same; unsigned n,round; size_t i; uint64_t epoch; const char *mode=getenv("GIN_RECEIVE_CASE");
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{}}";
 R(argc==3); alarm(15); R(strlen(argv[1])<sizeof(c.origin)); strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");saved=c;
 s.http.origin=c.origin;s.http.ca_file=argv[2];s.http.connect_ms=700;s.http.total_ms=1500;s.policy.client_protocol=4;s.policy.required_server_protocol=4;s.join_ms=1500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 { unsigned wait=37;
 R(gin_account_receive_http_wait_budget(NULL,&wait)==GIN_CONNECT_INVALID && wait==37);
 R(gin_account_receive_http_wait_budget(NULL,NULL)==GIN_CONNECT_INVALID);
 R(!gin_account_receive_cleanup_failed(NULL) && !gin_account_receive_quarantined(NULL)); }
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&plan)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_plan_topic_count(plan)==9);
 R(gin_account_receive_new(&s,&c,plan,&a)==GIN_CONNECT_OK);same=a;
 { unsigned wait=37;
 R(gin_account_receive_http_wait_budget(a,NULL)==GIN_CONNECT_INVALID);
 R(gin_account_receive_state(a)==GIN_CONNECT_IDLE);
 R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_INVALID && wait==37);
 R(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a)); }
 R(gin_account_receive_new(&s,&c,plan,&a)==GIN_CONNECT_INVALID && a==same);
 gin_account_plan_free(plan);gin_topology_free(t);gin_topology_free(b);
 if(mode && !strcmp(mode,"wrong_uuid")) {
  R(gin_account_receive_start(a)!=GIN_CONNECT_OK);R(!gin_account_receive_peek(a));
  gin_account_receive_free(a);gin_http_global_cleanup();puts("PASS wrong_uuid refused before WSS");return 0;
 }
 for(round=0;round<2;round++) {
 R(gin_account_receive_start(a)==GIN_CONNECT_OK);
 budget_current(a);
 if(mode && (!strcmp(mode,"overflow") || !strcmp(mode,"bytes"))) {
  enum gin_connect_code result=GIN_CONNECT_OK;
  for(n=0;result==GIN_CONNECT_OK;n++) { R(n<100);result=gin_account_receive_service(a,50,123456789); }
  R(result==GIN_CONNECT_LIMIT);R(!gin_account_receive_peek(a));
  { unsigned wait=37; R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_LIMIT && wait==37); }
  R(gin_account_receive_service(a,0,0)==GIN_CONNECT_LIMIT);
  gin_account_receive_close(a);R(gin_account_receive_start(a)==GIN_CONNECT_LIMIT);
  { unsigned wait=37; R(!gin_account_receive_epoch(a)); R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_LIMIT && wait==37);
    R(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a)); }
  gin_account_receive_free(a);gin_http_global_cleanup();puts(!strcmp(mode,"bytes")?"PASS sticky real byte overflow":"PASS sticky real count overflow");return 0;
 }
 for(n=0;!gin_account_receive_peek(a) || gin_account_receive_count(a)<9;n++) {
  R(n<100);R(gin_account_receive_service(a,50,123456789)==GIN_CONNECT_OK);
 }
 R(gin_account_receive_count(a)==9);epoch=round+1;
 budget_current(a);
 { const struct gin_live_view *v=gin_account_receive_peek(a); struct gin_live_view saved_view=*v; unsigned wait=50;
 R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_OK && wait==0);
 R(gin_account_receive_count(a)==9 && gin_account_receive_peek(a)==v && !memcmp(v,&saved_view,sizeof(saved_view))); }
 for(i=0;i<9;i++) {
  const struct gin_live_view *v=gin_account_receive_peek(a);char ref[21],payload[40];
  R(v && v->source_index==i && v->observed_epoch==epoch && v->receive_ms==123456789);
  R(!strcmp(v->fields.topic,topics[i]) && !strcmp(v->fields.event,"admitted"));
  snprintf(ref,sizeof(ref),"%lu",(unsigned long)(i+1+round*9));R(!strcmp(v->source_join_ref,ref));
  R((i%2==0 && !v->fields.join_ref) || (i%2==1 && !strcmp(v->fields.join_ref,ref)));
  R(v->fields.ref && !strcmp(v->fields.ref,"wire"));
  snprintf(payload,sizeof(payload),"{\"index\":%lu}",(unsigned long)i);
  R(v->fields.payload.length==strlen(payload) && !memcmp(v->fields.payload.data,payload,strlen(payload)));
  if(round || i<8) R(gin_account_receive_drop(a)==GIN_LIVE_OK);
 }
 R(gin_account_receive_count(a)==(round?0:1));gin_account_receive_close(a);R(!gin_account_receive_peek(a));
 { unsigned wait=37; R(!gin_account_receive_epoch(a)); R(gin_account_receive_http_wait_budget(a,&wait)==GIN_CONNECT_CLOSED && wait==37);
 R(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a)); }
 }
 R(!memcmp(&saved,&c,sizeof(c)));gin_account_receive_free(a);gin_http_global_cleanup();puts("PASS actual9 exact_sources owned_frames wrong_stale_prejoin_rejected");return 0;
}
