/* Opt-in real owned receiver and explicit stable routing admission. */
#include "account_receive_hot.h"
#include "account_dispatch_hot.h"
#include "account_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef GIN_DISPATCH_VALGRIND
#include <valgrind/memcheck.h>
#endif
#define R(x) do { if (!(x)) { fprintf(stderr,"SETUP line %d: %s\n",__LINE__,#x); exit(2); } } while(0)
/* Compare only test-owned copies. Constructors preserve unused name tails and
 * padding; no production object or semantic read is made defined here. */
static int window_equal(const unsigned char *before, const struct gin_topology_window *current)
{ return !memcmp(before,current,sizeof(*current)); }
static void window_copy(struct gin_topology_window *copy, const struct gin_topology_window *source)
{
 memcpy(copy,source,sizeof(*copy));
#ifdef GIN_DISPATCH_VALGRIND
 VALGRIND_MAKE_MEM_DEFINED(copy,sizeof(*copy));
#endif
}
static void service(struct gin_account_receive *a)
{ R(gin_account_receive_service(a,20,123)==GIN_CONNECT_OK); }
static void waithead(struct gin_account_receive *a)
{ unsigned i;for(i=0;!gin_account_receive_peek(a);i++) { R(i<150);service(a); } }
static void exact(struct gin_account_destination d,struct gin_render_record *r,struct gin_history_page *p,const char *body,int64_t id)
{
 char expected[256];struct gin_phoenix_view v=gin_render_record_bytes(r);
 const struct gin_history_scope *s=gin_history_page_scope(p);
 snprintf(expected,sizeof(expected),"[1970-01-01T00:00:00.123Z] privmsg sender=\"sender\" body=\"%s\"\n",body);
 R(d.network_index==0 && d.window_index==1 && d.role==GIN_RENDER_QUERY && !d.terminal);
 R(v.length==strlen(expected) && !memcmp(v.data,expected,v.length));
 R(p && gin_history_page_source(p)==GIN_HISTORY_LIVE && gin_history_page_count(p)==1);
 R(gin_history_page_row(p,0)->id==id && !strcmp(gin_history_page_row(p,0)->dm_with,"Peer"));
 R(s && !strcmp(s->subject_id,"22345678-1234-1234-1234-123456789abc") && !strcmp(s->network_slug,"one") && s->network_id==1 && !strcmp(s->target,"Peer") && s->socket_epoch==1 && s->role==GIN_RENDER_QUERY && s->encryption==GIN_RENDER_MODE_PLAIN);
}
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old\",\"joined\":true,\"source\":\"joined\"}],\"two\":[]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
 struct gin_connect_settings s={0};struct gin_credential c={0};
 struct gin_topology *b=NULL,*t=NULL;struct gin_account_plan *p=NULL,*sorted=NULL;
 struct gin_account_receive *a=NULL;struct gin_account_dispatch *d=NULL;
 struct gin_account_registry *registry=NULL;struct gin_account_registry_scope scope;
 struct gin_account_registry_delta delta;struct gin_account_inventory *inventory=NULL;
 struct gin_account_destination dest={0};struct gin_render_record *record=NULL;struct gin_history_page *page=NULL;
 const struct gin_topology_window *old_window;const char *old_name;
 unsigned char window_bytes[sizeof(struct gin_topology_window)];
 struct gin_topology_window current_window,mutated_window;
 const struct gin_connect_topic_status *old[9];unsigned char old_bytes[9][sizeof(struct gin_connect_topic_status)];
 size_t i,index=999;unsigned n;enum gin_session_code code;int new_success=0;
 if(argc!=3 && !(argc==4 && !strcmp(argv[3],"--instrumented-setup"))) return 2;
 alarm(14);R(strlen(argv[1])<sizeof(c.origin));strcpy(c.origin,argv[1]);
 strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin;s.http.ca_file=argv[2];s.http.connect_ms=argc==4?1400:700;s.http.total_ms=1500;s.policy.client_protocol=4;s.policy.required_server_protocol=4;s.join_ms=2500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&p)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_plan_topic_count(p)==9 && gin_account_plan_window_count(p)==2);
 R(gin_account_receive_new_hot(&s,&c,p,"#&",&a)==GIN_CONNECT_OK);

 R(gin_account_receive_start(a)==GIN_CONNECT_OK);
 waithead(a);R(gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_epoch(a)==1);
 R(!strcmp(gin_account_receive_identity(a)->subject_name,"FreshAlice"));
 scope.origin=c.origin;scope.subject_uuid=c.subject_id;scope.topic_subject="FreshAlice";scope.chantypes="#&";scope.epoch=1;
 R(gin_account_registry_new(t,&scope,&registry)==GIN_ACCOUNT_REGISTRY_OK);
 R(gin_account_dispatch_new_hot(a,p,t,registry,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory)==GIN_SESSION_OK && record && page && !inventory);
 exact(dest,record,page,"initial-old",101);gin_render_record_free(record);gin_history_page_free(page);record=NULL;page=NULL;
 for(n=0;!gin_account_receive_drained(a);n++) { R(n<150);service(a);R(!gin_account_receive_count(a)); }
 old_window=gin_account_registry_window_at(registry,1);old_name=old_window->name.canonical_target;
 R(!strcmp(old_name,"peer"));memcpy(window_bytes,old_window,sizeof(window_bytes));
