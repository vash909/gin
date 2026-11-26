#include "account_registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static const char boot[] = "{\"networks\":[{\"id\":4294967297,\"slug\":\"Net\",\"nick\":\"Own1\"}],\"channels\":{\"Net\":[{\"name\":\"#room\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
static const char peer[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"Peer\",\"opened_at\":\"old\"}]}}";
static struct gin_account_registry_scope scope = {"https://example.invalid","12345678-1234-1234-1234-123456789abc","TopicCase","#",7};
static struct gin_topology *parse(const char *b)
{
 struct gin_topology *t = NULL;
 CHECK(gin_topology_parse_boot((const unsigned char *)b,strlen(b),"#",&t)==GIN_TOPOLOGY_OK); return t;
}
static struct gin_topology *queries(struct gin_topology *b,const char *q)
{
 struct gin_topology *t = NULL;
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)q,strlen(q),&t)==GIN_TOPOLOGY_OK); return t;
}
static const char added[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"pEER\",\"opened_at\":\"refresh\"},{\"network_id\":4294967297,\"target_nick\":\"NewPeer\",\"opened_at\":\"new\"}]}}";
static const char own[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":4294967297,\"target_nick\":\"NewPeer\",\"opened_at\":\"x\"},{\"network_id\":4294967297,\"target_nick\":\"oWN1\",\"opened_at\":\"x\"}]}}";
static void updates(void)
{
 struct gin_topology *b=parse(boot), *t=queries(b,peer), *a=queries(b,added), *o=queries(b,own);
 struct gin_account_plan *cold=NULL;
 struct gin_account_registry *r=NULL;
 struct gin_account_registry_delta d;
 const struct gin_topology_window *w;
 const struct gin_topology_network *net;
 const struct gin_connect_subscription *src;
 unsigned char wb[sizeof(*w)], nb[sizeof(*net)], sb[sizeof(*src)];
 const char *stamp, *slug, *target;
 size_t i;
 CHECK(gin_account_registry_new(t,&scope,&r)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_account_plan_new(a,"#","TopicCase",&cold)==GIN_ACCOUNT_PLAN_OK);
 CHECK(!strcmp(gin_account_plan_window_at(cold,1)->name.canonical_target,"newpeer"));
 gin_account_plan_free(cold);
 w=gin_account_registry_window_at(r,1); net=gin_account_registry_network_at(r,0); src=gin_account_registry_source_at(r,4);
 memcpy(wb,w,sizeof(*w)); memcpy(nb,net,sizeof(*net)); memcpy(sb,src,sizeof(*src));
 stamp=w->opened_at; slug=net->slug; target=src->target;
 gin_topology_free(t);
 CHECK(gin_account_registry_apply(r,a,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(d.windows_added==1 && d.sources_added==1 && d.generation==1);
 CHECK(gin_account_registry_window_at(r,1)==w && !memcmp(wb,w,sizeof(*w)));
 CHECK(gin_account_registry_network_at(r,0)==net && !memcmp(nb,net,sizeof(*net)));
 CHECK(gin_account_registry_source_at(r,4)==src && !memcmp(sb,src,sizeof(*src)));
 CHECK(w->opened_at==stamp && net->slug==slug && src->target==target);
 CHECK(!strcmp(stamp,"old") && !strcmp(target,"Peer"));
 CHECK(!strcmp(gin_account_registry_window_at(r,2)->name.original_wire_target,"NewPeer"));
 CHECK(!strcmp(gin_account_registry_source_at(r,6)->target,"NewPeer"));
 for(i=0;i<3;i++) {
  CHECK(gin_account_registry_apply(r,a,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK);
  CHECK(!d.windows_added && !d.sources_added && d.generation==1);
 }
 CHECK(gin_account_registry_apply(r,o,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(d.windows_added==1 && !d.sources_added && d.generation==2);
 CHECK(gin_account_registry_source_count(r)==7 && gin_account_registry_window_count(r)==4);
 gin_topology_free(a); gin_topology_free(o); gin_topology_free(b);
 CHECK(!strcmp(gin_account_registry_window_at(r,3)->name.canonical_target,"own1"));
 CHECK(!strcmp(gin_account_registry_source_at(r,5)->target,"Own1"));
 CHECK(!memcmp(wb,w,sizeof(*w)) && !strcmp(stamp,"old"));
 gin_account_registry_free(r);
}
/* Supplemental guard characterization after the API-first link REDs. */
static void rejection(struct gin_account_registry *r,struct gin_topology *t,
 const struct gin_account_registry_scope *s,enum gin_account_registry_code code)
{
 struct gin_account_registry_delta d; unsigned char bytes[sizeof(d)];
 const struct gin_topology_window *w=gin_account_registry_window_at(r,1);
 unsigned char wb[sizeof(*w)];
 size_t wc=gin_account_registry_window_count(r),sc=gin_account_registry_source_count(r);
 uint64_t g=gin_account_registry_generation(r);
 memcpy(wb,w,sizeof(*w)); memset(&d,0xa5,sizeof(d)); memcpy(bytes,&d,sizeof(d));
 CHECK(gin_account_registry_apply(r,t,s,&d)==code);
 CHECK(!memcmp(bytes,&d,sizeof(d)) && !memcmp(wb,w,sizeof(*w)));
 CHECK(gin_account_registry_window_count(r)==wc && gin_account_registry_source_count(r)==sc);
 CHECK(gin_account_registry_generation(r)==g && !strcmp(w->opened_at,"old"));
}
static void guards(void)
{
 struct gin_topology *b=parse(boot), *t=queries(b,peer), *a=queries(b,added), *x;
 struct gin_account_registry *r=NULL,*occupied;
 struct gin_account_registry_scope changed=scope;
 const char *empty="{\"kind\":\"query_windows_list\",\"windows\":{}}";
 const char *bad[]={"$server",":Peer","bad.name","a,b","a!b","a@b","#other","+peer","bad peer"};
 char payload[20000],modified[sizeof(boot)+64]; size_t i,n; unsigned count,j;
 CHECK(gin_account_registry_new(b,&scope,&r)==GIN_ACCOUNT_REGISTRY_UNKNOWN && !r);
 CHECK(gin_account_registry_new(t,&scope,&r)==GIN_ACCOUNT_REGISTRY_OK); occupied=r;
 CHECK(gin_account_registry_new(t,&scope,&r)==GIN_ACCOUNT_REGISTRY_INVALID && r==occupied);
 CHECK(!gin_account_registry_window_at(r,99) && !gin_account_registry_source_at(r,99));
 changed.epoch++; rejection(r,a,&changed,GIN_ACCOUNT_REGISTRY_SCOPE); changed=scope;
 changed.origin="https://other.invalid"; rejection(r,a,&changed,GIN_ACCOUNT_REGISTRY_SCOPE); changed=scope;
 changed.subject_uuid="different-uuid"; rejection(r,a,&changed,GIN_ACCOUNT_REGISTRY_SCOPE); changed=scope;
 changed.topic_subject="OtherSubject"; rejection(r,a,&changed,GIN_ACCOUNT_REGISTRY_SCOPE); changed=scope;
 changed.chantypes="#&"; rejection(r,a,&changed,GIN_ACCOUNT_REGISTRY_SCOPE);
 rejection(r,b,&scope,GIN_ACCOUNT_REGISTRY_UNKNOWN); rejection(r,NULL,&scope,GIN_ACCOUNT_REGISTRY_INVALID);
 x=queries(b,empty); rejection(r,x,&scope,GIN_ACCOUNT_REGISTRY_UNSUPPORTED); gin_topology_free(x);
 for(i=0;i<5;i++) {
  char *at; struct gin_topology *base;
  strcpy(modified,boot);
  if(i==0) strcpy(modified,"{\"networks\":[{\"id\":4294967297,\"slug\":\"Net\",\"nick\":\"Own1\"}],\"channels\":{\"Net\":[]},\"heads\":{}}");
  if(i==1) { at=strstr(modified,"#room"); CHECK(at); memcpy(at,"#else",5); }
  if(i==2) { at=strstr(modified,"Own1"); CHECK(at); memcpy(at,"Own2",4); }
  if(i==3) { at=strstr(modified,"4294967297"); CHECK(at); memcpy(at,"4294967298",10); }
  if(i==4) { at=strstr(modified,"Net"); CHECK(at); memcpy(at,"NET",3); at=strstr(at+3,"Net"); CHECK(at); memcpy(at,"NET",3); }
  base=parse(modified); x=queries(base,empty); rejection(r,x,&scope,GIN_ACCOUNT_REGISTRY_UNSUPPORTED);
  gin_topology_free(x); gin_topology_free(base);
 }
 /* Generic topology names are legal upstream; registry QUERYs require the
  * narrower closed /query grammar, not filesystem role inference. */
 for(i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
  (void)snprintf(payload,sizeof(payload),"{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"%s\",\"opened_at\":\"x\"}]}}",bad[i]);
  x=NULL;
  { enum gin_topology_code pc=gin_topology_apply_queries(b,(const unsigned char *)payload,strlen(payload),&x);
    if(!strcmp(bad[i],"bad peer")) {
      CHECK(pc==GIN_TOPOLOGY_MALFORMED && !x);
      fprintf(stderr,"parser-boundary rejection: bad peer\n");
    } else { CHECK(pc==GIN_TOPOLOGY_OK);
      rejection(r,x,&scope,GIN_ACCOUNT_REGISTRY_INVALID); gin_topology_free(x); }
  }
 }
 /* Unknown networks and duplicate canonical names are parser rejections. */
 { const char *invalid[]={"{\"kind\":\"query_windows_list\",\"windows\":{\"9\":[]}}",
  "{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":4294967297,\"target_nick\":\"PEER\",\"opened_at\":\"y\"}]}}"};
 for(i=0;i<2;i++) { x=NULL; CHECK(gin_topology_apply_queries(b,(const unsigned char *)invalid[i],strlen(invalid[i]),&x)!=GIN_TOPOLOGY_OK && !x); }
 }
 for(count=59;count<=60;count++) {
  n=(size_t)snprintf(payload,sizeof(payload),"{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}");
  for(j=1;j<count;j++) n+=(size_t)snprintf(payload+n,sizeof(payload)-n,",{\"network_id\":4294967297,\"target_nick\":\"P%u\",\"opened_at\":\"x\"}",j);
  CHECK(n+4<sizeof(payload)); strcpy(payload+n,"]}}"); x=queries(b,payload);
  if(count==59) { struct gin_account_registry_delta d;
   CHECK(gin_account_registry_apply(r,x,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK);
   CHECK(gin_account_registry_source_count(r)==64 && d.windows_added==58 && d.sources_added==58);
  } else rejection(r,x,&scope,GIN_ACCOUNT_REGISTRY_LIMIT);
  gin_topology_free(x);
 }
 gin_account_registry_free(r); gin_topology_free(a); gin_topology_free(t); gin_topology_free(b);
}
static void scope_ownership(void)
{
 struct gin_topology *b=parse(boot),*t=queries(b,peer);
 char origin[80],uuid[37],subject[40],chantypes[5];
 struct gin_account_registry_scope s=scope; struct gin_account_registry *r=NULL;
 strcpy(origin,scope.origin); strcpy(uuid,scope.subject_uuid); strcpy(subject,scope.topic_subject); strcpy(chantypes,scope.chantypes);
 s.origin=origin; s.subject_uuid=uuid; s.topic_subject=subject; s.chantypes=chantypes;
 CHECK(gin_account_registry_new(t,&s,&r)==GIN_ACCOUNT_REGISTRY_OK);
 memset(origin,'x',sizeof(origin)); memset(uuid,'x',sizeof(uuid)); memset(subject,'x',sizeof(subject)); memset(chantypes,'x',sizeof(chantypes));
 gin_topology_free(t); gin_topology_free(b);
 CHECK(!strcmp(gin_account_registry_scope(r)->origin,scope.origin));
 CHECK(!strcmp(gin_account_registry_scope(r)->subject_uuid,scope.subject_uuid));
 CHECK(!strcmp(gin_account_registry_scope(r)->topic_subject,scope.topic_subject));
 CHECK(!strcmp(gin_account_registry_scope(r)->chantypes,scope.chantypes));
 gin_account_registry_free(r);
}
int main(void)
{
 struct gin_topology *b = parse(boot), *t = queries(b,peer);
 struct gin_account_registry *r = NULL;
 CHECK(gin_account_registry_new(t,&scope,&r)==GIN_ACCOUNT_REGISTRY_OK);
 gin_topology_free(t); gin_topology_free(b);
 CHECK(gin_account_registry_network_count(r)==1);
 CHECK(gin_account_registry_window_count(r)==2);
 CHECK(gin_account_registry_source_count(r)==6);
 CHECK(gin_account_registry_generation(r)==0);
 CHECK(gin_account_registry_network_at(r,0)->id==INT64_C(4294967297));
 CHECK(!strcmp(gin_account_registry_network_at(r,0)->slug,"Net"));
 CHECK(!strcmp(gin_account_registry_window_at(r,1)->opened_at,"old"));
 CHECK(!strcmp(gin_account_registry_source_at(r,5)->target,"Own1"));
 CHECK(!strcmp(gin_account_registry_scope(r)->subject_uuid,scope.subject_uuid));
 gin_account_registry_free(r); updates(); guards(); scope_ownership(); puts("account-registry: ok"); return 0;
}
