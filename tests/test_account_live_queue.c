/* Synthetic copied views only: capacity is not source authentication.
 * Private fault translation unit; never link session_live_queue.c separately. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "session_live_queue.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static size_t attempts, fail_at, allocations, releases;
struct allocation { void *p; size_t n; };
static struct allocation live[128];
static void *test_malloc(size_t n)
{
    size_t i; void *p;
    attempts++;
    if (fail_at && attempts == fail_at) return NULL;
    p = malloc(n); CHECK(p != NULL);
    for (i=0; i<128 && live[i].p; i++) { }
    CHECK(i<128); live[i].p=p; live[i].n=n; allocations++; return p;
}
static void test_free(void *p)
{
    size_t i,j;
    if (!p) return;
    for (i=0; i<128 && live[i].p!=p; i++) { }
    CHECK(i<128);
    for(j=0;j<live[i].n;j++) CHECK(((unsigned char *)p)[j]==0);
    live[i].p=NULL; live[i].n=0; releases++; free(p);
}
#define malloc test_malloc
#define free test_free
#include "../src/session_live_queue.c"
#undef malloc
#undef free
static struct gin_live_view sample(size_t index)
{
    struct gin_live_view v;
    memset(&v,0,sizeof(v));
    v.source_join_ref="source"; v.fields.topic="topic"; v.fields.event="event";
    v.fields.join_ref=""; v.fields.ref=NULL;
    v.fields.payload.data=(const unsigned char *)"{}"; v.fields.payload.length=2;
    v.source_index=index; v.observed_epoch=UINT64_MAX; v.receive_ms=INT64_MIN;
    v.kind=GIN_LIVE_MESSAGE; return v;
}
#ifndef LEGACY_SOURCE_BRIDGE
static void boundary(size_t limit)
{
    struct gin_live_queue *q=NULL; struct gin_live_view v=sample(limit-1);
    const struct gin_live_view *head; size_t a,n,b,cost;
    char source[]="source", topic[]="topic", event[]="event", join[]="join";
    unsigned char payload[]="{}";
    CHECK(gin_live_queue_new_sources(2,GIN_LIVE_BYTES_MAX,limit,&q)==GIN_LIVE_OK);
    v.source_join_ref=source; v.fields.topic=topic; v.fields.event=event;
    v.fields.join_ref=join; v.fields.ref=""; v.fields.payload.data=payload;
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
    head=gin_live_queue_peek(q); cost=gin_live_queue_bytes(q);
    CHECK(cost==sizeof(struct entry)+7+6+6+5+1+2);
    memset(source,'x',6); memset(topic,'x',5); memset(event,'x',5);
    memset(join,'x',4); memset(payload,'x',2);
    CHECK(head->source_index==limit-1 && head->observed_epoch==UINT64_MAX);
    CHECK(head->receive_ms==INT64_MIN && head->kind==GIN_LIVE_MESSAGE);
    CHECK(!strcmp(head->source_join_ref,"source") && !strcmp(head->fields.topic,"topic"));
    CHECK(!strcmp(head->fields.event,"event") && !strcmp(head->fields.join_ref,"join"));
    CHECK(head->fields.ref && !*head->fields.ref && !memcmp(head->fields.payload.data,"{}",2));
    a=attempts; n=gin_live_queue_count(q); b=gin_live_queue_bytes(q);
    v=sample(limit); CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_INVALID);
    CHECK(attempts==a && gin_live_queue_count(q)==n && gin_live_queue_bytes(q)==b);
    CHECK(gin_live_queue_peek(q)==head && gin_live_queue_failure(q)==GIN_LIVE_OK);
    v=sample(limit-1); v.fields.join_ref=NULL; v.fields.ref="";
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_LIMIT);
    CHECK(gin_live_queue_drop(q)==GIN_LIVE_OK);
    head=gin_live_queue_peek(q); CHECK(head->fields.join_ref==NULL && head->fields.ref && !*head->fields.ref);
    CHECK(gin_live_queue_drop(q)==GIN_LIVE_OK);
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_LIMIT && gin_live_queue_failure(q)==GIN_LIVE_LIMIT);
    gin_live_queue_free(q); q=NULL;
    CHECK(gin_live_queue_new_sources(2,cost-1,limit,&q)==GIN_LIVE_OK);
    v=sample(limit-1); v.fields.join_ref="join"; v.fields.ref="";
    a=attempts; CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_LIMIT && attempts==a);
    CHECK(gin_live_queue_count(q)==0 && gin_live_queue_bytes(q)==0);
    CHECK(gin_live_queue_drop(q)==GIN_LIVE_EMPTY);
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_LIMIT); gin_live_queue_free(q); q=NULL;
    CHECK(gin_live_queue_new_sources(2,GIN_LIVE_BYTES_MAX,limit,&q)==GIN_LIVE_OK);
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
    fail_at=attempts+1; CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_NOMEM);
    a=attempts; CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_NOMEM && attempts==a);
    CHECK(gin_live_queue_count(q)==1); CHECK(gin_live_queue_drop(q)==GIN_LIVE_OK);
    CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_NOMEM && attempts==a);
    CHECK(gin_live_queue_failure(q)==GIN_LIVE_NOMEM); gin_live_queue_free(q); fail_at=0;
}
#endif
int main(void)
{
    struct gin_live_queue *q=NULL; struct gin_live_view v=sample(63);
    alarm(10);
#ifdef LEGACY_SOURCE_BRIDGE
    CHECK(gin_live_queue_new(4,GIN_LIVE_BYTES_MAX,&q)==GIN_LIVE_OK);
    { enum gin_live_code code=gin_live_queue_push(q,&v);
      fprintf(stderr,"LEGACY_SOURCE_BRIDGE source_index=63 expected=OK actual=%d (INVALID=%d)\n",code,GIN_LIVE_INVALID);
      CHECK(code==GIN_LIVE_OK); }
#else
    { size_t a=attempts; struct gin_live_queue *old;
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,0,&q)==GIN_LIVE_INVALID && !q);
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,65,&q)==GIN_LIVE_INVALID && !q);
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,(size_t)-1,&q)==GIN_LIVE_INVALID);
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,64,NULL)==GIN_LIVE_INVALID);
      CHECK(gin_live_queue_new_sources(0,GIN_LIVE_BYTES_MAX,64,&q)==GIN_LIVE_INVALID);
      CHECK(gin_live_queue_new_sources(65,GIN_LIVE_BYTES_MAX,64,&q)==GIN_LIVE_INVALID);
      CHECK(gin_live_queue_new_sources(4,0,64,&q)==GIN_LIVE_INVALID);
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX+1u,64,&q)==GIN_LIVE_INVALID);
      CHECK(attempts==a);
      fail_at=attempts+1;
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,64,&q)==GIN_LIVE_NOMEM && !q);
      fail_at=0;
      CHECK(gin_live_queue_new(4,GIN_LIVE_BYTES_MAX,&q)==GIN_LIVE_OK);
      old=q; a=attempts;
      CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,64,&q)==GIN_LIVE_INVALID && q==old);
      CHECK(attempts==a); v=sample(3); CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
      a=attempts; v.source_index=4;
      CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_INVALID && attempts==a);
      CHECK(gin_live_queue_count(q)==1 && gin_live_queue_failure(q)==GIN_LIVE_OK);
      gin_live_queue_free(q); q=NULL;
    }
    boundary(1); boundary(4); boundary(64);
    CHECK(gin_live_queue_new_sources(4,GIN_LIVE_BYTES_MAX,64,&q)==GIN_LIVE_OK);
    v=sample(4); CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
    v=sample(63); CHECK(gin_live_queue_push(q,&v)==GIN_LIVE_OK);
    gin_live_queue_free(q);
    CHECK(allocations==releases);
    printf("PASS account source limits 1/4/64, default4, ownership, wipes, limits, NOMEM; allocations=%zu releases=%zu\n",allocations,releases);
#endif
    return 0;
}
