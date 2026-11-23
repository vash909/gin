/* Fabricated READY state, real owned Native and append: NOT auth/TLS proof. */
#include "account_receive_hot.h"
#include "connect_hot.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define receive_frame native_receive_frame
#define discard native_discard
#include "../src/connect.c"
#undef receive_frame
#undef discard
static struct gin_connect *owned;
static unsigned submits;
static enum gin_connect_code hot_probe(struct gin_connect *s,const char *slug,
 const char *peer,const char *ct,uint64_t epoch,size_t *out)
{
 CHECK(s==owned && !strcmp(ct,"#&")); submits++;
 return gin_connect_subscribe_query_live(s,slug,peer,ct,epoch,out);
}
/* Private reconciliation fault controls, not fresh /me or TLS evidence. */
static int start_fault;
static enum gin_connect_code start_probe(struct gin_connect *s)
{
 size_t i,n;
 CHECK(s==owned && start_fault);
 for(i=0;i<s->count;i++) {
  struct topic *t=&s->topics[i];
  CHECK(gin_ws_topic("RetryName",t->has_network?t->network:NULL,
   t->has_target?t->target:NULL,t->text,sizeof(t->text),&n)==GIN_WS_VALUE_OK);
 }
 if(start_fault==2) s->topics[1].text[0]='X';
 s->state=start_fault==3?GIN_CONNECT_JOINING:GIN_CONNECT_FAILED;
 return start_fault==3?GIN_CONNECT_OK:GIN_CONNECT_FORBIDDEN;
}
#define gin_connect_start start_probe
#define gin_connect_subscribe_query_live hot_probe
#include "../src/account_receive.c"
#undef gin_connect_subscribe_query_live
#undef gin_connect_start
static void ready(struct gin_account_receive *a)
{
 size_t i; a->native->state=GIN_CONNECT_READY;a->native->result.socket_epoch=7;
 for(i=0;i<a->native->count;i++) {
  struct topic *t=&a->native->topics[i];
  snprintf(t->ref,sizeof(t->ref),"%zu",i+1);
  t->status.join_ref=t->ref;t->status.state=GIN_CONNECT_TOPIC_JOINED;
 }
 a->native->refs.current=a->native->count;
}
int main(void)
{
 struct gin_connect_settings s={{"https://example.invalid",NULL,100,200},{4,4},1000};
 struct gin_credential c={"https://example.invalid","fixture-token","12345678-1234-1234-1234-123456789abc","Alice"};
 struct gin_topology *b=NULL,*t=NULL;struct gin_account_plan *p=NULL;
 struct gin_account_receive *a=NULL,*legacy=NULL,*occupied;
 char ct[]="#&";size_t out=999,bytes,i;unsigned calls;
 unsigned char saved[sizeof(struct gin_account_receive)];
 struct gin_live_view v={0};const struct gin_live_view *head;
 const struct gin_connect_topic_status *old;
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"Net\",\"nick\":\"Own\"}],\"channels\":{\"Net\":[]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{}}";
 (void)hot_probe;
 CHECK(gin_http_global_init()==GIN_HTTP_OK);
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),ct,&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,ct,"Alice",&p)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_receive_new(&s,&c,p,&legacy)==GIN_CONNECT_OK);
 CHECK(gin_account_receive_new_hot(&s,&c,p,ct,&a)==GIN_CONNECT_OK);owned=a->native;
 occupied=a;CHECK(gin_account_receive_new_hot(&s,&c,p,ct,&occupied)==GIN_CONNECT_INVALID && occupied==a);
 CHECK(gin_account_receive_new_hot(&s,&c,p,NULL,&occupied)==GIN_CONNECT_INVALID);
 gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);memset(ct,'x',2);memset(&c,0,sizeof(c));
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_name,"Alice"));
 CHECK(gin_account_receive_subscribe_query_live(NULL,"Net","Peer",7,&out)==GIN_CONNECT_INVALID);
 CHECK(gin_account_receive_subscribe_query_live(legacy,"Net","Peer",7,&out)==GIN_CONNECT_INVALID && !submits);
 v.fields.topic="grappa:user:Alice";v.fields.event="old";v.fields.payload.data=(const unsigned char *)"{}";v.fields.payload.length=2;
 v.source_join_ref="1";v.observed_epoch=7;v.kind=GIN_LIVE_UNSUPPORTED;v.source_index=63;
 CHECK(gin_live_queue_push(legacy->queue,&v)==GIN_LIVE_INVALID);
 CHECK(gin_live_queue_push(a->queue,&v)==GIN_LIVE_OK);CHECK(gin_live_queue_drop(a->queue)==GIN_LIVE_OK);
 v.source_index=0;CHECK(gin_live_queue_push(a->queue,&v)==GIN_LIVE_OK);
 ready(a);head=gin_account_receive_peek(a);bytes=gin_live_queue_bytes(a->queue);old=gin_account_receive_source(a,3);
 CHECK(head && old && a->topics==4);
 memcpy(saved,a,sizeof(*a));
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","Peer",8,&out)==GIN_CONNECT_SCOPE && out==999);
 CHECK(!memcmp(saved,a,sizeof(*a)) && !submits);
 CHECK(gin_account_receive_subscribe_query_live(a,NULL,"Peer",7,&out)==GIN_CONNECT_INVALID && !submits);
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","#channel",7,&out)==GIN_CONNECT_INVALID && !submits);
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","B.ob",7,&out)==GIN_CONNECT_INVALID && !submits);
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","Peer",7,NULL)==GIN_CONNECT_INVALID && !submits);
 a->failure=GIN_CONNECT_LIMIT;
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","Peer",7,&out)==GIN_CONNECT_LIMIT && !submits);a->failure=GIN_CONNECT_OK;
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","oWN",7,&out)==GIN_CONNECT_OK && out==3 && a->topics==4);
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","Peer",7,&out)==GIN_CONNECT_OK && out==4 && a->topics==5);
 CHECK(consistent(a) && gin_account_receive_state(a)==GIN_CONNECT_JOINING);
 CHECK(gin_account_receive_source(a,4)->state==GIN_CONNECT_TOPIC_PENDING);
 CHECK(gin_account_receive_source(a,3)==old && !strcmp(old->join_ref,"4"));
 CHECK(gin_account_receive_peek(a)==head && gin_live_queue_bytes(a->queue)==bytes && gin_account_receive_count(a)==1);
 CHECK(!strcmp(head->fields.event,"old") && !memcmp(head->fields.payload.data,"{}",2));
 calls=submits;out=999;CHECK(gin_account_receive_subscribe_query_live(a,"Net","Another",7,&out)==GIN_CONNECT_INVALID && out==999);
 CHECK(submits==calls+1);CHECK(gin_account_receive_drop(a)==GIN_LIVE_OK);
 gin_phoenix_bytes_free(&a->native->outbound);ready(a);a->native->clock_bad=1;
 i=a->topics;out=999;CHECK(gin_account_receive_subscribe_query_live(a,"Net","Another",7,&out)==GIN_CONNECT_CLOCK);
 CHECK(a->topics==i && out==999 && consistent(a));
 /* Private full-table and refresh characterization; no fresh /me claim. */
 a->native->clock_bad=0;ready(a);
 while(a->topics<GIN_CONNECT_TOPICS_MAX) {
  char peer[32];snprintf(peer,sizeof(peer),"Peer%zu",a->topics);
  CHECK(gin_account_receive_subscribe_query_live(a,"Net",peer,7,&out)==GIN_CONNECT_OK);
  gin_phoenix_bytes_free(&a->native->outbound);ready(a);
 }
 calls=submits;out=999;
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","OWN",7,&out)==GIN_CONNECT_OK && out==3);
 CHECK(submits==calls+1);out=999;memcpy(saved,a,sizeof(*a));
 CHECK(gin_account_receive_subscribe_query_live(a,"Net","Overflow",7,&out)==GIN_CONNECT_LIMIT && out==999);
 CHECK(!memcmp(saved,a,sizeof(*a)) && submits==calls+1);
 for(i=0;i<a->topics;i++) {
  struct topic *nt=&a->native->topics[i];size_t n;
  CHECK(gin_ws_topic("Fresh",nt->has_network?nt->network:NULL,nt->has_target?nt->target:NULL,
   nt->text,sizeof(nt->text),&n)==GIN_WS_VALUE_OK);
 }
 CHECK(refresh_hot(a)==GIN_CONNECT_OK && consistent(a));
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_name,"Fresh"));
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_id,"12345678-1234-1234-1234-123456789abc"));
 memcpy(saved,a,sizeof(*a));a->native->topics[4].text[0]='X';
 CHECK(refresh_hot(a)==GIN_CONNECT_SCOPE && !memcmp(saved,a,sizeof(*a)));
 a->native->topics[4].text[0]='g';CHECK(consistent(a));
 CHECK(gin_live_queue_push(a->queue,&v)==GIN_LIVE_OK);head=gin_account_receive_peek(a);
 CHECK(gin_account_receive_service(a,0,0)==GIN_CONNECT_INVALID && gin_account_receive_peek(a)==head);
 gin_account_receive_close(a);CHECK(!gin_live_queue_count(a->queue) && a->topics==64);
 /* Post-fix characterization: queue discard, verified owned reconstruction,
  * primary error precedence, sticky mismatch, unchanged legacy rename policy. */
 CHECK(gin_live_queue_push(a->queue,&v)==GIN_LIVE_OK);start_fault=1;
 CHECK(gin_account_receive_start(a)==GIN_CONNECT_FORBIDDEN);
 CHECK(!gin_live_queue_count(a->queue) && !a->failure && consistent(a) && a->topics==64);
 CHECK(!strcmp(gin_account_receive_identity(a)->subject_name,"RetryName"));
 start_fault=2;
 CHECK(gin_account_receive_start(a)==GIN_CONNECT_FORBIDDEN && a->failure==GIN_CONNECT_SCOPE);
 CHECK(!strcmp(a->expected[1],"grappa:user:RetryName/network:Net"));
 gin_account_receive_close(a);
 CHECK(gin_account_receive_start(a)==GIN_CONNECT_SCOPE);
 owned=legacy->native;start_fault=3;
 CHECK(gin_account_receive_start(legacy)==GIN_CONNECT_OK);
 CHECK(!strcmp(gin_account_receive_identity(legacy)->subject_name,"Alice"));
 CHECK(gin_account_receive_service(legacy,0,0)==GIN_CONNECT_SCOPE);
 gin_account_receive_close(legacy);CHECK(gin_account_receive_start(legacy)==GIN_CONNECT_SCOPE);
 gin_account_receive_free(a);gin_account_receive_free(legacy);gin_http_global_cleanup();
 puts("PASS private owned hot constructor/capacity/append/queue/local rejection/operational failure; no TLS proof");return 0;
}
