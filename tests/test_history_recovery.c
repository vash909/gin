#define _POSIX_C_SOURCE 200809L
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "history_values.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Accessor/copy facade only: define GIN_HISTORY_RECOVERY_REAL_PAGE and link
 * history_page.c to exercise the real owned constructor instead. */
#ifndef GIN_HISTORY_RECOVERY_REAL_PAGE
static int same(const char *a, const char *b) { return a == b || (a && b && strcmp(a,b) == 0); }
int gin_history_scope_equal(const struct gin_history_scope *a, const struct gin_history_scope *b)
{
    return a && b && same(a->origin,b->origin) && same(a->subject_id,b->subject_id) && same(a->network_slug,b->network_slug) && same(a->target,b->target) && same(a->chantypes,b->chantypes) && same(a->own_nick,b->own_nick) && a->network_id==b->network_id && a->socket_epoch==b->socket_epoch && a->role==b->role && a->encryption==b->encryption;
}
int gin_history_row_equal(const struct gin_history_row *a, const struct gin_history_row *b)
{
    return a->id==b->id && a->server_time==b->server_time && same(a->network,b->network) && same(a->channel,b->channel) && same(a->kind,b->kind) && same(a->sender,b->sender) && same(a->body,b->body) && same(a->dm_with,b->dm_with) && a->dm_present==b->dm_present;
}
enum gin_history_code gin_history_scope_check(const struct gin_history_scope *s) { return s && s->origin && s->socket_epoch ? GIN_HISTORY_OK : GIN_HISTORY_INVALID; }
static int fail_copy;
static char *duptext(const char *s, size_t *n) { char *p; if (!s) return NULL; p=strdup(s); CHECK(p); *n+=strlen(s)+1; return p; }
void gin_history_page_free(struct gin_history_page *p)
{
    size_t i; if (!p) return;
    free((void *)p->scope.origin); free((void *)p->scope.subject_id); free((void *)p->scope.network_slug); free((void *)p->scope.target); free((void *)p->scope.chantypes); free((void *)p->scope.own_nick);
    for(i=0;i<p->count;i++) { free((void *)p->rows[i].network); free((void *)p->rows[i].channel); free((void *)p->rows[i].kind); free((void *)p->rows[i].sender); free((void *)p->rows[i].body); free((void *)p->rows[i].dm_with); }
    free(p->rows); free(p);
}
size_t gin_history_page_count(const struct gin_history_page *p) { return p ? p->count : 0; }
size_t gin_history_page_owned_bytes(const struct gin_history_page *p) { return p ? p->owned_bytes : 0; }
const struct gin_history_row *gin_history_page_row(const struct gin_history_page *p,size_t i) { return p && i<p->count ? p->rows+i : NULL; }
const struct gin_history_scope *gin_history_page_scope(const struct gin_history_page *p) { return p ? &p->scope : NULL; }
enum gin_history_source gin_history_page_source(const struct gin_history_page *p) { return p ? p->source : GIN_HISTORY_MERGED; }
const struct gin_history_request *gin_history_page_request(const struct gin_history_page *p) { return p && p->source==GIN_HISTORY_REST ? &p->request : NULL; }
enum gin_history_code gin_history_copy_rows(const struct gin_history_scope *s,const struct gin_history_row *const *rows,size_t n,enum gin_history_source source,const struct gin_history_request *req,struct gin_history_page **out)
{
    struct gin_history_page *p; size_t i;
    CHECK(out && !*out); if(fail_copy) return GIN_HISTORY_NOMEM; p=calloc(1,sizeof(*p)); CHECK(p); p->owned_bytes=sizeof(*p)+n*sizeof(*p->rows); p->scope=*s;
#define DUP_SCOPE(f) p->scope.f=duptext(s->f,&p->owned_bytes)
    DUP_SCOPE(origin); DUP_SCOPE(subject_id); DUP_SCOPE(network_slug); DUP_SCOPE(target); DUP_SCOPE(chantypes); DUP_SCOPE(own_nick);
#undef DUP_SCOPE
    p->source=source; if(req) p->request=*req; p->count=n; p->rows=calloc(n ? n : 1,sizeof(*p->rows)); CHECK(p->rows);
    for(i=0;i<n;i++) { p->rows[i]=*rows[i];
#define DUP_ROW(f) p->rows[i].f=duptext(rows[i]->f,&p->owned_bytes)
        DUP_ROW(network); DUP_ROW(channel); DUP_ROW(kind); DUP_ROW(sender); DUP_ROW(body); DUP_ROW(dm_with);
#undef DUP_ROW
    }
    if(p->owned_bytes>GIN_HISTORY_OWNED_MAX) { gin_history_page_free(p); return GIN_HISTORY_LIMIT; }
    *out=p; return GIN_HISTORY_OK;
}
#endif
static struct gin_history_scope scope = { "https://example.test", "01234567-89ab-cdef-0123-456789abcdef", "Net", "#room", "#&", "self", 7, 9, GIN_RENDER_CHANNEL, GIN_RENDER_MODE_PLAIN };
static void initial(void)
{
    struct gin_history_recovery *r=NULL; struct gin_history_request req; struct gin_history_status s;
    CHECK(gin_history_recovery_new(&scope,42,2,100,&r)==GIN_HISTORY_OK);
    s=gin_history_recovery_status(r); CHECK(s.fetch_after==42 && !s.unknown_before && s.rows==0 && s.pages==0);
    CHECK(gin_history_recovery_next(r,100,&req)==GIN_HISTORY_OK); CHECK(req.cursor==GIN_HISTORY_AFTER && req.anchor==42 && req.limit==2);
    gin_history_recovery_free(r);
}
static struct gin_history_page *page_for(const struct gin_history_scope *s, enum gin_history_source source, int64_t anchor, unsigned limit, int64_t first, size_t n, const char *body)
{
    struct gin_history_row rows[GIN_HISTORY_PAGE_ROWS_MAX]; const struct gin_history_row *refs[GIN_HISTORY_PAGE_ROWS_MAX];
    struct gin_history_request req={GIN_HISTORY_AFTER,anchor,limit}; struct gin_history_page *p=NULL; size_t i;
    CHECK(n<=GIN_HISTORY_PAGE_ROWS_MAX);
    for(i=0;i<n;i++) { struct gin_history_row row={0,10,"Net","#room","privmsg","alice",NULL,NULL,0}; row.id=first+(int64_t)i; row.body=body; rows[i]=row; refs[i]=rows+i; }
    CHECK(gin_history_copy_rows(s,refs,n,source,source==GIN_HISTORY_REST ? &req : NULL,&p)==GIN_HISTORY_OK); return p;
}
static void feed(struct gin_history_recovery *r, enum gin_history_source source, int64_t first, size_t n, const char *body)
{
    struct gin_history_request req; struct gin_history_page *p;
    CHECK(gin_history_recovery_next(r,100,&req)==GIN_HISTORY_OK);
    p=page_for(&scope,source,req.anchor,req.limit,first,n,body);
    CHECK((source==GIN_HISTORY_LIVE ? gin_history_recovery_live(r,p,100) : gin_history_recovery_page(r,p,100))==GIN_HISTORY_OK); gin_history_page_free(p);
}
static void overlap(void)
{
    struct gin_history_recovery *r=NULL; struct gin_history_page *p; const struct gin_history_page *v; const struct gin_history_row *borrow; struct gin_history_request req; struct gin_history_status s;
    CHECK(gin_history_recovery_new(&scope,40,2,100,&r)==GIN_HISTORY_OK);
    feed(r,GIN_HISTORY_LIVE,44,1,"same"); feed(r,GIN_HISTORY_LIVE,43,1,"same");
    CHECK(gin_history_recovery_next(r,100,&req)==GIN_HISTORY_OK && req.anchor==40);
    feed(r,GIN_HISTORY_REST,41,2,"same");
    CHECK(gin_history_recovery_next(r,100,&req)==GIN_HISTORY_OK && req.anchor==42);
    v=gin_history_recovery_view(r); borrow=gin_history_page_row(v,0); CHECK(borrow->id==41);
    p=page_for(&scope,GIN_HISTORY_REST,42,2,43,2,"conflict");
    CHECK(gin_history_recovery_page(r,p,200)==GIN_HISTORY_CONFLICT); gin_history_page_free(p);
    CHECK(gin_history_recovery_view(r)==v && gin_history_page_row(v,0)==borrow && strcmp(borrow->body,"same")==0);
#ifndef GIN_HISTORY_RECOVERY_REAL_PAGE
    p=page_for(&scope,GIN_HISTORY_LIVE,0,2,45,1,"same"); fail_copy=1;
    CHECK(gin_history_recovery_live(r,p,200)==GIN_HISTORY_NOMEM); fail_copy=0; gin_history_page_free(p);
    CHECK(gin_history_recovery_view(r)==v && gin_history_recovery_status(r).fetch_after==42);
#endif
    /* Failed operations do not accept their later clock sample. */
    feed(r,GIN_HISTORY_REST,43,2,"same");
    s=gin_history_recovery_status(r); CHECK(s.rows==4 && s.live_rows==2 && s.pages==2 && s.fetch_after==44);
    CHECK(gin_history_page_row(gin_history_recovery_view(r),3)->id==44);
    feed(r,GIN_HISTORY_REST,45,0,"same");
    CHECK(gin_history_recovery_status(r).state==GIN_HISTORY_VISIBLE_DRAINED);
    p=page_for(&scope,GIN_HISTORY_LIVE,0,2,45,1,"same"); CHECK(gin_history_recovery_live(r,p,100)==GIN_HISTORY_INVALID); gin_history_page_free(p);
    gin_history_recovery_free(r);
}
static void clocks_and_scope(void)
{
    struct gin_history_recovery *r=NULL; struct gin_history_page *p; struct gin_history_scope other=scope; struct gin_history_request req={GIN_HISTORY_BEFORE,99,99}; const struct gin_history_page *v; unsigned i;
    CHECK(gin_history_recovery_new(&scope,0,2,UINT64_MAX-59999,&r)==GIN_HISTORY_INVALID && !r);
    CHECK(gin_history_recovery_new(&scope,-1,2,100,&r)==GIN_HISTORY_INVALID);
    CHECK(gin_history_recovery_new(&scope,0,0,100,&r)==GIN_HISTORY_INVALID);
    CHECK(gin_history_recovery_new(&scope,0,201,100,&r)==GIN_HISTORY_INVALID);
    CHECK(gin_history_recovery_new(&scope,0,2,100,&r)==GIN_HISTORY_OK);
    CHECK(gin_history_recovery_status(r).unknown_before);
    v=gin_history_recovery_view(r);
    CHECK(gin_history_recovery_next(r,99,&req)==GIN_HISTORY_INVALID && req.cursor==GIN_HISTORY_BEFORE);
    for(i=0;i<10;i++) {
        other=scope;
        switch(i) { case 0: other.origin="https://other.test"; break; case 1: other.subject_id="11234567-89ab-cdef-0123-456789abcdef"; break; case 2: other.network_slug="net"; break; case 3: other.target="#other"; break; case 4: other.chantypes="#"; break; case 5: other.own_nick="other"; break; case 6: other.network_id=8; break; case 7: other.socket_epoch=10; break; case 8: other.role=GIN_RENDER_STATUS; break; default: other.encryption=GIN_RENDER_MODE_ENCRYPTED; break; }
#ifndef GIN_HISTORY_RECOVERY_REAL_PAGE
        p=page_for(&other,GIN_HISTORY_LIVE,0,2,1,1,"a");
#else
        /* Construct valid page first; the real constructor rejects mismatched
         * route/mode scopes itself. Epoch isolates recovery's stale check. */
        if(i!=7) continue;
        p=page_for(&other,GIN_HISTORY_LIVE,0,2,1,1,"a");
#endif
        CHECK(gin_history_recovery_live(r,p,200)==GIN_HISTORY_STALE); gin_history_page_free(p); CHECK(gin_history_recovery_view(r)==v);
    }
    p=page_for(&scope,GIN_HISTORY_REST,1,2,2,1,"a"); CHECK(gin_history_recovery_page(r,p,200)==GIN_HISTORY_INVALID); gin_history_page_free(p);
    p=page_for(&scope,GIN_HISTORY_LIVE,0,2,1,1,"a"); CHECK(gin_history_recovery_page(r,p,200)==GIN_HISTORY_INVALID);
    CHECK(gin_history_recovery_live(r,p,99)==GIN_HISTORY_INVALID); gin_history_page_free(p);
    CHECK(gin_history_recovery_next(r,60099,&req)==GIN_HISTORY_OK);
    CHECK(gin_history_recovery_next(r,60100,&req)==GIN_HISTORY_DEADLINE);
    CHECK(gin_history_recovery_status(r).state==GIN_HISTORY_INCOMPLETE_DEADLINE && gin_history_recovery_view(r)==v);
    gin_history_recovery_free(r); r=NULL;
    CHECK(gin_history_recovery_new(&scope,INT64_MAX-1,2,100,&r)==GIN_HISTORY_OK);
    feed(r,GIN_HISTORY_REST,INT64_MAX,1,"max"); CHECK(gin_history_recovery_status(r).fetch_after==INT64_MAX); gin_history_recovery_free(r);
    r=NULL; CHECK(gin_history_recovery_new(&scope,INT64_MAX,2,100,&r)==GIN_HISTORY_OK);
    feed(r,GIN_HISTORY_REST,1,0,"empty"); CHECK(gin_history_recovery_status(r).fetch_after==INT64_MAX); gin_history_recovery_free(r);
}
static void caps(void)
{
    struct gin_history_recovery *r=NULL; struct gin_history_page *p; struct gin_history_status s; const struct gin_history_page *v; unsigned i; int short_last;
    for(short_last=0;short_last<2;short_last++) {
        CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_OK);
        for(i=0;i<16;i++) feed(r,GIN_HISTORY_REST,1+(int64_t)i*200,(short_last && i==15) ? 199 : 200,"x");
        s=gin_history_recovery_status(r); CHECK(s.pages==16 && s.state==(short_last ? GIN_HISTORY_VISIBLE_DRAINED : GIN_HISTORY_INCOMPLETE_LIMIT)); CHECK(s.live_rows==0);
        gin_history_recovery_free(r); r=NULL;
    }
    CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_OK);
    for(i=0;i<512;i++) feed(r,GIN_HISTORY_LIVE,(int64_t)i+1,1,"x");
    feed(r,GIN_HISTORY_LIVE,1,1,"x"); /* duplicate does not consume live cap */
    v=gin_history_recovery_view(r); p=page_for(&scope,GIN_HISTORY_LIVE,0,200,513,1,"x"); CHECK(gin_history_recovery_live(r,p,100)==GIN_HISTORY_LIMIT); gin_history_page_free(p);
    s=gin_history_recovery_status(r); CHECK(s.live_rows==512 && s.rows==512 && s.pages==0 && s.fetch_after==0 && s.state==GIN_HISTORY_INCOMPLETE_LIMIT && gin_history_recovery_view(r)==v); gin_history_recovery_free(r); r=NULL;
    /* REST-first IDs admitted again via LIVE count once as distinct live IDs. */
    CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_OK); feed(r,GIN_HISTORY_REST,1,200,"x"); feed(r,GIN_HISTORY_LIVE,1,1,"x"); CHECK(gin_history_recovery_status(r).live_rows==1 && gin_history_recovery_status(r).rows==200); gin_history_recovery_free(r); r=NULL;
    {
        char body[GIN_HISTORY_BODY_MAX+1]; memset(body,'x',sizeof(body)-1); body[sizeof(body)-1]='\0';
        CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_OK);
        feed(r,GIN_HISTORY_REST,1,200,body); feed(r,GIN_HISTORY_REST,201,200,body);
        v=gin_history_recovery_view(r); s=gin_history_recovery_status(r); CHECK(s.owned_bytes<GIN_HISTORY_OWNED_MAX);
        p=page_for(&scope,GIN_HISTORY_REST,400,200,401,200,body); CHECK(gin_history_recovery_page(r,p,100)==GIN_HISTORY_LIMIT); gin_history_page_free(p);
        CHECK(gin_history_recovery_status(r).rows==400 && gin_history_recovery_status(r).fetch_after==400 && gin_history_recovery_status(r).pages==2 && gin_history_recovery_view(r)==v); gin_history_recovery_free(r);
    }
}
#ifdef GIN_HISTORY_RECOVERY_WHITEBOX
/* Test-only allocation fault and defensive row-cap state construction. No
 * forged state is a production API path; legal page/live budgets top out at
 * 3712 distinct IDs before the independent 4096-row guard is reachable. */