#ifdef GIN_DISPATCH_VALGRIND
 VALGRIND_MAKE_MEM_DEFINED(window_bytes,sizeof(window_bytes));
#endif
 window_copy(&mutated_window,old_window);
 mutated_window.network_id=old_window->network_id+1;
 R(!window_equal(window_bytes,&mutated_window));
 fprintf(stderr,"WINDOW_COMPARATOR_NAMED_FIELD_MUTATION_REJECTED connect_ms=%ld total_ms=1500 join_ms=2500 alarm=14\n",(long)s.http.connect_ms);
 for(i=0;i<9;i++) { char ref[21];old[i]=gin_account_receive_source(a,i);R(old[i]);snprintf(ref,sizeof(ref),"%zu",i+1);R(old[i]->state==GIN_CONNECT_TOPIC_JOINED && !strcmp(old[i]->join_ref,ref));memcpy(old_bytes[i],old[i],sizeof(old_bytes[i])); }
 puts("INITIAL_READY_DRAIN_EXACT_OLD");fflush(stdout);
 waithead(a);R(gin_account_receive_peek(a)->source_index==0 && gin_account_receive_peek(a)->observed_epoch==1 && !strcmp(gin_account_receive_peek(a)->source_join_ref,"1"));
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory)==GIN_SESSION_OK && inventory && !record && !page);
 { const struct gin_account_identity *id=gin_account_inventory_identity(inventory);
 R(!strcmp(id->origin,c.origin) && !strcmp(id->subject_id,c.subject_id) && !strcmp(id->subject_name,"FreshAlice") && gin_account_inventory_epoch(inventory)==1);
 scope.origin=id->origin;scope.subject_uuid=id->subject_id;scope.topic_subject=id->subject_name;scope.epoch=gin_account_inventory_epoch(inventory);

 R(gin_account_plan_new(gin_account_inventory_topology(inventory),"#&","FreshAlice",&sorted)==GIN_ACCOUNT_PLAN_OK);
 R(!strcmp(gin_account_plan_window_at(sorted,1)->name.canonical_target,"aaron") && !strcmp(gin_account_plan_window_at(sorted,2)->name.canonical_target,"peer")); }
 R(gin_account_dispatch_resolve_inventory(d,&inventory,0,&delta)==GIN_SESSION_OK && !inventory);
 scope=*gin_account_registry_scope(registry);
 R(delta.windows_added==1 && delta.sources_added==1 && delta.generation==1 && gin_account_registry_source_count(registry)==10);
 R(!strcmp(gin_account_registry_window_at(registry,2)->name.canonical_target,"aaron"));
 gin_account_inventory_free(inventory);inventory=NULL;gin_account_plan_free(sorted);sorted=NULL;
 window_copy(&current_window,old_window);
 R(gin_account_registry_window_at(registry,1)==old_window && old_window->name.canonical_target==old_name && window_equal(window_bytes,&current_window));
 R(gin_account_receive_subscribe_query_live(a,"one","Aaron",1,&index)==GIN_CONNECT_OK && index==9);
 waithead(a);R(gin_account_receive_count(a)==1 && gin_account_receive_state(a)==GIN_CONNECT_JOINING && gin_account_receive_source(a,9)->state==GIN_CONNECT_TOPIC_PENDING);
 R(gin_account_receive_peek(a)->source_index==6 && gin_account_receive_peek(a)->observed_epoch==1 && !strcmp(gin_account_receive_peek(a)->source_join_ref,"7"));
 R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory)==GIN_SESSION_OK && record && page && !inventory);
 exact(dest,record,page,"held-old",102);gin_render_record_free(record);gin_history_page_free(page);record=NULL;page=NULL;
 puts("OLD_RECEIVED_JOINING_NEW_ZERO");fflush(stdout);
 waithead(a);R(gin_account_receive_source(a,9)->state==GIN_CONNECT_TOPIC_JOINED && gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_epoch(a)==1);
 R(gin_account_receive_count(a)==1 && gin_account_receive_peek(a)->source_index==9 && gin_account_receive_peek(a)->observed_epoch==1);
 { const struct gin_live_view *v=gin_account_receive_peek(a);
 const char expected[]={"{\"kind\":\"message\",\"message\":{\"kind\":\"privmsg\",\"network\":\"one\",\"channel\":\"Aaron\",\"dm_with\":\"Aaron\",\"sender\":\"sender\",\"body\":\"new-aaron\",\"server_time\":123,\"id\":103}}"};
 R(!strcmp(v->fields.event,"event") && v->fields.payload.length==strlen(expected) && !memcmp(v->fields.payload.data,expected,strlen(expected)));
 fprintf(stderr,"RECEIVED_EXACT_NEW_WIRE source=9 epoch=1 id=103 body=new-aaron\n");
 }
 fprintf(stderr,"REGISTRY_APPLIED identity=FreshAlice epoch=1 generation=1 windows=3 sources=10 old_peer_index=1 appended_aaron_index=2 sorted_aaron_index=1; old_peer_slot_and_name_pointer_stable=1\n");
 R(!strcmp(gin_account_receive_peek(a)->fields.topic,"grappa:user:FreshAlice/network:one/channel:aaron") && !strcmp(gin_account_receive_peek(a)->source_join_ref,"10") && !strcmp(gin_account_receive_source(a,9)->join_ref,"10"));
 for(i=0;i<9;i++) R(old[i]==gin_account_receive_source(a,i) && !memcmp(old_bytes[i],old[i],sizeof(old_bytes[i])));
 dest.network_index=12345;dest.window_index=23456;dest.role=GIN_RENDER_STATUS;
 R(gin_account_dispatch_admit_window(d,&scope,1,2)==GIN_SESSION_OK);
 code=gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory);
 if(code==GIN_SESSION_OK && record && page) {
  struct gin_phoenix_view v=gin_render_record_bytes(record);const char expected[]="[1970-01-01T00:00:00.123Z] privmsg sender=\"sender\" body=\"new-aaron\"\n";
  const struct gin_history_scope *hs=gin_history_page_scope(page);
  R(hs && !strcmp(hs->origin,c.origin) && !strcmp(hs->subject_id,c.subject_id) && !strcmp(hs->network_slug,"one") && hs->network_id==1 && !strcmp(hs->chantypes,"#&") && !strcmp(hs->own_nick,"Own1") && hs->role==GIN_RENDER_QUERY && hs->encryption==GIN_RENDER_MODE_PLAIN && !strcmp(gin_history_page_row(page,0)->dm_with,"Aaron"));
  new_success=dest.network_index==0 && dest.window_index==2 && dest.role==GIN_RENDER_QUERY && !dest.terminal && v.length==strlen(expected) && !memcmp(v.data,expected,v.length) && gin_history_page_source(page)==GIN_HISTORY_LIVE && gin_history_page_count(page)==1 && gin_history_page_row(page,0)->id==103 && hs && !strcmp(hs->target,"Aaron") && hs->socket_epoch==1;
 }
 printf("NEW_JOINED epoch=1 source=9 ref=10 expected_destination=0/2 expected_role=QUERY expected_page=LIVE actual_code=%d route_code=%d record=%d page=%d destination=%zu/%zu\n",code,GIN_SESSION_ROUTE,!!record,!!page,dest.network_index,dest.window_index);fflush(stdout);
 if(code!=GIN_SESSION_OK) R(code==GIN_SESSION_ROUTE && !record && !page && !inventory && dest.network_index==12345 && dest.window_index==23456);
 gin_render_record_free(record);gin_history_page_free(page);gin_account_inventory_free(inventory);
 gin_account_dispatch_free(d);gin_account_receive_close(a);R(!gin_account_receive_cleanup_failed(a) && !gin_account_receive_quarantined(a));gin_account_receive_free(a);
 gin_account_registry_free(registry);gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);gin_credential_clear(&c);gin_http_global_cleanup();
 puts("CLEANUP_COMPLETE");fflush(stdout);
 if(!new_success) { fputs("SEMANTIC_RED: expected appended Aaron destination and typed LIVE page; actual GIN_SESSION_ROUTE\n",stderr);return 1; }
 return 0;
}
