#include "gin_history.h"
#include "history_values.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <jansson.h>
#ifdef TEST_OWNED_FAULT
static size_t alloc_at, fail_at;
static void *fault_malloc(size_t n) { return ++alloc_at==fail_at ? NULL : malloc(n); }
static void *fault_calloc(size_t n,size_t z) { return ++alloc_at==fail_at ? NULL : calloc(n,z); }
#define malloc fault_malloc
#define calloc fault_calloc
#include "../src/history_page.c"
#undef malloc
#undef calloc
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static struct gin_history_scope scope = {"https://example.test", "01234567-89ab-cdef-0123-456789abcdef", "Net", "#Room", "#&", "Me", 1, 7, GIN_RENDER_CHANNEL, GIN_RENDER_MODE_PLAIN};
static struct gin_history_request request = {GIN_HISTORY_AFTER,0,2};
static const char row[] = "[{\"id\":9223372036854775807,\"server_time\":-9223372036854775808,\"network\":\"Net\",\"channel\":\"#room\",\"kind\":\"privmsg\",\"sender\":\"peer\",\"body\":null}]";
static enum gin_history_code decode(const char *s,struct gin_history_page **p)
{ return gin_history_page_decode(&scope,&request,(const unsigned char *)s,strlen(s),p); }
static void expect(const char *s,enum gin_history_code code)
{ struct gin_history_page *p=NULL; CHECK(decode(s,&p)==code);CHECK(!p); }
static void more(void)
{
    struct gin_history_page *p=NULL,*q=NULL; const struct gin_history_row *v[2];
    struct gin_history_row a={2,100,"Net","#room","privmsg","sender","","",0}, b;
    a.dm_with=NULL; b=a;b.id=3;b.server_time=-100;v[0]=&a;v[1]=&b;
    CHECK(gin_history_copy_rows(&scope,v,2,GIN_HISTORY_REST,&request,&p)==GIN_HISTORY_OK);
    CHECK(gin_history_page_owned_bytes(p)>sizeof(*p));
    v[0]=gin_history_page_row(p,0);v[1]=gin_history_page_row(p,1);
    CHECK(gin_history_copy_rows(&scope,v,2,GIN_HISTORY_MERGED,NULL,&q)==GIN_HISTORY_OK);
    gin_history_page_free(p);p=NULL;CHECK(!strcmp(gin_history_page_row(q,0)->body,""));CHECK(!gin_history_page_request(q));gin_history_page_free(q);q=NULL;
    b.id=2;v[0]=&a;v[1]=&b;CHECK(gin_history_copy_rows(&scope,v,2,GIN_HISTORY_MERGED,NULL,&p)==GIN_HISTORY_MALFORMED);CHECK(!p);
    CHECK(gin_history_row_equal(&a,&b)==0);b=a;CHECK(gin_history_row_equal(&a,&b));b.body=NULL;CHECK(!gin_history_row_equal(&a,&b));b=a;b.dm_present=1;CHECK(!gin_history_row_equal(&a,&b));
    expect("{\"messages\":[]}",GIN_HISTORY_MALFORMED);
    expect("[{\"kind\":\"unknown\",\"body\":\"prefix\\u0001E2Ebroken\"}]",GIN_HISTORY_UNSUPPORTED);
    expect("[] trailing",GIN_HISTORY_MALFORMED);expect("[{\"id\":1,\"id\":2}]",GIN_HISTORY_MALFORMED);
    expect("[{\"meta\":\"\\u0000\"}]",GIN_HISTORY_MALFORMED);
    request.limit=0;CHECK(decode("[]",&p)==GIN_HISTORY_INVALID);request.limit=2;
    request.cursor=GIN_HISTORY_BEFORE;request.anchor=2;v[0]=&a;CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_REST,&request,&p)==GIN_HISTORY_MALFORMED);
    request.cursor=GIN_HISTORY_LATEST;request.anchor=0;b=a;b.id=1;v[1]=&b;CHECK(gin_history_copy_rows(&scope,v,2,GIN_HISTORY_REST,&request,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
    request.cursor=GIN_HISTORY_AFTER;
    scope.encryption=GIN_RENDER_MODE_UNKNOWN;CHECK(decode("[]",&p)==GIN_HISTORY_UNSUPPORTED);scope.encryption=GIN_RENDER_MODE_PLAIN;
    scope.role=GIN_RENDER_QUERY;scope.target="Peer";a.channel="OldNick";a.dm_with="peer";a.dm_present=1;
    CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
    a.dm_with=NULL;CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_ROUTE);
    a.kind="joined";a.channel="peer";CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
    scope.role=GIN_RENDER_STATUS;scope.target="$server";a.channel="$server";a.dm_present=0;
    CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
    scope.role=GIN_RENDER_CHANNEL;scope.target="#Room";
    {
        char body[GIN_HISTORY_BODY_MAX+2];memset(body,'x',sizeof(body));body[sizeof(body)-1]=0;a.channel="#room";a.body=body;
        CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_LIMIT);body[GIN_HISTORY_BODY_MAX]=0;
        CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
        body[0]=(char)0xff;CHECK(gin_history_copy_rows(&scope,v,1,GIN_HISTORY_LIVE,NULL,&p)==GIN_HISTORY_MALFORMED);
    }
    CHECK(decode("[]",&p)==GIN_HISTORY_OK);q=p;CHECK(decode(row,&p)==GIN_HISTORY_INVALID && p==q);gin_history_page_free(p);
}
#ifndef TEST_OWNED_FAULT
static size_t json_at,json_fail;
static void *json_fault(size_t n) {return ++json_at==json_fail ? NULL:malloc(n);}
#endif
static void faults(void)
{
    size_t i,j; struct gin_history_page *p=NULL,*expected=NULL;
    static const char *const fixtures[]={
        "[{\"id\":9223372036854775807,\"server_time\":-9223372036854775808,\"network\":\"Net\",\"channel\":\"#room\",\"kind\":\"privmsg\",\"sender\":\"peer\",\"body\":null}]",
        "[{\"i\\u0064\":9007199254740993,\"server_time\":9007199254740995,\"network\":\"N\\u0065t\",\"channel\":\"OldNick\",\"kind\":\"privmsg\",\"sender\":\"\\ud83d\\ude00\",\"body\":\"line\\n\\u00e9\",\"dm_with\":\"PEER\"}]",
        "[{\"id\":12,\"server_time\":0,\"network\":\"Net\",\"channel\":\"Peer\",\"kind\":\"joined\",\"sender\":\"\",\"body\":\"\",\"dm_with\":null}]"
    };
#ifndef TEST_OWNED_FAULT
    json_malloc_t oldmalloc;json_free_t oldfree;json_get_alloc_funcs(&oldmalloc,&oldfree);
#endif
    for(j=0;j<sizeof(fixtures)/sizeof(fixtures[0]);j++) {
        if(j) {scope.role=GIN_RENDER_QUERY;scope.target="Peer";}
        CHECK(decode(fixtures[j],&expected)==GIN_HISTORY_OK);
#ifndef TEST_OWNED_FAULT
        json_set_alloc_funcs(json_fault,free);
#endif
        for(i=1;i<240;i++) {
            enum gin_history_code c;
#ifdef TEST_OWNED_FAULT
            alloc_at=0;fail_at=i;
#else
            json_at=0;json_fail=i;
#endif
            c=decode(fixtures[j],&p);
            if(c==GIN_HISTORY_OK) {CHECK(gin_history_page_count(p)==1);CHECK(gin_history_row_equal(gin_history_page_row(p,0),gin_history_page_row(expected,0)));CHECK(gin_history_scope_equal(gin_history_page_scope(p),&scope));gin_history_page_free(p);p=NULL;} else CHECK(!p);
        }
#ifdef TEST_OWNED_FAULT
        fail_at=0;
#else
        json_set_alloc_funcs(oldmalloc,oldfree);
#endif
        gin_history_page_free(expected);expected=NULL;
    }
    scope.role=GIN_RENDER_CHANNEL;scope.target="#Room";
}