static int fail_recovery_alloc;
static void *recovery_calloc(size_t n, size_t bytes)
{
    if(fail_recovery_alloc) return NULL;
    return calloc(n,bytes);
}
#define calloc recovery_calloc
#include "../src/history_recovery.c"
#undef calloc
static void defensive_rows_and_alloc(void)
{
    struct gin_history_recovery *r=NULL; struct gin_history_page *p=NULL, *big=NULL;
    struct gin_history_row *rows; const struct gin_history_row **refs; size_t i;
    const struct gin_history_page *v;
    fail_recovery_alloc=1; CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_NOMEM && !r); fail_recovery_alloc=0;
    CHECK(gin_history_recovery_new(&scope,0,200,100,&r)==GIN_HISTORY_OK);
    rows=calloc(GIN_HISTORY_ROWS_MAX,sizeof(*rows)); refs=calloc(GIN_HISTORY_ROWS_MAX,sizeof(*refs)); CHECK(rows && refs);
    for(i=0;i<GIN_HISTORY_ROWS_MAX;i++) { struct gin_history_row row={1,10,"Net","#room","privmsg","alice","x",NULL,0}; row.id=(int64_t)i+1; rows[i]=row; refs[i]=rows+i; }
    CHECK(gin_history_copy_rows(&scope,refs,GIN_HISTORY_ROWS_MAX,GIN_HISTORY_MERGED,NULL,&big)==GIN_HISTORY_OK);
    free(rows); free(refs); gin_history_page_free(r->view); r->view=big; r->status.rows=GIN_HISTORY_ROWS_MAX; r->status.owned_bytes=gin_history_page_owned_bytes(big);
    v=r->view; p=page_for(&scope,GIN_HISTORY_LIVE,0,200,4097,1,"x");
    CHECK(gin_history_recovery_live(r,p,100)==GIN_HISTORY_LIMIT); CHECK(r->view==v && r->status.rows==4096 && r->status.fetch_after==0 && r->status.pages==0 && r->status.live_rows==0 && r->status.state==GIN_HISTORY_INCOMPLETE_LIMIT);
    gin_history_page_free(p); gin_history_recovery_free(r);
}
#endif
int main(void) {
    initial(); overlap(); clocks_and_scope(); caps();
#ifdef GIN_HISTORY_RECOVERY_WHITEBOX
    defensive_rows_and_alloc();
#endif
    puts("history recovery: PASS"); return 0;
}
