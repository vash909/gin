#include "account_dispatch_hot.h"
#include <errno.h>
#include "history_evidence.h"
#include "connect_values.h"
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
struct gin_account_dispatch {
    struct gin_account_receive *receiver;
    const struct gin_account_plan *plan;
    const struct gin_topology *topology;
    char chantypes[5];
    enum gin_session_code failure;
    uint64_t epoch;
    struct gin_account_registry *registry;
    struct gin_account_inventory *pending, *accepted;
    uint64_t generation;
    size_t cold_windows;
    unsigned char admitted[64];
};
struct gin_account_inventory {
    struct gin_topology *topology;
    struct gin_account_identity identity;
    char origin[GIN_CREDENTIAL_ORIGIN_MAX+1], subject_id[37];
    char subject_name[GIN_CREDENTIAL_NAME_MAX+1];
    uint64_t epoch;
};
const struct gin_topology *gin_account_inventory_topology(const struct gin_account_inventory *p)
{ return p?p->topology:NULL; }
const struct gin_account_identity *gin_account_inventory_identity(const struct gin_account_inventory *p)
{ return p?&p->identity:NULL; }
uint64_t gin_account_inventory_epoch(const struct gin_account_inventory *p)
{ return p?p->epoch:0; }
void gin_account_inventory_free(struct gin_account_inventory *p)
{ if(p) { gin_topology_free(p->topology);free(p); } }
static const char *dispatch_text(json_t *o,const char *key)
{ return json_string_value(json_object_get(o,key)); }
static int reserved(json_t *o)
{
    const char *k=dispatch_text(o,"kind"), *s=dispatch_text(o,"body");
    if (k && !strncmp(k,"e2e_",4)) return 1;
    if (s) for (;*s;s++) if (*s==1 && !strncmp(s+1,"E2E",3)) return 1;
    return 0;
}
static int equivalent(const struct gin_account_plan *a,const struct gin_account_plan *b)
{
    size_t i;
    if (gin_account_plan_network_count(a)!=gin_account_plan_network_count(b) ||
        gin_account_plan_window_count(a)!=gin_account_plan_window_count(b)) return 0;
    for(i=0;i<gin_account_plan_network_count(a);i++) {
        const struct gin_topology_network *x=gin_account_plan_network_at(a,i),*y=gin_account_plan_network_at(b,i);
        if(x->id!=y->id || strcmp(x->slug,y->slug) || strcmp(x->own_nick,y->own_nick)) return 0;
    }
    for(i=0;i<gin_account_plan_window_count(a);i++) {
        const struct gin_topology_window *x=gin_account_plan_window_at(a,i),*y=gin_account_plan_window_at(b,i);
        if(x->network_id!=y->network_id || x->role!=y->role || x->joined!=y->joined || x->source!=y->source ||
            strcmp(x->name.original_wire_target,y->name.original_wire_target) || strcmp(x->name.canonical_target,y->name.canonical_target) ||
            !!x->opened_at!=!!y->opened_at || (x->opened_at && strcmp(x->opened_at,y->opened_at))) return 0;
    }
    return 1;
}
void gin_account_dispatch_free(struct gin_account_dispatch *d)
{ if(d) { gin_account_inventory_free(d->accepted);free(d); } }
enum gin_session_code gin_account_dispatch_new(struct gin_account_receive *r,
    const struct gin_account_plan *p,const struct gin_topology *t,const char *ct,
    enum gin_render_encryption policy,struct gin_account_dispatch **out)
{
    struct gin_account_dispatch *d; struct gin_account_plan *check=NULL;
    const struct gin_account_identity *id=gin_account_receive_identity(r);
    int ok;
    if(!r || !p || !t || !ct || !out || *out || !id || strlen(ct)>4) return GIN_SESSION_INVALID;
    if(policy!=GIN_RENDER_MODE_PLAIN) return GIN_SESSION_UNSUPPORTED;
    if(gin_account_plan_new(t,ct,id->subject_name,&check)!=GIN_ACCOUNT_PLAN_OK) return GIN_SESSION_ROUTE;
    ok=equivalent(p,check);gin_account_plan_free(check);if(!ok) return GIN_SESSION_ROUTE;
    d=calloc(1,sizeof(*d));if(!d) return GIN_SESSION_NOMEM;
    d->receiver=r;d->plan=p;d->topology=t;strcpy(d->chantypes,ct);*out=d;return GIN_SESSION_OK;
}
static int scope_equal(const struct gin_account_registry_scope *a,const struct gin_account_registry_scope *b)
{
    return a && b && a->origin && b->origin && a->subject_uuid && b->subject_uuid &&
        a->topic_subject && b->topic_subject && a->chantypes && b->chantypes &&
        a->epoch==b->epoch && !strcmp(a->origin,b->origin) &&
        !strcmp(a->subject_uuid,b->subject_uuid) && !strcmp(a->topic_subject,b->topic_subject) && !strcmp(a->chantypes,b->chantypes);
}
static int hot_binding(const struct gin_account_dispatch *d)
{
    const struct gin_account_identity *id;
    const struct gin_account_registry_scope *s;
    enum gin_connect_state state;
    if(!d || !d->registry || d->failure || !d->epoch ||
        gin_account_registry_generation(d->registry)!=d->generation) return 0;
    s=gin_account_registry_scope(d->registry);id=gin_account_receive_identity(d->receiver);
    state=gin_account_receive_state(d->receiver);
    return id && s && id->origin && id->subject_id && id->subject_name &&
        (state==GIN_CONNECT_READY || state==GIN_CONNECT_JOINING) &&
        gin_account_receive_epoch(d->receiver)==d->epoch && s->epoch==d->epoch &&
        !strcmp(s->origin,id->origin) && !strcmp(s->subject_uuid,id->subject_id) &&
        !strcmp(s->topic_subject,id->subject_name) && !strcmp(s->chantypes,d->chantypes);
}
static int joined_source(const struct gin_account_dispatch *d,size_t i)
{
    const struct gin_connect_subscription *sub=gin_account_registry_source_at(d->registry,i);
    const struct gin_connect_topic_status *source=gin_account_receive_source(d->receiver,i);
    const struct gin_account_registry_scope *s=gin_account_registry_scope(d->registry);
    char topic[GIN_WS_TOPIC_CAP];size_t length;
    return sub && source && source->state==GIN_CONNECT_TOPIC_JOINED && source->join_ref && *source->join_ref &&
        gin_ws_topic(s->topic_subject,sub->network,sub->target,topic,sizeof(topic),&length)==GIN_WS_VALUE_OK && !strcmp(topic,source->topic);
}
struct gin_account_dispatch_hot_result gin_account_dispatch_hot_status(const struct gin_account_dispatch *d,
    const struct gin_account_receive *r,const struct gin_account_registry *registry,
    const struct gin_account_registry_scope *scope,uint64_t generation,size_t index)
{
    int saved=errno;
    struct gin_account_dispatch_hot_result result={GIN_SESSION_ROUTE,0,0,0};
    if(d && d->registry) {
        result.generation=d->generation;result.proposal_pending=d->pending!=NULL;
        if(d->receiver==r && d->registry==registry && hot_binding(d) && generation==d->generation &&
            scope_equal(scope,gin_account_registry_scope(registry)) &&
            (index==SIZE_MAX || index<gin_account_registry_window_count(registry))) {
            result.code=GIN_SESSION_OK;
            if(index!=SIZE_MAX) result.window_admitted=d->admitted[index]!=0;
        } else if(d->failure) result.code=d->failure;
    }
    errno=saved;return result;
}
const struct gin_topology *gin_account_dispatch_hot_topology(const struct gin_account_dispatch *d)
{ int saved=errno;const struct gin_topology *t=hot_binding(d)?d->topology:NULL;errno=saved;return t; }
enum gin_session_code gin_account_dispatch_new_hot(struct gin_account_receive *r,
    const struct gin_account_plan *p,const struct gin_topology *t,struct gin_account_registry *registry,
    const char *ct,enum gin_render_encryption policy,struct gin_account_dispatch **out)
{
    struct gin_account_dispatch *d=NULL;size_t i,n;const struct gin_connect_subscription *subs;
    enum gin_session_code code;
    if(!registry || !out || *out) return GIN_SESSION_INVALID;
    if(gin_account_registry_generation(registry)) return GIN_SESSION_ROUTE;
    code=gin_account_dispatch_new(r,p,t,ct,policy,&d);if(code) return code;
    d->registry=registry;d->epoch=gin_account_receive_epoch(r);
    if(!hot_binding(d) || gin_account_registry_network_count(registry)!=gin_account_plan_network_count(p) ||
        gin_account_registry_window_count(registry)!=gin_account_plan_window_count(p)) goto bad;
    for(i=0;i<gin_account_plan_network_count(p);i++) {
        const struct gin_topology_network *a=gin_account_plan_network_at(p,i),*b=gin_account_registry_network_at(registry,i);
        if(a->id!=b->id || strcmp(a->slug,b->slug) || strcmp(a->own_nick,b->own_nick) || strcmp(a->component,b->component)) goto bad;
    }
    for(i=0;i<gin_account_plan_window_count(p);i++) {
        const struct gin_topology_window *a=gin_account_plan_window_at(p,i),*b=gin_account_registry_window_at(registry,i);
        if(a->network_id!=b->network_id || a->role!=b->role || a->joined!=b->joined || a->source!=b->source ||
            strcmp(a->name.original_wire_target,b->name.original_wire_target) || strcmp(a->name.canonical_target,b->name.canonical_target) ||
            !!a->opened_at!=!!b->opened_at || (a->opened_at && strcmp(a->opened_at,b->opened_at))) goto bad;
        d->admitted[i]=1;
    }
    subs=gin_account_plan_subscriptions(p,&n);
    if(n+1!=gin_account_registry_source_count(registry)) goto bad;
    for(i=0;i<=n;i++) {
        const struct gin_connect_subscription *b=gin_account_registry_source_at(registry,i);
        if(!joined_source(d,i) || (i && (!b->network || strcmp(b->network,subs[i-1].network) || !!b->target!=!!subs[i-1].target || (b->target && strcmp(b->target,subs[i-1].target)))) ||
            (!i && (b->network || b->target))) goto bad;
    }
    d->cold_windows=gin_account_registry_window_count(registry);*out=d;return GIN_SESSION_OK;
bad:
    gin_account_dispatch_free(d);return GIN_SESSION_ROUTE;
}
enum gin_session_code gin_account_dispatch_resolve_inventory(struct gin_account_dispatch *d,
    struct gin_account_inventory **proposal,uint64_t generation,struct gin_account_registry_delta *out)
{
    struct gin_account_inventory *old;struct gin_account_registry_scope s;
    enum gin_account_registry_code code;
    if(!d || !proposal || !*proposal || !out) return GIN_SESSION_INVALID;
    if(!hot_binding(d) || generation!=d->generation || *proposal!=d->pending) return GIN_SESSION_ROUTE;
    s.origin=(*proposal)->identity.origin;s.subject_uuid=(*proposal)->identity.subject_id;
    s.topic_subject=(*proposal)->identity.subject_name;s.chantypes=d->chantypes;s.epoch=(*proposal)->epoch;
    if(!scope_equal(&s,gin_account_registry_scope(d->registry))) return GIN_SESSION_ROUTE;
    code=gin_account_registry_apply(d->registry,(*proposal)->topology,&s,out);
    if(code!=GIN_ACCOUNT_REGISTRY_OK) return code==GIN_ACCOUNT_REGISTRY_NOMEM?GIN_SESSION_NOMEM:GIN_SESSION_ROUTE;
    old=d->accepted;d->accepted=*proposal;d->topology=d->accepted->topology;
    d->generation=out->generation;d->pending=NULL;*proposal=NULL;
    gin_account_inventory_free(old);return GIN_SESSION_OK;
}
enum gin_session_code gin_account_dispatch_admit_window(struct gin_account_dispatch *d,
    const struct gin_account_registry_scope *scope,uint64_t generation,size_t index)
{
    const struct gin_topology_window *w;const struct gin_topology_network *net=NULL;
    size_t i;char topic[GIN_WS_TOPIC_CAP];size_t length;
    struct gin_account_dispatch_hot_result result=gin_account_dispatch_hot_status(d,d?d->receiver:NULL,d?d->registry:NULL,scope,generation,index);
    if(result.code!=GIN_SESSION_OK || index==SIZE_MAX || d->pending) return GIN_SESSION_ROUTE;
    w=gin_account_registry_window_at(d->registry,index);
    if(index>=d->cold_windows && w->role!=GIN_REMOTE_QUERY) return GIN_SESSION_ROUTE;
    for(i=0;i<gin_account_registry_network_count(d->registry);i++) {
        net=gin_account_registry_network_at(d->registry,i);if(net->id==w->network_id) break;
    }
    if(i==gin_account_registry_network_count(d->registry) ||
        gin_ws_topic(scope->topic_subject,net->slug,w->name.original_wire_target,topic,sizeof(topic),&length)!=GIN_WS_VALUE_OK) return GIN_SESSION_ROUTE;
    for(i=0;i<gin_account_registry_source_count(d->registry);i++) {
        const struct gin_connect_topic_status *source=gin_account_receive_source(d->receiver,i);
        if(source && !strcmp(source->topic,topic) && joined_source(d,i)) break;
    }
    if(i==gin_account_registry_source_count(d->registry)) return GIN_SESSION_ROUTE;
    /* Retained cold membership must still map exactly at its stable indices. */
    { size_t n;gin_account_plan_subscriptions(d->plan,&n);
      for(i=0;i<=n;i++) if(!joined_source(d,i)) return GIN_SESSION_ROUTE; }
    d->admitted[index]=1;return GIN_SESSION_OK;
}
static size_t dispatch_network_count(const struct gin_account_dispatch *d)
{ return d->registry?gin_account_registry_network_count(d->registry):gin_account_plan_network_count(d->plan); }
static size_t dispatch_window_count(const struct gin_account_dispatch *d)
{ return d->registry?gin_account_registry_window_count(d->registry):gin_account_plan_window_count(d->plan); }
static const struct gin_topology_network *dispatch_network_at(const struct gin_account_dispatch *d,size_t i)
{ return d->registry?gin_account_registry_network_at(d->registry,i):gin_account_plan_network_at(d->plan,i); }
static const struct gin_topology_window *dispatch_window_at(const struct gin_account_dispatch *d,size_t i)
{ return d->registry?gin_account_registry_window_at(d->registry,i):gin_account_plan_window_at(d->plan,i); }
static enum gin_session_code dispatch_next(struct gin_account_dispatch *d,
    struct gin_account_destination *destination,struct gin_render_record **record,struct gin_history_page **history,
    struct gin_account_inventory **fresh_out)
{
    const struct gin_live_view *v;
    const struct gin_account_identity *id;
    const struct gin_connect_topic_status *source;
    const struct gin_connect_subscription *subs;
    const struct gin_topology_network *net=NULL;
    const struct gin_topology_window *window=NULL;
    struct gin_account_destination selected={0,SIZE_MAX,GIN_RENDER_STATUS,0};
    struct gin_session_settings settings={0};struct gin_live_projection context={0};
    struct gin_history_scope scope={0};struct gin_topology *next=NULL;
    struct gin_render_record *temporary=NULL;struct gin_history_page *page=NULL;
    struct gin_account_inventory *proposal=NULL;
    json_error_t error={0};json_t *o=NULL,*row;
    const char *kind,*network,*target,*peer,*source_target=NULL;
    struct gin_name name;char topic[GIN_WS_TOPIC_CAP];size_t count,i,length;int terminal=0, lifecycle=0;
    uint64_t epoch;enum gin_session_code code=GIN_SESSION_ROUTE;
    if(!d || !destination || !record || *record || !history || *history) return GIN_SESSION_INVALID;
    if(d->failure) return d->failure;
    if(d->registry) {
        struct gin_account_dispatch_hot_result status=gin_account_dispatch_hot_status(d,d->receiver,d->registry,gin_account_registry_scope(d->registry),d->generation,SIZE_MAX);
        if(status.code!=GIN_SESSION_OK) return status.code;
        if(d->pending) return GIN_SESSION_ROUTE;
    }
    v=gin_account_receive_peek(d->receiver);if(!v) return GIN_SESSION_OK;
    id=gin_account_receive_identity(d->receiver);epoch=gin_account_receive_epoch(d->receiver);
    source=gin_account_receive_source(d->receiver,v->source_index);
    subs=gin_account_plan_subscriptions(d->plan,&count);
    if(d->registry) count=gin_account_registry_source_count(d->registry)-1;
    if(!epoch || (d->epoch && d->epoch!=epoch) || v->observed_epoch!=epoch || !source ||
        source->state!=GIN_CONNECT_TOPIC_JOINED || !source->join_ref || !v->source_join_ref ||
        strcmp(source->join_ref,v->source_join_ref) || strcmp(source->topic,v->fields.topic) ||
        (v->fields.join_ref && strcmp(v->fields.join_ref,source->join_ref)) || v->source_index>count) goto done;
    d->epoch=epoch;
    if(v->source_index) source_target=d->registry?gin_account_registry_source_at(d->registry,v->source_index)->target:subs[v->source_index-1].target;
    if(gin_ws_topic(id->subject_name,v->source_index?(d->registry?gin_account_registry_source_at(d->registry,v->source_index)->network:subs[v->source_index-1].network):NULL,
        source_target,topic,sizeof(topic),&length)!=GIN_WS_VALUE_OK || strcmp(topic,source->topic)) goto done;
    o=json_loadb((const char *)v->fields.payload.data,v->fields.payload.length,JSON_REJECT_DUPLICATES,&error);
    /* This exact routing DOM must be attested BEFORE any consumed value. */
    if(!o || !gin_history_evidence(v->fields.payload.data,v->fields.payload.length,o)) goto done;
    kind=dispatch_text(o,"kind");row=json_object_get(o,"message");
    if(!kind) goto done;
    if(reserved(o) || reserved(row)) {code=GIN_SESSION_UNSUPPORTED;goto done;}
    if(strcmp(v->fields.event,"event")) {code=GIN_SESSION_OK;goto done;}
    if(!strcmp(kind,"query_windows_list")) {
        struct gin_account_plan *candidate=NULL;
        if(d->registry && !fresh_out) goto done;
        if(v->source_index || gin_topology_apply_queries(d->topology,v->fields.payload.data,v->fields.payload.length,&next)!=GIN_TOPOLOGY_OK) goto done;
        if(gin_account_plan_new(next,d->chantypes,id->subject_name,&candidate)==GIN_ACCOUNT_PLAN_OK &&
            (fresh_out || equivalent(d->plan,candidate))) code=GIN_SESSION_OK;
        if(code==GIN_SESSION_OK && fresh_out) {
            if(!id->origin || !id->subject_id || !id->subject_name ||
                strnlen(id->origin,GIN_CREDENTIAL_ORIGIN_MAX+1)>GIN_CREDENTIAL_ORIGIN_MAX ||
                strnlen(id->subject_id,37)!=36 ||
                strnlen(id->subject_name,GIN_CREDENTIAL_NAME_MAX+1)>GIN_CREDENTIAL_NAME_MAX) code=GIN_SESSION_ROUTE;
            else {
                proposal=calloc(1,sizeof(*proposal));
                if(!proposal) code=GIN_SESSION_NOMEM;
                else {
                    strcpy(proposal->origin,id->origin);strcpy(proposal->subject_id,id->subject_id);
                    strcpy(proposal->subject_name,id->subject_name);
                    proposal->identity.origin=proposal->origin;proposal->identity.subject_id=proposal->subject_id;
                    proposal->identity.subject_name=proposal->subject_name;proposal->epoch=epoch;
                    proposal->topology=next;next=NULL;
                }
            }
        }
        gin_account_plan_free(candidate);goto done;
    }
    if(!strcmp(kind,"own_nick_changed")) {
        json_t *nid=json_object_get(o,"network_id");const char *nick=dispatch_text(o,"nick");
        if(v->source_index || !json_is_integer(nid) || !nick) goto done;
        net=gin_topology_find_id(d->topology,json_integer_value(nid));
        if(net && !strcmp(net->own_nick,nick)) code=GIN_SESSION_OK;
        goto done;
    }
    lifecycle=!strcmp(kind,"joined") || !strcmp(kind,"window_pending") ||
        !strcmp(kind,"window_invited") || !strcmp(kind,"window_invite_declined") ||
        !strcmp(kind,"join_failed") || !strcmp(kind,"kicked");
    if(lifecycle) {
        const char *state=dispatch_text(o,"state"), *expected=NULL;
        const char *details[]={"by","reason","inviter"};
        json_t *numeric=json_object_get(o,"numeric");
        network=dispatch_text(o,"network");target=dispatch_text(o,"channel");
        /* Only the authenticated joined root publisher is accepted here. */
        if(v->source_index || !network || !target ||
            gin_name_prepare(network,target,d->chantypes,&name)<0) goto done;
        for(i=0;i<dispatch_network_count(d);i++) {
            net=dispatch_network_at(d,i);if(!strcmp(net->slug,network)) break;
        }
        if(i==dispatch_network_count(d)) goto done;
        selected.network_index=i;
        if(!strcmp(kind,"joined")) expected="joined";
        else if(!strcmp(kind,"window_pending")) expected="pending";
        else if(!strcmp(kind,"window_invited")) expected="invited";
        else if(!strcmp(kind,"join_failed")) expected="failed";
        else if(!strcmp(kind,"kicked")) expected="kicked";
        if(expected && (!state || strcmp(state,expected))) goto done;
        for(i=0;i<sizeof(details)/sizeof(details[0]);i++) {
            json_t *detail=json_object_get(o,details[i]);
            if(detail && !json_is_null(detail) && !json_is_string(detail)) goto done;
        }
        if(numeric && !json_is_null(numeric) &&
            (!json_is_integer(numeric) || json_integer_value(numeric)<=0)) goto done;
        for(i=0;i<dispatch_window_count(d);i++) {
            window=dispatch_window_at(d,i);
            if(window->network_id==net->id && !strcmp(window->name.canonical_target,name.canonical_target)) break;
        }
        if(i==dispatch_window_count(d)) {code=GIN_SESSION_OK;goto done;}
        selected.window_index=i;selected.role=window->role==GIN_REMOTE_QUERY?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
        goto project_selected;
    }
    if(strcmp(kind,"message")) {code=GIN_SESSION_OK;goto done;}
    kind=dispatch_text(row,"kind");network=dispatch_text(row,"network");target=dispatch_text(row,"channel");peer=dispatch_text(row,"dm_with");
    if(!kind || !network || !target || !v->source_index || !source_target ||
        strcmp(d->registry?gin_account_registry_source_at(d->registry,v->source_index)->network:subs[v->source_index-1].network,network)) goto done;
    if(gin_name_prepare(network,target,d->chantypes,&name)<0 ||
        gin_ws_topic(id->subject_name,network,target,topic,sizeof(topic),&length)!=GIN_WS_VALUE_OK || strcmp(topic,source->topic)) goto done;
    for(i=0;i<dispatch_network_count(d);i++) {
        net=dispatch_network_at(d,i);if(!strcmp(net->slug,network)) break;
    }
    if(i==dispatch_network_count(d)) goto done;
    selected.network_index=i;
    if(!strcmp(target,"$server")) {
        if(strcmp(source_target,"$server") || (json_object_get(row,"dm_with") && !json_is_null(json_object_get(row,"dm_with")))) goto done;
    } else {
        if(peer) {
            struct gin_name source_name, own_name;
            const struct gin_topology_window *source_window;
            if(gin_name_prepare(network,source_target,d->chantypes,&source_name)<0 ||
                gin_name_prepare(network,net->own_nick,d->chantypes,&own_name)<0 ||
                gin_name_prepare(network,peer,d->chantypes,&name)<0) goto done;
            source_window=gin_topology_find_window(d->topology,net->id,source_name.canonical_target);
            /* The legacy projector trusts the owner's source role. Channels
             * cannot become DMs; own-nick transport may address a known peer. */
            if(source_window && source_window->role==GIN_REMOTE_CHANNEL) goto done;
            if(strcmp(source_name.canonical_target,own_name.canonical_target) &&
                (!source_window || source_window->role!=GIN_REMOTE_QUERY ||
                 strcmp(source_name.canonical_target,name.canonical_target))) goto done;
        }
        for(i=0;i<dispatch_window_count(d);i++) {
            window=dispatch_window_at(d,i);
            if(window->network_id==net->id && !strcmp(window->name.canonical_target,name.canonical_target)) break;
        }
        if(i==dispatch_window_count(d) || (peer?window->role!=GIN_REMOTE_QUERY:window->role!=GIN_REMOTE_CHANNEL) ||
            (!peer && json_object_get(row,"dm_with") && !json_is_null(json_object_get(row,"dm_with")))) goto done;
        selected.window_index=i;selected.role=peer?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
    }
project_selected:
    if(d->registry && selected.window_index!=SIZE_MAX && !d->admitted[selected.window_index]) goto done;
    strcpy(settings.origin,id->origin);strcpy(settings.local.network,net->slug);strcpy(settings.local.chantypes,d->chantypes);
    strcpy(settings.local.target,window && selected.role!=GIN_RENDER_STATUS?window->name.original_wire_target:"$server");strcpy(settings.mode,"plain");
    context.settings=&settings;context.subject=id->subject_name;context.own_nick=net->own_nick;
    context.source_target=source_target;context.network_id=net->id;context.query=selected.role==GIN_RENDER_QUERY;
    scope.origin=id->origin;scope.subject_id=id->subject_id;scope.network_slug=net->slug;scope.target=settings.local.target;
    scope.chantypes=d->chantypes;scope.own_nick=net->own_nick;scope.network_id=net->id;scope.socket_epoch=epoch;
    scope.role=selected.role;scope.encryption=GIN_RENDER_MODE_PLAIN;
    if(selected.role==GIN_RENDER_STATUS) code=gin_live_project_status_history(v,&context,d->topology,&scope,&temporary,&page);
    else code=gin_live_project_history(v,&context,d->topology,&next,&temporary,&terminal,&scope,&page);
    if(next) code=GIN_SESSION_ROUTE;
    selected.terminal=terminal;
done:
    json_decref(o);gin_topology_free(next);
    if(code==GIN_SESSION_OK && gin_account_receive_drop(d->receiver)!=GIN_LIVE_OK) code=GIN_SESSION_ROUTE;
    if(code==GIN_SESSION_OK && proposal) { if(d->registry) d->pending=proposal;*fresh_out=proposal;proposal=NULL; }
    gin_account_inventory_free(proposal);
    if(code==GIN_SESSION_OK) { if(temporary) *destination=selected;*record=temporary;*history=page;if(terminal) d->failure=GIN_SESSION_ROUTE; }
    else {gin_render_record_free(temporary);gin_history_page_free(page);d->failure=code;}
    return code;
}
enum gin_session_code gin_account_dispatch_next(struct gin_account_dispatch *d,
    struct gin_account_destination *destination,struct gin_render_record **record,struct gin_history_page **history)
{ return dispatch_next(d,destination,record,history,NULL); }
enum gin_session_code gin_account_dispatch_next_inventory(struct gin_account_dispatch *d,
    struct gin_account_destination *destination,struct gin_render_record **record,struct gin_history_page **history,
    struct gin_account_inventory **fresh_out)
{
    if(!fresh_out || *fresh_out) return GIN_SESSION_INVALID;
    return dispatch_next(d,destination,record,history,fresh_out);
}
