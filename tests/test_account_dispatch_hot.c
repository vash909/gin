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

/* Post-fix characterization/sensitivity, not retroactive initial TDD.
 * This private receiver facade exercises real parsers/projectors, NOT WSS. */
struct admission_fixture {
 struct gin_topology *boot,*topology;
 struct gin_account_plan *plan;
 struct gin_account_registry *registry;
 struct gin_account_receive receiver;
 struct gin_account_dispatch *dispatch;
};
static const struct gin_account_registry_scope admission_scope={
 "https://unit.invalid","22345678-1234-1234-1234-123456789abc","Fresh","#&",1};
static const char admission_boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own\"}],\"channels\":{\"one\":[]},\"heads\":{}}";
static const char admission_old[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
static const char admission_add[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Aaron\",\"opened_at\":\"x\"},{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
static void admission_open(struct admission_fixture *f,int hot)
{
 struct gin_account_inventory *proposal=NULL;struct gin_render_record *record=NULL;
 struct gin_history_page *page=NULL;struct gin_account_destination dest={0};
 struct gin_account_registry_delta delta={0};size_t i;
 memset(f,0,sizeof(*f));
 R(gin_topology_parse_boot((const unsigned char *)admission_boot,strlen(admission_boot),"#&",&f->boot)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(f->boot,(const unsigned char *)admission_old,strlen(admission_old),&f->topology)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(f->topology,"#&","Fresh",&f->plan)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_registry_new(f->topology,&admission_scope,&f->registry)==GIN_ACCOUNT_REGISTRY_OK);
 f->receiver.id.origin=admission_scope.origin;f->receiver.id.subject_id=admission_scope.subject_uuid;
 f->receiver.id.subject_name=admission_scope.topic_subject;f->receiver.epoch=1;sources(&f->receiver,f->registry);
 if(!hot) { R(gin_account_dispatch_new(&f->receiver,f->plan,f->topology,"#&",GIN_RENDER_MODE_PLAIN,&f->dispatch)==GIN_SESSION_OK);return; }
 R(gin_account_dispatch_new_hot(&f->receiver,f->plan,f->topology,f->registry,"#&",GIN_RENDER_MODE_PLAIN,&f->dispatch)==GIN_SESSION_OK);
 queue(&f->receiver,admission_add,0);
 R(gin_account_dispatch_next_inventory_typed(f->dispatch,&dest,&record,&page,&proposal)==GIN_SESSION_OK && proposal && !record && !page);
 R(gin_account_dispatch_resolve_inventory(f->dispatch,&proposal,0,&delta)==GIN_SESSION_OK && !proposal && delta.generation==1);
 sources(&f->receiver,f->registry);
 for(i=0;i<gin_account_registry_window_count(f->registry);i++)
  R(gin_account_dispatch_admit_window(f->dispatch,&admission_scope,1,i)==GIN_SESSION_OK);
}
static void admission_close(struct admission_fixture *f)
{
 gin_account_dispatch_free(f->dispatch);gin_account_registry_free(f->registry);
 gin_account_plan_free(f->plan);gin_topology_free(f->topology);gin_topology_free(f->boot);
}
static size_t admission_source(struct admission_fixture *f,const char *target)
{
 size_t i;
 for(i=0;i<gin_account_registry_source_count(f->registry);i++) {
  const struct gin_connect_subscription *s=gin_account_registry_source_at(f->registry,i);
  if(s->target && !strcmp(s->target,target)) return i;
 }
 R(0);return 0;
}
static void admission_payload(char *out,size_t cap,const char *target,const char *kind,const char *id)
{
 int n=snprintf(out,cap,"{\"kind\":\"message\",\"message\":{\"kind\":\"%s\",\"network\":\"one\",\"channel\":\"%s\",%s\"sender\":\"sender\",\"body\":\"unit\",\"server_time\":123%s}}",
 kind,target,strcmp(target,"$server")?(!strcmp(target,"Aaron")?"\"dm_with\":\"Aaron\",":"\"dm_with\":\"Peer\","):"",id);
 R(n>0 && (size_t)n<cap);
}
static int admission_dest_unchanged(const struct gin_account_destination *d)
{ return d->network_index==123 && d->window_index==456 && d->role==GIN_RENDER_CHANNEL && d->terminal==7; }
static enum gin_session_code admission_next(int mode,struct gin_account_dispatch *d,struct gin_account_destination *dest,
 struct gin_render_record **record,struct gin_history_page **page,struct gin_account_inventory **proposal)
{
 if(mode==0) return gin_account_dispatch_next_inventory_typed(d,dest,record,page,proposal);
 if(mode==1) return gin_account_dispatch_next(d,dest,record,page);
 return gin_account_dispatch_next_inventory(d,dest,record,page,proposal);
}
static void admission_table(void)
{
 static const char *targets[]={"Aaron","Peer","$server"};
 static const char *kinds[]={"privmsg","notice","action","server_event"};
 static const char *ids[]={",\"id\":103","",",\"id\":0",",\"id\":-1",",\"id\":\"103\""};
 size_t route,k,id;int mode,variant;unsigned rows=0,failures=0,controls=0;
 for(mode=0;mode<3;mode++) for(route=0;route<3;route++)
  for(variant=0;variant<4;variant++) for(k=0;k<(variant==0?(route==2?4u:3u):1u);k++)
   for(id=0;id<(variant==0?(mode==0?5u:2u):1u);id++) {
    struct admission_fixture f;struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
    struct gin_account_inventory *proposal=NULL;struct gin_account_destination dest={123,456,GIN_RENDER_CHANNEL,7};
    enum gin_session_code code;char payload[512];int drops;int bad=mode==0 && (variant==1 || (variant==0 && id!=0));
    admission_open(&f,1);
    if(variant==3) strcpy(payload,"{\"kind\":\"own_nick_changed\",\"network_id\":1,\"nick\":\"Own\"}");
    else if(variant==2) strcpy(payload,"{\"kind\":\"harmless_metadata\",\"extra\":1}");
    else admission_payload(payload,sizeof(payload),targets[route],variant==1?"unknown_plaintext":kinds[k],variant==1?ids[0]:ids[id]);
    queue(&f.receiver,payload,variant==3?0:admission_source(&f,targets[route]));drops=f.receiver.drops;
    printf("admission mode=%d route=%s kind=%s id=%zu variant=%d\n",mode,targets[route],kinds[k],id,variant);fflush(stdout);
    code=admission_next(mode,f.dispatch,&dest,&record,&page,&proposal);rows++;
    if(bad) {
     R(code!=GIN_SESSION_OK && code!=GIN_SESSION_INVALID && f.receiver.queued && f.receiver.drops==drops);
     R(!record && !page && !proposal && admission_dest_unchanged(&dest));
     R(!gin_account_dispatch_hot_topology(f.dispatch));
     R(gin_account_dispatch_hot_status(f.dispatch,&f.receiver,f.registry,&admission_scope,1,SIZE_MAX).code==code);
     R(admission_next(mode,f.dispatch,&dest,&record,&page,&proposal)==code && f.receiver.queued && f.receiver.drops==drops);
     R(!record && !page && !proposal && admission_dest_unchanged(&dest));failures++;
    } else {
     R(code==GIN_SESSION_OK && !f.receiver.queued && f.receiver.drops==drops+1 && !proposal);
     if(variant==0) {
      const struct gin_topology_window *w;
      R(record && (id==0?page!=NULL:page==NULL) && dest.network_index==0 && !dest.terminal);
      R(dest.role==(route==2?GIN_RENDER_STATUS:GIN_RENDER_QUERY));
      if(route==2) R(dest.window_index==SIZE_MAX);
      else { w=gin_account_registry_window_at(f.registry,dest.window_index);R(w && !strcmp(w->name.original_wire_target,targets[route])); }
      if(page) {
       const struct gin_history_row *row=gin_history_page_row(page,0);
       const struct gin_history_scope *scope=gin_history_page_scope(page);
       R(gin_history_page_count(page)==1 && row && row->id==103 && !strcmp(row->kind,kinds[k]));
       R(scope && scope->role==dest.role && !strcmp(scope->target,targets[route]) && !strcmp(row->channel,targets[route]));
       R(route==2?!row->dm_with:(row->dm_with && !strcmp(row->dm_with,targets[route])));
      }
     } else { R(!record && !page && admission_dest_unchanged(&dest));controls++; }
     R(gin_account_dispatch_hot_topology(f.dispatch));
    }
    gin_render_record_free(record);gin_history_page_free(page);admission_close(&f);
   }
 printf("admission table PASS rows=%u typed_failures=%u harmless_or_legacy_ignore=%u\n",rows,failures,controls);
}
static void admission_invalid(void)
{
 unsigned which;
 for(which=0;which<9;which++) {
  struct admission_fixture f;struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
  struct gin_account_inventory *proposal=NULL;struct gin_account_destination dest={123,456,GIN_RENDER_CHANNEL,7};
  struct gin_render_record *held_record;struct gin_history_page *held_page;struct gin_account_inventory *held_proposal;
  struct gin_account_registry_delta delta={0};char payload[512];int drops;
  admission_open(&f,which!=1);
  admission_payload(payload,sizeof(payload),"Peer","privmsg",",\"id\":103");
  if(which==6 || which==7) {
   queue(&f.receiver,payload,admission_source(&f,"Peer"));
   R(admission_next(0,f.dispatch,&dest,&record,&page,&proposal)==GIN_SESSION_OK && record && page);
   if(which==6) {gin_history_page_free(page);page=NULL;} else {gin_render_record_free(record);record=NULL;}
  }
  if(which==8) {queue(&f.receiver,admission_add,0);R(admission_next(0,f.dispatch,&dest,&record,&page,&proposal)==GIN_SESSION_OK && proposal);}
  dest.network_index=123;dest.window_index=456;dest.role=GIN_RENDER_CHANNEL;dest.terminal=7;
  held_record=record;held_page=page;held_proposal=proposal;
  queue(&f.receiver,payload,admission_source(&f,"Peer"));drops=f.receiver.drops;
  R(gin_account_dispatch_next_inventory_typed(which==0?NULL:f.dispatch,which==2?NULL:&dest,
    which==3?NULL:&record,which==4?NULL:&page,which==5?NULL:&proposal)==GIN_SESSION_INVALID);
  R(f.receiver.queued && f.receiver.drops==drops && record==held_record && page==held_page && proposal==held_proposal && admission_dest_unchanged(&dest));
  R(f.dispatch->failure==GIN_SESSION_OK);
  gin_render_record_free(record);record=NULL;gin_history_page_free(page);page=NULL;
  if(proposal) R(gin_account_dispatch_resolve_inventory(f.dispatch,&proposal,1,&delta)==GIN_SESSION_OK && !proposal);
  R(admission_next(which==1?1:0,f.dispatch,&dest,&record,&page,&proposal)==GIN_SESSION_OK && record && page && !f.receiver.queued);
  gin_render_record_free(record);gin_history_page_free(page);admission_close(&f);
 }
 puts("admission invalid arguments PASS cases=9");
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
 admission_table();admission_invalid();
 puts("private hot dispatcher units PASS");return 0;
}
