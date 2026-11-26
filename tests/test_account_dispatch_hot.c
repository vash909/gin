/* Private getter facade: not authenticated transport qualification. */
#include "account_dispatch_hot.h"
#include "connect_values.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(x) do { if(!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);exit(1); } } while(0)
struct gin_account_receive {
 struct gin_account_identity id;
 uint64_t epoch;
 struct gin_connect_topic_status sources[64];
 char topics[64][GIN_WS_TOPIC_CAP];
 struct gin_live_view view;
 int queued,drops;
};
const struct gin_account_identity *gin_account_receive_identity(const struct gin_account_receive *r) { return r?&r->id:NULL; }
uint64_t gin_account_receive_epoch(const struct gin_account_receive *r) { return r?r->epoch:0; }
enum gin_connect_state gin_account_receive_state(const struct gin_account_receive *r) { return r?GIN_CONNECT_READY:GIN_CONNECT_IDLE; }
const struct gin_connect_topic_status *gin_account_receive_source(const struct gin_account_receive *r,size_t i) { return r && i<64 && r->sources[i].topic?&r->sources[i]:NULL; }
const struct gin_live_view *gin_account_receive_peek(const struct gin_account_receive *r) { return r && r->queued?&r->view:NULL; }
enum gin_live_code gin_account_receive_drop(struct gin_account_receive *r) { r->queued=0;r->drops++;return GIN_LIVE_OK; }
#include "../src/account_dispatch.c"
static void sources(struct gin_account_receive *r,const struct gin_account_registry *g)
{
 size_t i,n;
 for(i=0;i<gin_account_registry_source_count(g);i++) {
  const struct gin_connect_subscription *s=gin_account_registry_source_at(g,i);
  R(gin_ws_topic(r->id.subject_name,s->network,s->target,r->topics[i],sizeof(r->topics[i]),&n)==GIN_WS_VALUE_OK);
  r->sources[i].topic=r->topics[i];r->sources[i].join_ref="ref";r->sources[i].state=GIN_CONNECT_TOPIC_JOINED;
 }
}
static void queue(struct gin_account_receive *r,const char *payload,size_t source)
{
 memset(&r->view,0,sizeof(r->view));r->view.fields.topic=r->sources[source].topic;r->view.fields.event="event";
 r->view.fields.payload.data=(const unsigned char *)payload;r->view.fields.payload.length=strlen(payload);
 r->view.source_index=source;r->view.source_join_ref="ref";r->view.observed_epoch=r->epoch;r->queued=1;
}
int main(void)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own\"}],\"channels\":{\"one\":[]},\"heads\":{}}";
 const char same[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
 const char add[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Aaron\",\"opened_at\":\"x\"},{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
 const char message[]="{\"kind\":\"message\",\"message\":{\"kind\":\"privmsg\",\"network\":\"one\",\"channel\":\"Aaron\",\"dm_with\":\"Aaron\",\"sender\":\"sender\",\"body\":\"new\",\"server_time\":123,\"id\":103}}";
 struct gin_topology *b=NULL,*t=NULL;struct gin_account_plan *p=NULL;
 struct gin_account_registry *g=NULL,*other=NULL;
 struct gin_account_registry_scope s={"https://unit.invalid","22345678-1234-1234-1234-123456789abc","Fresh","#&",1},wrong;
 struct gin_account_receive r={0},r2={0};struct gin_account_dispatch *d=NULL,*legacy=NULL;
 struct gin_account_inventory *proposal=NULL,*held=NULL;struct gin_account_registry_delta delta={77,88,99};
 struct gin_account_destination dest={123,456,GIN_RENDER_STATUS,0};struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
 struct gin_account_dispatch_hot_result status;unsigned char bytes[sizeof(struct gin_account_dispatch)];const struct gin_topology *accepted;int drops;
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)same,strlen(same),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Fresh",&p)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_registry_new(t,&s,&g)==GIN_ACCOUNT_REGISTRY_OK);R(gin_account_registry_new(t,&s,&other)==GIN_ACCOUNT_REGISTRY_OK);
 r.id.origin=s.origin;r.id.subject_id=s.subject_uuid;r.id.subject_name=s.topic_subject;r.epoch=1;sources(&r,g);
 R(gin_account_dispatch_new_hot(&r,p,t,g,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(gin_account_dispatch_new(&r,p,t,"#&",GIN_RENDER_MODE_PLAIN,&legacy)==GIN_SESSION_OK);
 R(gin_account_dispatch_hot_status(legacy,&r,g,&s,0,0).code!=GIN_SESSION_OK);
 memcpy(bytes,d,sizeof(bytes));errno=EDOM;
 status=gin_account_dispatch_hot_status(d,&r,g,&s,0,0);R(status.code==GIN_SESSION_OK && status.window_admitted && !status.proposal_pending && errno==EDOM);
 R(gin_account_dispatch_hot_topology(d)==t && errno==EDOM && !memcmp(bytes,d,sizeof(bytes)));
 R(gin_account_dispatch_hot_status(d,&r2,g,&s,0,0).code!=GIN_SESSION_OK);
 R(gin_account_dispatch_hot_status(d,&r,other,&s,0,0).code!=GIN_SESSION_OK);
 R(gin_account_dispatch_hot_status(d,&r,g,&s,1,0).code!=GIN_SESSION_OK);
 wrong=s;wrong.topic_subject="Wrong";R(gin_account_dispatch_admit_window(d,&wrong,0,0)!=GIN_SESSION_OK);
 queue(&r,same,0);R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&proposal)==GIN_SESSION_OK && proposal);
 held=proposal;accepted=gin_account_dispatch_hot_topology(d);drops=r.drops;queue(&r,add,0);
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&held)==GIN_SESSION_INVALID);held=NULL;
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&held)==GIN_SESSION_ROUTE && !held && r.drops==drops && r.queued && dest.network_index==123 && dest.window_index==456 && !record && !page);
 R(gin_account_dispatch_hot_status(d,&r,g,&s,0,SIZE_MAX).proposal_pending && gin_account_dispatch_hot_topology(d)==accepted);
 R(gin_account_dispatch_resolve_inventory(d,&proposal,1,&delta)!=GIN_SESSION_OK && proposal && delta.generation==99);
 R(gin_account_dispatch_resolve_inventory(legacy,&proposal,0,&delta)!=GIN_SESSION_OK && proposal);
 R(gin_account_dispatch_resolve_inventory(d,&proposal,0,&delta)==GIN_SESSION_OK && !proposal && !delta.windows_added && !delta.generation);
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&proposal)==GIN_SESSION_OK && proposal);
 held=proposal;proposal=NULL;gin_account_dispatch_free(d);d=NULL;
 R(gin_account_inventory_topology(held));gin_account_inventory_free(held);held=NULL;
 R(gin_account_dispatch_new_hot(&r,p,t,g,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 queue(&r,add,0);R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&proposal)==GIN_SESSION_OK && proposal);
 R(gin_account_dispatch_resolve_inventory(d,&proposal,0,&delta)==GIN_SESSION_OK && !proposal && delta.generation==1);
 R(gin_account_dispatch_hot_status(d,&r,g,&s,1,1).window_admitted==0);
 R(gin_account_dispatch_admit_window(d,&s,1,1)!=GIN_SESSION_OK);sources(&r,g);
 R(gin_account_dispatch_admit_window(d,&s,0,1)!=GIN_SESSION_OK);
 r.sources[5].join_ref="";R(gin_account_dispatch_admit_window(d,&s,1,1)!=GIN_SESSION_OK);r.sources[5].join_ref="ref";
 queue(&r,message,5);R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&proposal)==GIN_SESSION_ROUTE && !record && !page && r.queued);
 R(!gin_account_dispatch_hot_topology(d));gin_account_dispatch_free(d);d=NULL;
 R(gin_account_dispatch_new_hot(&r,p,t,g,"#&",GIN_RENDER_MODE_PLAIN,&d)!=GIN_SESSION_OK && !d);
 gin_account_dispatch_free(legacy);gin_account_registry_free(g);gin_account_registry_free(other);gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);
 puts("private hot dispatcher units PASS");return 0;
}
