/* Private forwarding probes, not transport quarantine/authentication proof. */
#include "account_receive.h"
#include "connect_control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static struct gin_connect *expected;
static unsigned calls, incoming, reduced;
static enum gin_connect_code answer;
static int cleanup_flag, quarantine_flag;
static enum gin_connect_code probe_budget(struct gin_connect *n, unsigned *w)
{ R(n==expected); calls++; incoming=*w; if (answer==GIN_CONNECT_OK) *w=reduced; return answer; }
static int probe_cleanup(const struct gin_connect *n)
{ R(n==expected); return cleanup_flag; }
static int probe_quarantine(const struct gin_connect *n)
{ R(n==expected); return quarantine_flag; }
static unsigned query_calls, result_calls, take_calls;
static const struct gin_topology *query_topology;
static const char *query_slug, *query_nick, *query_chantypes;
static struct gin_query_result query_answer;
static int take_answer;
static enum gin_query_code probe_query(struct gin_connect *n,
 const struct gin_topology *t,const char *slug,const char *nick,const char *chantypes,
 enum gin_query_mode mode,uint64_t id,unsigned timeout)
{
 R(n==expected && t==query_topology && slug==query_slug && nick==query_nick && chantypes==query_chantypes);
 R(mode==GIN_QUERY_MODE_PLAIN && id==17 && timeout==5000); query_calls++;return GIN_QUERY_BUSY;
}
static struct gin_query_result probe_result(const struct gin_connect *n)
{ R(n==expected);result_calls++;return query_answer; }
static int probe_take(struct gin_connect *n,struct gin_query_result *out)
{ R(n==expected);take_calls++;if(take_answer==1) {*out=query_answer;memset(&query_answer,0,sizeof(query_answer));}return take_answer; }
#define gin_connect_open_query probe_query
#define gin_connect_query_result probe_result
#define gin_connect_query_take probe_take
#define gin_connect_http_wait_budget probe_budget
#define gin_connect_cleanup_failed probe_cleanup
#define gin_connect_quarantined probe_quarantine
#include "../src/account_receive.c"
#undef gin_connect_http_wait_budget
#undef gin_connect_cleanup_failed
#undef gin_connect_quarantined
#undef gin_connect_open_query
#undef gin_connect_query_result
#undef gin_connect_query_take
int main(void)
{
    struct gin_connect_settings s={0}; struct gin_credential c={0};
    struct gin_topology *b=NULL,*t=NULL; struct gin_account_plan *p=NULL;
    struct gin_account_receive *a=NULL; struct gin_account_receive before;
    struct gin_live_view v={0}; const struct gin_live_view *head;
    unsigned w; size_t bytes; int i,j;
    const char boot[]="{\"networks\":[],\"channels\":{},\"heads\":{}}";
    const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{}}";
    w=37; R(gin_account_receive_http_wait_budget(NULL,&w)==GIN_CONNECT_INVALID && w==37);
    R(gin_account_receive_http_wait_budget(NULL,NULL)==GIN_CONNECT_INVALID);
    R(!gin_account_receive_cleanup_failed(NULL) && !gin_account_receive_quarantined(NULL));
    strcpy(c.origin,"https://127.0.0.1:1"); strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");
    strcpy(c.subject_name,"Alice"); strcpy(c.token,"fixture-token.DO-NOT-LOG");
    s.http.origin=c.origin; s.http.connect_ms=700; s.http.total_ms=1500;
    s.policy.client_protocol=4; s.policy.required_server_protocol=4; s.join_ms=1500;
    R(gin_http_global_init()==GIN_HTTP_OK);
    R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
    R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
    R(gin_account_plan_new(t,"#&","Alice",&p)==GIN_ACCOUNT_PLAN_OK);
    R(gin_account_receive_new(&s,&c,p,&a)==GIN_CONNECT_OK); expected=a->native; R(expected);
    {
        struct gin_query_result r;
        unsigned char original[sizeof(r)];
        query_topology=t;query_slug="one";query_nick="NewPeer";query_chantypes="#&";
        R(gin_account_receive_open_query(NULL,t,query_slug,query_nick,query_chantypes,GIN_QUERY_MODE_PLAIN,17,5000)==GIN_QUERY_INVALID);
        R(gin_account_receive_query_result(NULL).outcome==GIN_QUERY_NONE);
        R(gin_account_receive_query_take(NULL,&r)==-1);
        R(gin_account_receive_query_take(a,NULL)==-1 && !take_calls);
        R(gin_account_receive_open_query(a,t,query_slug,query_nick,query_chantypes,GIN_QUERY_MODE_PLAIN,17,5000)==GIN_QUERY_BUSY && query_calls==1);
        for(i=GIN_CONNECT_INVALID;i<=GIN_CONNECT_CLOCK;i++) {
            a->failure=(enum gin_connect_code)i;
            R(gin_account_receive_open_query(a,t,query_slug,query_nick,query_chantypes,GIN_QUERY_MODE_PLAIN,17,5000)==GIN_QUERY_UNAVAILABLE && query_calls==1);
        }
        query_answer.operation_id=17;query_answer.outcome=GIN_QUERY_ACCEPTED;
        r=gin_account_receive_query_result(a);R(r.operation_id==17 && r.outcome==GIN_QUERY_ACCEPTED && result_calls==1);
        memset(&r,0xa5,sizeof(r));memcpy(original,&r,sizeof(r));
        for(take_answer=-1;take_answer<=0;take_answer++) {
            R(gin_account_receive_query_take(a,&r)==take_answer && !memcmp(original,&r,sizeof(r)));
        }
        R(gin_account_receive_query_result(a).operation_id==17);
        take_answer=1;R(gin_account_receive_query_take(a,&r)==1 && r.operation_id==17 && r.outcome==GIN_QUERY_ACCEPTED);
        R(gin_account_receive_query_result(a).outcome==GIN_QUERY_NONE);
        a->failure=GIN_CONNECT_OK;
    }
    gin_account_plan_free(p); gin_topology_free(t); gin_topology_free(b);
    before=*a;
    R(gin_account_receive_http_wait_budget(a,NULL)==GIN_CONNECT_INVALID && calls==0);
    answer=GIN_CONNECT_OK; reduced=13; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_OK && w==13 && incoming==37 && calls==1);
    R(!memcmp(&before,a,sizeof(before)) && gin_connect_get_state(expected)==GIN_CONNECT_IDLE);
    reduced=0; w=0; R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_OK && !w && incoming==0 && calls==2);
    answer=GIN_CONNECT_CLOSED; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_CLOSED && w==37 && calls==3);
    v.fields.topic="grappa:user:Alice"; v.fields.event="admitted";
    v.fields.payload.data=(const unsigned char *)"{}"; v.fields.payload.length=2;
    v.source_join_ref="1"; v.observed_epoch=1; v.kind=GIN_LIVE_UNSUPPORTED;
    R(gin_live_queue_push(a->queue,&v)==GIN_LIVE_OK); head=gin_live_queue_peek(a->queue); bytes=gin_live_queue_bytes(a->queue);
    /* Public peek is hidden in IDLE: use the actual owned raw queue. */
    R(!gin_account_receive_peek(a)); answer=GIN_CONNECT_OK; reduced=13; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_OK && w==0 && calls==4);
    R(gin_live_queue_peek(a->queue)==head && gin_live_queue_count(a->queue)==1 && gin_live_queue_bytes(a->queue)==bytes);
    answer=GIN_CONNECT_CLOSED; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_CLOSED && w==37 && calls==5);
    a->failure=GIN_CONNECT_LIMIT; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_LIMIT && w==37 && calls==5);
    a->failure=GIN_CONNECT_NOMEM; w=37;
    R(gin_account_receive_http_wait_budget(a,&w)==GIN_CONNECT_NOMEM && w==37 && calls==5);
    R(gin_account_receive_http_wait_budget(a,NULL)==GIN_CONNECT_INVALID && calls==5);
    for(i=0;i<2;i++) for(j=0;j<2;j++) {
        cleanup_flag=i; quarantine_flag=j; before=*a;
        R(gin_account_receive_cleanup_failed(a)==i && gin_account_receive_quarantined(a)==j);
        R(gin_account_receive_cleanup_failed(a)==i && gin_account_receive_quarantined(a)==j);
        R(!memcmp(&before,a,sizeof(before)));
    }
    /* Probes never mutate Native; real close/free see only the real owned ptr. */
    cleanup_flag=quarantine_flag=0; a->failure=GIN_CONNECT_OK;
    gin_account_receive_close(a); before=*a; cleanup_flag=quarantine_flag=1;
    query_answer.operation_id=19;query_answer.outcome=GIN_QUERY_UNCERTAIN;
    a->failure=GIN_CONNECT_LIMIT;
    R(gin_account_receive_query_result(a).operation_id==19);
    {
        struct gin_query_result r;
        take_answer=1;R(gin_account_receive_query_take(a,&r)==1 && r.operation_id==19 && r.outcome==GIN_QUERY_UNCERTAIN);
    }
    a->failure=GIN_CONNECT_OK;
    R(gin_account_receive_cleanup_failed(a)==1 && gin_account_receive_quarantined(a)==1);
    R(!memcmp(&before,a,sizeof(before))); cleanup_flag=quarantine_flag=0;
    gin_account_receive_free(a); gin_http_global_cleanup();
    puts("PASS private facade forwarding; no actual transport quarantine claim"); return 0;
}
