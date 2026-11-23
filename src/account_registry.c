#include "account_registry.h"
#include "connect_values.h"
#include "gin_command.h"
#include <stdlib.h>
#include <string.h>
#define SLOTS 64
struct gin_account_registry {
    struct gin_account_registry_scope scope;
    char origin[2049], uuid[37], subject[256], chantypes[5];
    struct gin_topology_network networks[SLOTS];
    char slugs[SLOTS][256], nicks[SLOTS][257], components[SLOTS][256];
    struct gin_topology_window windows[SLOTS];
    char stamps[SLOTS][GIN_TOPOLOGY_OPENED_AT_MAX+1];
    struct gin_connect_subscription sources[SLOTS];
    char source_networks[SLOTS][256], source_targets[SLOTS][257];
    char topics[SLOTS][GIN_WS_TOPIC_CAP];
    size_t nc, wc, sc;
    uint64_t generation;
};
static int bounded(const char *s, size_t max)
{ return s && *s && strlen(s)<=max; }
static int scope_valid(const struct gin_account_registry_scope *s)
{
    return s && bounded(s->origin,2048) && bounded(s->subject_uuid,36) &&
        bounded(s->topic_subject,255) && s->chantypes && strlen(s->chantypes)<=4 &&
        !gin_chantypes_validate(s->chantypes) && s->epoch;
}
static enum gin_account_registry_code plan_code(enum gin_account_plan_code c)
{
    switch(c) {
    case GIN_ACCOUNT_PLAN_OK: return GIN_ACCOUNT_REGISTRY_OK;
    case GIN_ACCOUNT_PLAN_LIMIT: return GIN_ACCOUNT_REGISTRY_LIMIT;
    case GIN_ACCOUNT_PLAN_NOMEM: return GIN_ACCOUNT_REGISTRY_NOMEM;
    case GIN_ACCOUNT_PLAN_QUERIES_UNKNOWN: return GIN_ACCOUNT_REGISTRY_UNKNOWN;
    default: return GIN_ACCOUNT_REGISTRY_INVALID;
    }
}
static int peer_valid(const struct gin_topology_window *w, const char *chantypes)
{
    char line[136]; size_t n;
    struct gin_command command;
    if(w->role!=GIN_REMOTE_QUERY) return 1;
    n=strlen(w->name.original_wire_target);
    if(n>128) return 0;
    memcpy(line,"/query ",7); memcpy(line+7,w->name.original_wire_target,n);
    return gin_command_parse((const unsigned char *)line,n+7,chantypes,&command)==GIN_COMMAND_OK;
}
static void window_copy(struct gin_account_registry *r, const struct gin_topology_window *w)
{
    size_t i=r->wc;
    r->windows[i]=*w;
    if(w->opened_at) {
        strcpy(r->stamps[i],w->opened_at); r->windows[i].opened_at=r->stamps[i];
    }
    r->wc++;
}
static void source_copy(struct gin_account_registry *r, const struct gin_connect_subscription *s, const char *topic)
{
    size_t i=r->sc;
    if(s->network) { strcpy(r->source_networks[i],s->network); r->sources[i].network=r->source_networks[i]; }
    if(s->target) { strcpy(r->source_targets[i],s->target); r->sources[i].target=r->source_targets[i]; }
    strcpy(r->topics[i],topic); r->sc++;
}
void gin_account_registry_free(struct gin_account_registry *r) { free(r); }
const struct gin_account_registry_scope *gin_account_registry_scope(const struct gin_account_registry *r)
{ return r ? &r->scope : NULL; }
size_t gin_account_registry_network_count(const struct gin_account_registry *r) { return r?r->nc:0; }
size_t gin_account_registry_window_count(const struct gin_account_registry *r) { return r?r->wc:0; }
size_t gin_account_registry_source_count(const struct gin_account_registry *r) { return r?r->sc:0; }
uint64_t gin_account_registry_generation(const struct gin_account_registry *r) { return r?r->generation:0; }
const struct gin_topology_network *gin_account_registry_network_at(const struct gin_account_registry *r,size_t i)
{ return r && i<r->nc ? &r->networks[i] : NULL; }
const struct gin_topology_window *gin_account_registry_window_at(const struct gin_account_registry *r,size_t i)
{ return r && i<r->wc ? &r->windows[i] : NULL; }
const struct gin_connect_subscription *gin_account_registry_source_at(const struct gin_account_registry *r,size_t i)
{ return r && i<r->sc ? &r->sources[i] : NULL; }
enum gin_account_registry_code gin_account_registry_new(const struct gin_topology *t,
    const struct gin_account_registry_scope *scope, struct gin_account_registry **out)
{
    struct gin_account_plan *p=NULL;
    struct gin_account_registry *r;
    const struct gin_connect_subscription *subs;
    struct gin_connect_subscription root={NULL,NULL};
    size_t i,count,n; char topic[GIN_WS_TOPIC_CAP];
    enum gin_account_registry_code code;
    if(!out || *out || !scope_valid(scope)) return GIN_ACCOUNT_REGISTRY_INVALID;
    code=plan_code(gin_account_plan_new(t,scope->chantypes,scope->topic_subject,&p));
    if(code) return code;
    for(i=0;i<gin_account_plan_window_count(p);i++)
        if(!peer_valid(gin_account_plan_window_at(p,i),scope->chantypes)) {
            gin_account_plan_free(p); return GIN_ACCOUNT_REGISTRY_INVALID;
        }
    r=calloc(1,sizeof(*r));
    if(!r) { gin_account_plan_free(p); return GIN_ACCOUNT_REGISTRY_NOMEM; }
    strcpy(r->origin,scope->origin); strcpy(r->uuid,scope->subject_uuid);
    strcpy(r->subject,scope->topic_subject); strcpy(r->chantypes,scope->chantypes);
    r->scope.origin=r->origin; r->scope.subject_uuid=r->uuid;
    r->scope.topic_subject=r->subject; r->scope.chantypes=r->chantypes; r->scope.epoch=scope->epoch;
    for(i=0;i<gin_account_plan_network_count(p);i++) {
        const struct gin_topology_network *s=gin_account_plan_network_at(p,i);
        r->networks[i]=*s;
        strcpy(r->slugs[i],s->slug); strcpy(r->nicks[i],s->own_nick); strcpy(r->components[i],s->component);
        r->networks[i].slug=r->slugs[i]; r->networks[i].own_nick=r->nicks[i]; r->networks[i].component=r->components[i]; r->nc++;
    }
    for(i=0;i<gin_account_plan_window_count(p);i++) window_copy(r,gin_account_plan_window_at(p,i));
    (void)gin_ws_topic(r->subject,NULL,NULL,topic,sizeof(topic),&n); source_copy(r,&root,topic);
    subs=gin_account_plan_subscriptions(p,&count);
    for(i=0;i<count;i++) {
        (void)gin_ws_topic(r->subject,subs[i].network,subs[i].target,topic,sizeof(topic),&n);
        source_copy(r,&subs[i],topic);
    }
    gin_account_plan_free(p); *out=r; return GIN_ACCOUNT_REGISTRY_OK;
}
static int same_window(const struct gin_topology_window *a,const struct gin_topology_window *b)
{
    return a->network_id==b->network_id && a->role==b->role &&
        !strcmp(a->name.input_network_slug,b->name.input_network_slug) &&
        !strcmp(a->name.canonical_target,b->name.canonical_target);
}
static size_t find_window(const struct gin_account_registry *r,const struct gin_topology_window *w)
{
    size_t i;
    for(i=0;i<r->wc;i++) if(same_window(&r->windows[i],w)) break;
    return i;
}
enum gin_account_registry_code gin_account_registry_apply(struct gin_account_registry *r,
    const struct gin_topology *t,const struct gin_account_registry_scope *scope,
    struct gin_account_registry_delta *out)
{
    struct gin_account_registry *stage=NULL;
    struct gin_account_registry_delta delta;
    size_t wi[SLOTS],si[SLOTS],nw=0,ns=0,i,j;
    enum gin_account_registry_code code;
    if(!r || !t || !out || !scope_valid(scope)) return GIN_ACCOUNT_REGISTRY_INVALID;
    if(scope->epoch!=r->scope.epoch || strcmp(scope->origin,r->origin) ||
        strcmp(scope->subject_uuid,r->uuid) || strcmp(scope->topic_subject,r->subject) ||
        strcmp(scope->chantypes,r->chantypes)) return GIN_ACCOUNT_REGISTRY_SCOPE;
    code=gin_account_registry_new(t,scope,&stage);
    if(code) return code;
    code=GIN_ACCOUNT_REGISTRY_UNSUPPORTED;
    if(r->nc!=stage->nc) goto done;
    for(i=0;i<r->nc;i++) {
        const struct gin_topology_network *a=&r->networks[i],*b=&stage->networks[i];
        if(a->id!=b->id || strcmp(a->slug,b->slug) || strcmp(a->own_nick,b->own_nick)) goto done;
    }
    for(i=0;i<r->wc;i++) if(find_window(stage,&r->windows[i])==stage->wc) goto done;
    for(i=0;i<stage->wc;i++) {
        if(find_window(r,&stage->windows[i])==r->wc) {
            if(stage->windows[i].role!=GIN_REMOTE_QUERY) goto done;
            wi[nw++]=i;
        }
    }
    for(i=0;i<stage->sc;i++) {
        for(j=0;j<r->sc && strcmp(stage->topics[i],r->topics[j]);j++) { }
        if(j==r->sc) si[ns++]=i;
    }
    code=GIN_ACCOUNT_REGISTRY_LIMIT;
    if(nw>SLOTS-1-r->wc || ns>SLOTS-r->sc || ((nw || ns) && r->generation==UINT64_MAX)) goto done;
    /* All allocations, validation, capacities and overflow checks precede the
     * first write. Only previously unused slots are touched from this point. */
    for(i=0;i<nw;i++) window_copy(r,&stage->windows[wi[i]]);
    for(i=0;i<ns;i++) source_copy(r,&stage->sources[si[i]],stage->topics[si[i]]);
    if(nw || ns) r->generation++;
    delta.windows_added=nw; delta.sources_added=ns; delta.generation=r->generation;
    *out=delta; code=GIN_ACCOUNT_REGISTRY_OK;
done:
    gin_account_registry_free(stage); return code;
}
