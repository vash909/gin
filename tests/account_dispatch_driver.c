#include "account_dispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}]}}";
 struct gin_connect_settings s={0}; struct gin_credential c={0};
 struct gin_topology *b=NULL,*t=NULL; struct gin_account_plan *p=NULL;
 struct gin_account_receive *a=NULL; struct gin_account_dispatch *d=NULL;
 struct gin_account_destination dest; struct gin_render_record *record=NULL;
 struct gin_history_page *page=NULL; unsigned i,n=0; const char *mode=getenv("GIN_DISPATCH_CASE");
 const size_t nets[]={0,1,0,1,0,0}; const size_t wins[]={SIZE_MAX,SIZE_MAX,0,2,1,1};
 const char *kinds[]={"server_event","notice","privmsg","privmsg","privmsg","privmsg"};
 struct gin_account_inventory *inventory=NULL,*held=NULL;
 const struct gin_topology_window *old_window=NULL;
 unsigned char old_bytes[sizeof(struct gin_topology_window)];
 uint64_t observed=0; unsigned proposals=0;
 int inventory_mode=mode && (!strcmp(mode,"inventory_add") || !strcmp(mode,"inventory_same") || !strcmp(mode,"inventory_wrong_event") || !strcmp(mode,"inventory_wrong_source") || !strcmp(mode,"inventory_malformed") || !strcmp(mode,"inventory_unknown_network") || !strcmp(mode,"inventory_cap") || !strcmp(mode,"inventory_encrypted"));
 int inventory_good=inventory_mode && (!strcmp(mode,"inventory_add") || !strcmp(mode,"inventory_same") || !strcmp(mode,"inventory_wrong_event"));
 int life=0;
 int barrier=mode && (!strcmp(mode,"wrong_event_query") || !strcmp(mode,"wrong_event_nick") || !strcmp(mode,"query_same") || !strcmp(mode,"nick_same") || !strcmp(mode,"life_unknown") || !strcmp(mode,"diagnostic") || !strcmp(mode,"life_joined") || !strcmp(mode,"life_kicked"));
 R(!mode || inventory_mode || !strcmp(mode,"positive") || barrier || !strcmp(mode,"query_changed") || !strcmp(mode,"nick_changed") || !strcmp(mode,"life_offscope") || !strcmp(mode,"life_malformed") || !strcmp(mode,"life_encrypted") || !strcmp(mode,"wrong_source") || !strcmp(mode,"wrong_network") || !strcmp(mode,"unknown_dm") || !strcmp(mode,"encrypted") || !strcmp(mode,"channel_dm") || !strcmp(mode,"fidelity_kind") || !strcmp(mode,"fidelity_peer") || !strcmp(mode,"fidelity_id"));
 R(argc==3); alarm(15); strcpy(c.origin,argv[1]); strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc"); strcpy(c.subject_name,"Alice"); strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin; s.http.ca_file=argv[2]; s.http.connect_ms=700; s.http.total_ms=1500; s.policy.client_protocol=4; s.policy.required_server_protocol=4; s.join_ms=1500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&p)==GIN_ACCOUNT_PLAN_OK);
 old_window=gin_account_plan_window_at(p,1);memcpy(old_bytes,old_window,sizeof(old_bytes));
 R(!gin_account_inventory_topology(NULL) && !gin_account_inventory_identity(NULL) && !gin_account_inventory_epoch(NULL));
 gin_account_inventory_free(NULL);
 R(gin_account_receive_new(&s,&c,p,&a)==GIN_CONNECT_OK);
 R(gin_account_dispatch_new(a,p,t,"#&",GIN_RENDER_MODE_UNKNOWN,&d)==GIN_SESSION_UNSUPPORTED && !d);
 R(gin_account_dispatch_new(a,p,t,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(!gin_account_receive_epoch(a)); R(!gin_account_receive_drained(a));
 R(gin_account_receive_start(a)==GIN_CONNECT_OK);
 for(i=0;i<150;i++) {
  R(gin_account_receive_service(a,20,123)==GIN_CONNECT_OK);
  while(gin_account_receive_peek(a)) {
   const struct gin_live_view *before=gin_account_receive_peek(a);
   enum gin_session_code code;
   dest.network_index=12345;dest.window_index=23456;dest.role=GIN_RENDER_STATUS;
   if(inventory_mode) {
    size_t queued=gin_account_receive_count(a);
    R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,NULL)==GIN_SESSION_INVALID);
    R(gin_account_dispatch_next_inventory(NULL,&dest,&record,&page,&inventory)==GIN_SESSION_INVALID);
    R(gin_account_dispatch_next_inventory(d,NULL,&record,&page,&inventory)==GIN_SESSION_INVALID);
    R(gin_account_dispatch_next_inventory(d,&dest,NULL,&page,&inventory)==GIN_SESSION_INVALID);
    R(gin_account_dispatch_next_inventory(d,&dest,&record,NULL,&inventory)==GIN_SESSION_INVALID);
    if(held) { inventory=held;
     R(gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory)==GIN_SESSION_INVALID && inventory==held);
     inventory=NULL;
    }
    R(gin_account_receive_peek(a)==before && gin_account_receive_count(a)==queued);
    R(dest.network_index==12345 && dest.window_index==23456 && !record && !page);
    code=gin_account_dispatch_next_inventory(d,&dest,&record,&page,&inventory);
    if(inventory) {
     const struct gin_account_identity *identity=gin_account_inventory_identity(inventory);
     const struct gin_topology *proposal=gin_account_inventory_topology(inventory);
     R(code==GIN_SESSION_OK && !record && !page && !held);
     R(dest.network_index==12345 && dest.window_index==23456);
     R(!strcmp(identity->origin,c.origin) && !strcmp(identity->subject_id,c.subject_id) && !strcmp(identity->subject_name,"Alice"));
     observed=gin_account_receive_epoch(a);R(observed && gin_account_inventory_epoch(inventory)==observed);
     R(gin_topology_find_window(proposal,1,"peer"));
     R(!!gin_topology_find_window(proposal,1,"newpeer")==!strcmp(mode,"inventory_add"));
     R(gin_account_plan_window_count(p)==3 && gin_account_plan_window_at(p,1)==old_window && !memcmp(old_bytes,old_window,sizeof(old_bytes)));
     R(!gin_topology_find_window(t,1,"newpeer"));
     held=inventory;inventory=NULL;proposals++;continue;
    }
   } else code=gin_account_dispatch_next(d,&dest,&record,&page);
   if(barrier && (!strcmp(mode,"life_joined") || !strcmp(mode,"life_kicked")) && !life) {
    struct gin_phoenix_view bytes;
    const char *expected=!strcmp(mode,"life_kicked") ? "[1970-01-01T00:00:00.123Z recv] kicked target=\"#old1\" state=\"kicked\"\n" : "[1970-01-01T00:00:00.123Z recv] joined target=\"#old1\" state=\"joined\"\n";
    R(code==GIN_SESSION_OK && record && !page);
    R(dest.network_index==0 && dest.window_index==0 && dest.role==GIN_RENDER_CHANNEL);
    bytes=gin_render_record_bytes(record);R(bytes.length==strlen(expected) && !memcmp(bytes.data,expected,bytes.length));

    R(dest.terminal==!strcmp(mode,"life_kicked"));

    gin_render_record_free(record);record=NULL;life=1;
    if(!strcmp(mode,"life_kicked")) {
     dest.network_index=12345;dest.window_index=23456;
     R(gin_account_dispatch_next(d,&dest,&record,&page)==GIN_SESSION_ROUTE && !record && !page);
     R(dest.network_index==12345 && dest.window_index==23456);goto done;
    }
    continue;
   }
   if(mode && strcmp(mode,"positive") && !barrier && !inventory_good) {
    if(code!=((!strcmp(mode,"encrypted") || !strcmp(mode,"life_encrypted") || !strcmp(mode,"inventory_encrypted"))?GIN_SESSION_UNSUPPORTED:GIN_SESSION_ROUTE) || record || page)
     fprintf(stderr,"observed code=%d record=%d history=%d destination=%zu/%zu role=%d\n",code,!!record,!!page,dest.network_index,dest.window_index,dest.role);
    R(code==((!strcmp(mode,"encrypted") || !strcmp(mode,"life_encrypted") || !strcmp(mode,"inventory_encrypted"))?GIN_SESSION_UNSUPPORTED:GIN_SESSION_ROUTE) && !record && !page);
    R(!inventory);
    R(dest.network_index==12345 && dest.window_index==23456 && dest.role==GIN_RENDER_STATUS);
    R(gin_account_receive_peek(a)==before);
    R(gin_account_dispatch_next(d,&dest,&record,&page)==code && !record && !page);
    goto done;
   }
   R(code==GIN_SESSION_OK);
   R(gin_account_plan_window_count(p)==3 && gin_account_plan_window_at(p,1)==old_window && !memcmp(old_bytes,old_window,sizeof(old_bytes)));

   if(record) R(!dest.terminal);

   if(record) {
    char expected[256]; struct gin_phoenix_view bytes=gin_render_record_bytes(record);
    R(n<6 && dest.network_index==nets[n] && dest.window_index==wins[n]);
    R(dest.role==(n<2?GIN_RENDER_STATUS:n<4?GIN_RENDER_CHANNEL:GIN_RENDER_QUERY));
    snprintf(expected,sizeof(expected),"[1970-01-01T00:00:00.123Z] %s sender=\"sender\" body=\"hello\"\n",kinds[n]);
    R(bytes.length==strlen(expected) && !memcmp(bytes.data,expected,bytes.length));
    R(page && gin_history_page_row(page,0)->id==99);
    R(!strcmp(gin_history_page_scope(page)->subject_id,c.subject_id)); n++;
    gin_render_record_free(record);gin_history_page_free(page);record=NULL;page=NULL;
   } else R(!page);
  }
  if(n==6 && gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_drained(a)) break;
 }
 R(n==6 && i<150); if(inventory_mode) R(proposals==(!strcmp(mode,"inventory_wrong_event")?0u:1u));
 if(!mode || !strcmp(mode,"positive")) puts("PASS exact6 destinations bytes IDs local-drain");
done:
 gin_account_dispatch_free(d);gin_account_receive_free(a);gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);
 if(held) {
  R(gin_account_inventory_epoch(held)==observed);
  R(!strcmp(gin_account_inventory_identity(held)->subject_name,"Alice"));
  R(!strcmp(gin_account_inventory_identity(held)->subject_id,"22345678-1234-1234-1234-123456789abc"));
  R(!strcmp(gin_account_inventory_identity(held)->origin,argv[1]));
  R(gin_topology_find_window(gin_account_inventory_topology(held),1,"peer"));
  R(!!gin_topology_find_window(gin_account_inventory_topology(held),1,"newpeer")==!strcmp(mode,"inventory_add"));
 }
 gin_account_inventory_free(held);gin_http_global_cleanup();return 0;
}
