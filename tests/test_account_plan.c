#include "account_plan.h"
#include "connect_values.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifdef ACCOUNT_PLAN_ALLOC_FAULT
static int allocation_fails;
static size_t allocation_calls;
static void *plan_calloc(size_t count, size_t size)
{
 allocation_calls++;
 return allocation_fails ? NULL : calloc(count, size);
}
#define calloc plan_calloc
#include "../src/account_plan.c"
#undef calloc
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static const char boot[] =
 "{\"networks\":[{\"id\":4294967297,\"slug\":\"Net\",\"nick\":\"Self\"},{\"id\":7,\"slug\":\"net\",\"nick\":\"Other\"}],"
 "\"channels\":{\"Net\":[{\"name\":\"#in\",\"joined\":true,\"source\":\"joined\"},{\"name\":\"#out\",\"joined\":true,\"source\":\"joined\"}],"
 "\"net\":[{\"name\":\"#Pending\",\"joined\":false,\"source\":\"autojoin\"}]},\"heads\":{}}";
static const char queries[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"4294967297\":[{\"network_id\":4294967297,\"target_nick\":\"PeEr\",\"opened_at\":\"opaque\"}]}}";
static const char empty[] = "{\"kind\":\"query_windows_list\",\"windows\":{}}";
static struct gin_topology *parse(const char *b)
{ struct gin_topology *t = NULL; CHECK(gin_topology_parse_boot((const unsigned char *)b, strlen(b), "#", &t) == GIN_TOPOLOGY_OK); return t; }
static struct gin_topology *known(struct gin_topology *b, const char *q)
{ struct gin_topology *t = NULL; CHECK(gin_topology_apply_queries(b, (const unsigned char *)q, strlen(q), &t) == GIN_TOPOLOGY_OK); return t; }
static void native(const struct gin_connect_subscription *subs, size_t count, size_t expected)
{
 struct gin_connect_settings settings = {{"https://example.invalid", NULL, 100, 200}, {4,4}, 1000};
 struct gin_credential c = {"https://example.invalid", "fake-token", "12345678-1234-1234-1234-123456789abc", "TopicCase"};
 struct gin_connect *s = NULL; size_t i, n; char topic[GIN_WS_TOPIC_CAP];
 CHECK(gin_connect_new(&settings, &c, subs, count, NULL, NULL, &s) == GIN_CONNECT_OK);
 CHECK(gin_connect_get_state(s) == GIN_CONNECT_IDLE);
 for (i = 0; i < gin_connect_topic_count(s); i++) CHECK(strstr(gin_connect_topic_at(s,i)->topic, "TopicCase") != NULL);
 fprintf(stderr, "Native desired=%lu expected=%lu\n", (unsigned long)gin_connect_topic_count(s), (unsigned long)expected);
 CHECK(gin_connect_topic_count(s) == expected);
 for (i = 0; i < count; i++) {
  CHECK(gin_ws_topic("TopicCase",subs[i].network,subs[i].target,topic,sizeof(topic),&n) == GIN_WS_VALUE_OK);
  CHECK(!strcmp(gin_connect_topic_at(s,i+1)->topic,topic));
 }
 CHECK(gin_ws_topic("TopicCase", NULL, NULL, topic, sizeof(topic), &n) == GIN_WS_VALUE_OK);
 CHECK(!strcmp(gin_connect_topic_at(s,0)->topic, topic));
 gin_connect_free(s);
}
static void status_subscription(void)
{
 struct gin_topology *base = parse(boot), *topology = known(base,queries);
 struct gin_account_plan *plan = NULL;
 const struct gin_connect_subscription *subs;
 size_t count;
 CHECK(gin_account_plan_new(topology,"#","TopicCase",&plan) == GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_plan_network_count(plan) == 2 && gin_account_plan_window_count(plan) == 3);
 gin_topology_free(topology); gin_topology_free(base);
 subs = gin_account_plan_subscriptions(plan,&count);
 /* Real Native constructor, with healthy complete inventory and no I/O. */
 native(subs,count,10);
 CHECK(count == 9);
 CHECK(!subs[0].target && !subs[1].target);
 CHECK(!strcmp(subs[0].network,"net") && !strcmp(subs[1].network,"Net"));
 CHECK(!strcmp(subs[2].target,"$server") && !strcmp(subs[3].target,"$server"));
 CHECK(!strcmp(subs[2].network,"net") && !strcmp(subs[3].network,"Net"));
 CHECK(!strcmp(subs[7].target,"Other") && !strcmp(subs[8].target,"Self"));
 gin_account_plan_free(plan);
}
static void extra_characterization(void)
{
 struct gin_topology *base, *topology;
 struct gin_account_plan *plan = NULL;
 const struct gin_connect_subscription *subscriptions;
 size_t count;
 /* A complete empty account is Root-only, not unknown query inventory. */
 base = parse("{\"networks\":[],\"channels\":{},\"heads\":{}}");
 topology = known(base, empty); gin_topology_free(base);
 CHECK(gin_account_plan_new(topology,"#","TopicCase",&plan) == GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_plan_network_count(plan) == 0 && gin_account_plan_window_count(plan) == 0);
 CHECK(!gin_account_plan_network_at(plan,0) && !gin_account_plan_window_at(plan,0));
 subscriptions = gin_account_plan_subscriptions(plan,&count);
 CHECK(count == 0 && gin_account_plan_topic_count(plan) == 1); native(subscriptions,count,1);
 gin_account_plan_free(plan); plan = NULL; gin_topology_free(topology);
 /* An explicitly listed own-nick query is retained, but its differently cased
  * wire target deduplicates with the transport-only own-nick subscription. */
 base = parse("{\"networks\":[{\"id\":1,\"slug\":\"N\",\"nick\":\"Self\"}],\"channels\":{\"N\":[]},\"heads\":{}}");
 topology = known(base,"{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"sELF\",\"opened_at\":\"x\"}]}}");
 gin_topology_free(base);
 CHECK(gin_account_plan_new(topology,"#","TopicCase",&plan) == GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_plan_window_count(plan) == 1 && gin_account_plan_topic_count(plan) == 4);
 CHECK(!strcmp(gin_account_plan_window_at(plan,0)->name.original_wire_target,"sELF"));
 CHECK(!strcmp(gin_account_plan_window_at(plan,0)->name.window_component,"@self"));
 subscriptions = gin_account_plan_subscriptions(plan,&count); CHECK(count == 3);
 native(subscriptions,count,4);
 gin_account_plan_free(plan); plan = NULL;
#ifdef ACCOUNT_PLAN_ALLOC_FAULT
 allocation_calls = 0; allocation_fails = 1;
 CHECK(gin_account_plan_new(topology,"#","TopicCase",&plan) == GIN_ACCOUNT_PLAN_NOMEM);
 CHECK(!plan && allocation_calls == 1 && gin_topology_window_count(topology) == 1);
 allocation_calls = 0; allocation_fails = 0;
 CHECK(gin_account_plan_new(topology,"#","TopicCase",&plan) == GIN_ACCOUNT_PLAN_OK);
 CHECK(plan && allocation_calls == 1);
 gin_account_plan_free(plan); plan = NULL;
#endif
 gin_topology_free(topology);
}
int main(void)
{
 struct gin_topology *b, *t; alarm(15); b = parse(boot); t = known(b, queries);
#ifdef LEGACY_SINGLE_WINDOW_BRIDGE
 (void)extra_characterization;
 /* Private baseline old selected recipe; NOT a defect in Native constructor. */
 { const struct gin_topology_window *w = gin_topology_find_window(t, INT64_C(4294967297), "#in");
 struct gin_connect_subscription selected = {"Net", w->name.original_wire_target};
 CHECK(gin_topology_window_count(t) == 4); native(&selected, 1, 8); }
#else
 status_subscription();
 extra_characterization();
 { struct gin_account_plan *p = NULL, *occupied; size_t count, i; const struct gin_connect_subscription *subs;
 CHECK(gin_account_plan_new(b,"#","TopicCase",&p) == GIN_ACCOUNT_PLAN_QUERIES_UNKNOWN && !p);
 CHECK(gin_account_plan_new(t,"#","TopicCase",&p) == GIN_ACCOUNT_PLAN_OK);
 occupied = p; CHECK(gin_account_plan_new(t,"#","TopicCase",&p) == GIN_ACCOUNT_PLAN_INVALID && p == occupied);
 CHECK(gin_account_plan_network_count(p) == 2 && gin_account_plan_window_count(p) == 3);
 CHECK(gin_account_plan_network_at(p,1)->id == INT64_C(4294967297));
 CHECK(!strcmp(gin_account_plan_network_at(p,1)->slug,"Net"));
 for(i=0;i<3;i++) { const struct gin_topology_window *w = gin_account_plan_window_at(p,i);
 CHECK(w->network_id == INT64_C(4294967297));
 CHECK(strcmp(w->name.window_component,"in") && strcmp(w->name.window_component,"out"));
 if(w->role == GIN_REMOTE_QUERY) CHECK(!strcmp(w->name.original_wire_target,"PeEr") && !strcmp(w->name.canonical_target,"peer") && !strcmp(w->opened_at,"opaque"));
 else CHECK(!strcmp(w->name.window_component,w->name.canonical_target)); }
 gin_topology_free(t); t = NULL;
 subs = gin_account_plan_subscriptions(p,&count); CHECK(count == 9); native(subs,count,10);
 CHECK(gin_account_plan_topic_count(p) == 10); gin_account_plan_free(p); p = NULL;
 t = known(b,empty); CHECK(gin_account_plan_new(t,"#","TopicCase",&p) == GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_plan_window_count(p) == 2); gin_account_plan_free(p); p = NULL;
 CHECK(gin_topology_window_count(t) == 3); gin_topology_free(t); t = NULL;
 { char payload[20000]; size_t used; unsigned j, lim;
 for(lim=60;lim<=61;lim++) { struct gin_topology *single = parse("{\"networks\":[{\"id\":1,\"slug\":\"N\",\"nick\":\"Self\"}],\"channels\":{\"N\":[]},\"heads\":{}}");
 used = (size_t)snprintf(payload,sizeof(payload),"{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[");
 for(j=0;j<lim;j++) used += (size_t)snprintf(payload+used,sizeof(payload)-used,"%s{\"network_id\":1,\"target_nick\":\"Peer%u\",\"opened_at\":\"x\"}",j?",":"",j);
 CHECK(used+4 < sizeof(payload)); strcpy(payload+used,"]}}"); t = known(single,payload); gin_topology_free(single);
 CHECK(gin_account_plan_new(t,"#","TopicCase",&p) == (lim==60?GIN_ACCOUNT_PLAN_OK:GIN_ACCOUNT_PLAN_LIMIT));
 if(lim==60) { subs = gin_account_plan_subscriptions(p,&count); CHECK(count==63); native(subs,count,64); gin_account_plan_free(p); p=NULL; } else CHECK(!p);
 gin_topology_free(t); t=NULL;
 } }
 }
#endif
 gin_topology_free(t); gin_topology_free(b); puts("account-plan: ok"); return 0;
}
