/* Private fabricated-state guard probes, not authentication evidence. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "connect_hot.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"check line %d\n",__LINE__); exit(1); } } while (0)
#include "../src/connect.c"
static enum gin_connect_code submit(struct gin_connect *s,const char *net,const char *peer,size_t *out)
{ return gin_connect_subscribe_query_live(s,net,peer,"#&",1,out); }
int main(void)
{
 struct gin_connect_settings settings={{"https://example.invalid",NULL,100,200},{4,1},1000};
 struct gin_credential c={"https://example.invalid","aa~","12345678-1234-1234-1234-123456789abc","Alice"};
 struct gin_connect_subscription plan[]={{"Net","Own"}};
 struct gin_connect *s=NULL; size_t out=999,i; unsigned char before[sizeof(struct gin_connect)];
 struct topic old[3];
 CHECK(submit(NULL,"Net","Peer",&out)==GIN_CONNECT_INVALID && out==999);
 CHECK(gin_connect_new(&settings,&c,plan,1,NULL,NULL,&s)==GIN_CONNECT_OK);
 CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);
 s->state=GIN_CONNECT_READY; s->result.socket_epoch=1;
 /* The root is the actual active subject authority for this synthetic probe. */
 for(i=0;i<s->count;i++) { snprintf(s->topics[i].ref,sizeof(s->topics[i].ref),"%zu",i+1); s->topics[i].status.join_ref=s->topics[i].ref; s->topics[i].status.state=GIN_CONNECT_TOPIC_JOINED; }
 s->refs.current=3;
 memcpy(before,s,sizeof(*s));
 CHECK(gin_connect_subscribe_query_live(s,"Net","Peer","#&",2,&out)==GIN_CONNECT_SCOPE);
 CHECK(!memcmp(before,s,sizeof(*s)) && out==999);
 CHECK(submit(s,"Missing","Peer",&out)==GIN_CONNECT_SCOPE);
 CHECK(submit(s,"Net","#channel",&out)==GIN_CONNECT_INVALID);
 CHECK(submit(s,"Net","B.ob",&out)==GIN_CONNECT_INVALID);
 CHECK(!memcmp(before,s,sizeof(*s)) && out==999);
 s->in_callback=1; CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);s->in_callback=0;
 s->query.outcome=GIN_QUERY_ACCEPTED;CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);s->query.outcome=GIN_QUERY_NONE;
 s->writer=WIRE_APPLICATION;CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);s->writer=WIRE_IDLE;
 s->heartbeat=GIN_HEARTBEAT_QUEUED;CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);s->heartbeat=GIN_HEARTBEAT_OFF;
 s->refs.current=UINT64_MAX;memcpy(before,s,sizeof(*s));CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_LIMIT);CHECK(!memcmp(before,s,sizeof(*s)));s->refs.current=3;
 CHECK(submit(s,"Net","oWN",&out)==GIN_CONNECT_OK && out==2 && s->refs.current==3);
 memcpy(old,s->topics,sizeof(old));s->receive_drained=1;
 CHECK(submit(s,"Net","NewPeer",&out)==GIN_CONNECT_OK && out==3);
 CHECK(s->count==4 && s->pending==3 && s->refs.current==4 && s->state==GIN_CONNECT_JOINING && !s->receive_drained);
 CHECK(!memcmp(old,s->topics,sizeof(old)));
 CHECK(!strcmp(s->topics[3].text,"grappa:user:Alice/network:Net/channel:newpeer"));
 CHECK(s->topics[3].status.topic==s->topics[3].text && s->topics[3].status.join_ref==s->topics[3].ref);
 CHECK(s->outbound.data && s->topics[3].status.state==GIN_CONNECT_TOPIC_PENDING);
 gin_phoenix_bytes_free(&s->outbound);s->state=GIN_CONNECT_READY;s->topics[3].status.state=GIN_CONNECT_TOPIC_JOINED;
 s->count=64;out=999;memcpy(before,s,sizeof(*s));
 CHECK(submit(s,"Net","OWN",&out)==GIN_CONNECT_OK && out==2 && !memcmp(before,s,sizeof(*s)));
 out=999;CHECK(submit(s,"Net","Another",&out)==GIN_CONNECT_LIMIT && out==999 && !memcmp(before,s,sizeof(*s)));
 s->count=4;gin_connect_close(s);CHECK(submit(s,"Net","Peer",&out)==GIN_CONNECT_INVALID);gin_connect_free(s);
 puts("PASS guard probes append stability duplicate-at-cap ref-overflow");return 0;
}