int main(void)
{
    struct gin_history_page *p = NULL;
    const struct gin_history_row *r;
    CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)row,strlen(row),&p)==GIN_HISTORY_OK);
    CHECK(gin_history_page_count(p)==1);
    r=gin_history_page_row(p,0); CHECK(r && r->id==INT64_MAX && r->server_time==INT64_MIN && !r->body && !r->dm_present);
    CHECK(gin_history_page_scope(p)!=&scope && gin_history_scope_equal(gin_history_page_scope(p),&scope));
    gin_history_page_free(p);
    p=NULL;
    {
        const char encrypted[]="[{\"kind\":\"e2e_broken\"}]";
        CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)encrypted,strlen(encrypted),&p)==GIN_HISTORY_UNSUPPORTED);
        CHECK(!p);
    }
    more();faults();
    {
        char *large=malloc(GIN_HISTORY_RESPONSE_MAX+1); CHECK(large);memset(large,' ',GIN_HISTORY_RESPONSE_MAX+1);large[0]='[';large[1]=']';p=NULL;
        CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)large,GIN_HISTORY_RESPONSE_MAX,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
        CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)large,GIN_HISTORY_RESPONSE_MAX+1,&p)==GIN_HISTORY_LIMIT);free(large);
    }
    {
        char deep[128];size_t i;for(i=0;i<33;i++) deep[i]='[';for(i=33;i<66;i++) deep[i]=']';deep[66]=0;
        expect(deep,GIN_HISTORY_LIMIT);
    }
    {
        struct gin_history_row r={1,0,"Net","#room","privmsg","s",NULL,NULL,0};
        struct gin_history_row rows[201];const struct gin_history_row *views[201];size_t i;
        for(i=0;i<201;i++) {rows[i]=r;rows[i].id=(int64_t)i+1;views[i]=&rows[i];}
        request.limit=200;p=NULL;CHECK(gin_history_copy_rows(&scope,views,200,GIN_HISTORY_REST,&request,&p)==GIN_HISTORY_OK);gin_history_page_free(p);p=NULL;
        CHECK(gin_history_copy_rows(&scope,views,201,GIN_HISTORY_REST,&request,&p)==GIN_HISTORY_LIMIT);request.limit=2;
    }
    {
        const char escaped[]="[{\"i\\u0064\":42,\"server_time\":123,\"network\":\"N\\u0065t\",\"channel\":\"#room\",\"kind\":\"unknown\",\"sender\":\"\\ud83d\\ude00\",\"body\":\"\\n\\\\\\\"\"}]";
        p=NULL;CHECK(decode(escaped,&p)==GIN_HISTORY_OK);CHECK(gin_history_page_row(p,0)->id==42);gin_history_page_free(p);
    }
    puts("history page: PASS"); return 0;
}
