#include "account_receive.h"
#include "account_receive_hot.h"
#include "connect_hot.h"
#include "gin_naming.h"
#include "gin_command.h"
#include "connect_control.h"
#include "connect_values.h"
#include <stdlib.h>
#include <string.h>
struct gin_account_receive {
    struct gin_connect *native;
    struct gin_account_identity identity;
    char origin[sizeof(((struct gin_credential *)0)->origin)];
    char subject_id[37], subject_name[GIN_CREDENTIAL_NAME_MAX+1];
    struct gin_live_queue *queue;
    size_t topics;
    char expected[GIN_CONNECT_TOPICS_MAX][GIN_WS_TOPIC_CAP];
    int64_t receive_ms;
    enum gin_connect_code failure;
    int hot;
    char chantypes[5];
    struct {
        char network[256], target[257];
        int has_network, has_target;
    } membership[GIN_CONNECT_TOPICS_MAX];
};
static void discard(struct gin_account_receive *a)
{ while (gin_live_queue_count(a->queue)) (void)gin_live_queue_drop(a->queue); }
static int consistent(const struct gin_account_receive *a)
{
    size_t i, j;
    if (gin_connect_topic_count(a->native) != a->topics) return 0;
    for (i=0;i<a->topics;i++) {
        const struct gin_connect_topic_status *t=gin_connect_topic_at(a->native,i);
        if (!t || !t->topic || strcmp(t->topic,a->expected[i])) return 0;
        for (j=0;j<i;j++) if (!strcmp(t->topic,a->expected[j])) return 0;
    }
    return 1;
}
static void receive_frame(void *user,const struct gin_phoenix_frame *frame,uint64_t epoch)
{
    struct gin_account_receive *a=user;
    const struct gin_phoenix_fields *f=gin_phoenix_frame_fields(frame);
    size_t i;
    if (!f || !epoch || epoch!=gin_connect_last_result(a->native).socket_epoch || a->failure) return;
    for (i=0;i<a->topics;i++) {
        const struct gin_connect_topic_status *t=gin_connect_topic_at(a->native,i);
        struct gin_live_view v;
        enum gin_live_code code;
        if (!t || !t->topic || strcmp(f->topic,t->topic)) continue;
        if (strcmp(t->topic,a->expected[i]) || t->state!=GIN_CONNECT_TOPIC_JOINED ||
            !t->join_ref || !*t->join_ref || (f->join_ref && strcmp(f->join_ref,t->join_ref))) return;
        memset(&v,0,sizeof(v)); v.fields=*f; v.source_join_ref=t->join_ref;
        v.source_index=i; v.observed_epoch=epoch; v.receive_ms=a->receive_ms;
        v.kind=GIN_LIVE_UNSUPPORTED;
        code=gin_live_queue_push(a->queue,&v);
        if (code!=GIN_LIVE_OK) a->failure=code==GIN_LIVE_NOMEM?GIN_CONNECT_NOMEM:
            code==GIN_LIVE_LIMIT?GIN_CONNECT_LIMIT:GIN_CONNECT_INVALID;
        return;
    }
}
void gin_account_receive_free(struct gin_account_receive *a)
{
    if (!a) return;
    gin_connect_free(a->native); gin_live_queue_free(a->queue);
    /* Expected topics are nonsensitive; queue free wipes retained frame data. */
    free(a);
}
static enum gin_connect_code receive_new(const struct gin_connect_settings *s,
    const struct gin_credential *c,const struct gin_account_plan *p,const char *chantypes,
    struct gin_account_receive **out)
{
    struct gin_account_receive *a;
    const struct gin_connect_subscription *subs;
    size_t count, i, n;
    enum gin_connect_code code;
    enum gin_live_code q;
    if (!s || !c || !p || !out || *out) return GIN_CONNECT_INVALID;
    if (chantypes && gin_chantypes_validate(chantypes)) return GIN_CONNECT_INVALID;
    subs=gin_account_plan_subscriptions(p,&count);
    if (count>=GIN_CONNECT_TOPICS_MAX || gin_account_plan_topic_count(p)!=count+1) return GIN_CONNECT_INVALID;
    a=calloc(1,sizeof(*a)); if (!a) return GIN_CONNECT_NOMEM;
    a->hot=chantypes!=NULL;
    if (chantypes) strcpy(a->chantypes,chantypes);
    code=gin_connect_new(s,c,subs,count,receive_frame,a,&a->native);
    if (code) goto fail;
    strcpy(a->origin,c->origin); strcpy(a->subject_id,c->subject_id); strcpy(a->subject_name,c->subject_name);
    a->identity.origin=a->origin; a->identity.subject_id=a->subject_id; a->identity.subject_name=a->subject_name;
    a->topics=count+1;
    for (i=0;i<a->topics;i++) {
        if (a->hot && i) {
            if (!subs[i-1].network || strlen(subs[i-1].network)>255 ||
                (subs[i-1].target && strlen(subs[i-1].target)>256)) {
                code=GIN_CONNECT_INVALID; goto fail;
            }
            strcpy(a->membership[i].network,subs[i-1].network);
            a->membership[i].has_network=1;
            if (subs[i-1].target) {
                strcpy(a->membership[i].target,subs[i-1].target);
                a->membership[i].has_target=1;
            }
        }
        if (gin_ws_topic(c->subject_name,i?subs[i-1].network:NULL,
            i?subs[i-1].target:NULL,a->expected[i],sizeof(a->expected[i]),&n)!=GIN_WS_VALUE_OK) {
            code=GIN_CONNECT_INVALID; goto fail;
        }
    }
    if (!consistent(a)) { code=GIN_CONNECT_INVALID; goto fail; }
    q=gin_live_queue_new_sources(GIN_LIVE_ROWS_MAX,GIN_LIVE_BYTES_MAX,
        a->hot?GIN_CONNECT_TOPICS_MAX:a->topics,&a->queue);
    if (q) { code=q==GIN_LIVE_NOMEM?GIN_CONNECT_NOMEM:GIN_CONNECT_INVALID; goto fail; }
    *out=a; return GIN_CONNECT_OK;
fail:
    gin_account_receive_free(a); return code;
}
enum gin_connect_code gin_account_receive_new(const struct gin_connect_settings *s,
    const struct gin_credential *c,const struct gin_account_plan *p,struct gin_account_receive **out)
{ return receive_new(s,c,p,NULL,out); }
enum gin_connect_code gin_account_receive_new_hot(const struct gin_connect_settings *s,
    const struct gin_credential *c,const struct gin_account_plan *p,const char *chantypes,
    struct gin_account_receive **out)
{ return chantypes?receive_new(s,c,p,chantypes,out):GIN_CONNECT_INVALID; }
/* Reconstruct from owned pairs, never adopt arbitrary Native membership. */
static enum gin_connect_code refresh_hot(struct gin_account_receive *a)
{
    static const char prefix[]="grappa:user:";
    const struct gin_connect_topic_status *root=gin_connect_topic_at(a->native,0);
    const char *subject;
    char (*staged)[GIN_WS_TOPIC_CAP];
    size_t i,j,n;
    if (!root || !root->topic || strncmp(root->topic,prefix,sizeof(prefix)-1) ||
        gin_connect_topic_count(a->native)!=a->topics) return GIN_CONNECT_SCOPE;
    subject=root->topic+sizeof(prefix)-1;
    if (strlen(subject)>GIN_CREDENTIAL_NAME_MAX) return GIN_CONNECT_SCOPE;
    staged=calloc(a->topics,sizeof(*staged));
    if (!staged) return GIN_CONNECT_NOMEM;
    for (i=0;i<a->topics;i++) {
        const struct gin_connect_topic_status *t=gin_connect_topic_at(a->native,i);
        if (gin_ws_topic(subject,a->membership[i].has_network?a->membership[i].network:NULL,
            a->membership[i].has_target?a->membership[i].target:NULL,
            staged[i],sizeof(staged[i]),&n)!=GIN_WS_VALUE_OK ||
            !t || !t->topic || strcmp(t->topic,staged[i])) goto mismatch;
        for (j=0;j<i;j++) if (!strcmp(staged[j],staged[i])) goto mismatch;
    }
    strcpy(a->subject_name,subject);
    memcpy(a->expected,staged,a->topics*sizeof(*staged));
    free(staged);return GIN_CONNECT_OK;
mismatch:
    free(staged);return GIN_CONNECT_SCOPE;
}
enum gin_connect_code gin_account_receive_subscribe_query_live(struct gin_account_receive *a,
    const char *slug,const char *peer,uint64_t epoch,size_t *out)
{
    char staged[GIN_WS_TOPIC_CAP];
    unsigned char record[7+GIN_COMMAND_NICK_MAX];
    struct gin_command command;
    struct gin_connect_topic_status old[GIN_CONNECT_TOPICS_MAX];
    char refs[GIN_CONNECT_TOPICS_MAX][GIN_PHOENIX_REF_CAP];
    const struct gin_connect_topic_status *t;
    size_t i,index=0,n,found,previous;
    enum gin_connect_code code;
    if (!a || !slug || !peer || !out || !a->hot) return GIN_CONNECT_INVALID;
    if (a->failure) return a->failure;
    if (!epoch || epoch!=gin_connect_last_result(a->native).socket_epoch) return GIN_CONNECT_SCOPE;
    if (!consistent(a)) return (a->failure=GIN_CONNECT_SCOPE);
    if (strlen(slug)>255 || strlen(peer)>256) return GIN_CONNECT_LIMIT;
    n=strlen(peer);
    if (n>GIN_COMMAND_NICK_MAX) return GIN_CONNECT_LIMIT;
    memcpy(record,"/query ",7);memcpy(record+7,peer,n);
    if (gin_command_parse(record,7+n,a->chantypes,&command)!=GIN_COMMAND_OK ||
        command.kind!=GIN_COMMAND_QUERY) return GIN_CONNECT_INVALID;
    if (gin_ws_topic(a->subject_name,slug,peer,staged,sizeof(staged),&n)!=GIN_WS_VALUE_OK)
        return GIN_CONNECT_INVALID;
    previous=a->topics;found=previous;
    for (i=0;i<previous;i++) {
        t=gin_connect_topic_at(a->native,i);
        if (!strcmp(staged,a->expected[i])) found=i;
        memcpy(&old[i],t,sizeof(*t));
        if (t->join_ref && strlen(t->join_ref)>=sizeof(refs[i])) return (a->failure=GIN_CONNECT_SCOPE);
        strcpy(refs[i],t->join_ref?t->join_ref:"");
    }
    if (found==previous && previous==GIN_CONNECT_TOPICS_MAX) return GIN_CONNECT_LIMIT;
    code=gin_connect_subscribe_query_live(a->native,slug,peer,a->chantypes,epoch,&index);
    if (code) return code;
    /* Native cannot roll back: anomalies latch failure. No allocation, service
     * or callback occurs before synchronous expected-table publication. */
    if (gin_connect_last_result(a->native).socket_epoch!=epoch ||
        index!=(found==previous?previous:found) ||
        gin_connect_topic_count(a->native)!=previous+(found==previous)) goto mismatch;
    for (i=0;i<previous;i++) {
        t=gin_connect_topic_at(a->native,i);
        if (!t || t->topic!=old[i].topic || strcmp(t->topic,a->expected[i]) ||
            t->join_ref!=old[i].join_ref || t->state!=old[i].state ||
            strcmp(t->join_ref?t->join_ref:"",refs[i])) goto mismatch;
    }
    t=gin_connect_topic_at(a->native,index);
    if (!t || !t->topic || strcmp(t->topic,staged) || !t->join_ref || !*t->join_ref ||
        t->state!=(found==previous?GIN_CONNECT_TOPIC_PENDING:GIN_CONNECT_TOPIC_JOINED) ||
        gin_connect_get_state(a->native)!=(found==previous?GIN_CONNECT_JOINING:GIN_CONNECT_READY)) goto mismatch;
    if (found==previous) {
        strcpy(a->membership[index].network,slug);strcpy(a->membership[index].target,peer);
        a->membership[index].has_network=a->membership[index].has_target=1;
        strcpy(a->expected[index],staged);a->topics++;
    }
    *out=index;return GIN_CONNECT_OK;
mismatch:
    return (a->failure=GIN_CONNECT_SCOPE);
}
enum gin_connect_code gin_account_receive_start(struct gin_account_receive *a)
{
    enum gin_connect_state state;
    enum gin_connect_code code;
    if (!a) return GIN_CONNECT_INVALID;
    if (a->failure) return a->failure;
    state=gin_connect_get_state(a->native);
    if (state!=GIN_CONNECT_IDLE && state!=GIN_CONNECT_STOPPED && state!=GIN_CONNECT_FAILED) return GIN_CONNECT_INVALID;
    discard(a);
    if (!consistent(a)) return (a->failure=GIN_CONNECT_SCOPE);
    code=gin_connect_start(a->native);
    if (a->hot) {
        /* Fresh bound /me can rename retained topics before transport failure.
         * Reconstruct only from owned membership, even on a failed attempt;
         * latch reconciliation failure without masking Native's primary error. */
        enum gin_connect_code refreshed=refresh_hot(a);
        if (refreshed) a->failure=refreshed;
        if (!code) code=refreshed;
    }
    return code;
}
enum gin_connect_code gin_account_receive_service(struct gin_account_receive *a,unsigned wait,int64_t receive_ms)
{
    enum gin_connect_code code;
    if (!a) return GIN_CONNECT_INVALID;
    if (a->failure) return a->failure;
    if (!consistent(a)) { discard(a); return (a->failure=GIN_CONNECT_SCOPE); }
    if (a->hot && gin_live_queue_count(a->queue)) return GIN_CONNECT_INVALID;
    a->receive_ms=receive_ms; code=gin_connect_service(a->native,wait);
    if (a->failure) return a->failure;
    if (code!=GIN_CONNECT_OK) discard(a);
    return code;
}
const struct gin_live_view *gin_account_receive_peek(const struct gin_account_receive *a)
{
    const struct gin_live_view *v;
    enum gin_connect_state state;
    if (!a || a->failure) return NULL;
    state=gin_connect_get_state(a->native);
    if (state!=GIN_CONNECT_JOINING && state!=GIN_CONNECT_READY) return NULL;
    v=gin_live_queue_peek(a->queue);
    return v && v->observed_epoch==gin_connect_last_result(a->native).socket_epoch?v:NULL;
}
size_t gin_account_receive_count(const struct gin_account_receive *a)
{ return gin_account_receive_peek(a)?gin_live_queue_count(a->queue):0; }
enum gin_live_code gin_account_receive_drop(struct gin_account_receive *a)
{ return a?gin_live_queue_drop(a->queue):GIN_LIVE_INVALID; }
void gin_account_receive_close(struct gin_account_receive *a)
{ if (a) { gin_connect_close(a->native); discard(a); } }

const struct gin_account_identity *gin_account_receive_identity(const struct gin_account_receive *a)
{ return a?&a->identity:NULL; }
enum gin_connect_state gin_account_receive_state(const struct gin_account_receive *a)
{ return a?gin_connect_get_state(a->native):GIN_CONNECT_STOPPED; }
uint64_t gin_account_receive_epoch(const struct gin_account_receive *a)
{ enum gin_connect_state s=gin_account_receive_state(a); return a && !a->failure && (s==GIN_CONNECT_JOINING || s==GIN_CONNECT_READY)?gin_connect_last_result(a->native).socket_epoch:0; }
int gin_account_receive_drained(const struct gin_account_receive *a)
{ return a && !a->failure && gin_connect_receive_drained(a->native); }
enum gin_connect_code gin_account_receive_http_wait_budget(struct gin_account_receive *a, unsigned *wait)
{
    enum gin_connect_code code;
    if (!a || !wait) return GIN_CONNECT_INVALID;
    if (a->failure) return a->failure;
    code=gin_connect_http_wait_budget(a->native,wait);
    if (code==GIN_CONNECT_OK && gin_live_queue_count(a->queue)) *wait=0;
    return code;
}
int gin_account_receive_cleanup_failed(const struct gin_account_receive *a)
{ return a?gin_connect_cleanup_failed(a->native):0; }
int gin_account_receive_quarantined(const struct gin_account_receive *a)
{ return a?gin_connect_quarantined(a->native):0; }
enum gin_query_code gin_account_receive_open_query(struct gin_account_receive *a,
 const struct gin_topology *topology,const char *slug,const char *nick,
 const char *chantypes,enum gin_query_mode mode,uint64_t id,unsigned timeout)
{
    if (!a) return GIN_QUERY_INVALID;
    if (a->failure) return GIN_QUERY_UNAVAILABLE;
    return gin_connect_open_query(a->native,topology,slug,nick,chantypes,mode,id,timeout);
}
struct gin_query_result gin_account_receive_query_result(const struct gin_account_receive *a)
{
    struct gin_query_result r;
    memset(&r,0,sizeof(r));
    return a?gin_connect_query_result(a->native):r;
}
int gin_account_receive_query_take(struct gin_account_receive *a,struct gin_query_result *out)
{ return a && out?gin_connect_query_take(a->native,out):-1; }
const struct gin_connect_topic_status *gin_account_receive_source(const struct gin_account_receive *a,size_t i)
{ return a && !a->failure?gin_connect_topic_at(a->native,i):NULL; }
