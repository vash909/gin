#include "account_dispatch.h"
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
void gin_account_dispatch_free(struct gin_account_dispatch *d) { free(d); }
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
    v=gin_account_receive_peek(d->receiver);if(!v) return GIN_SESSION_OK;
    id=gin_account_receive_identity(d->receiver);epoch=gin_account_receive_epoch(d->receiver);
    source=gin_account_receive_source(d->receiver,v->source_index);
    subs=gin_account_plan_subscriptions(d->plan,&count);
    if(!epoch || (d->epoch && d->epoch!=epoch) || v->observed_epoch!=epoch || !source ||
        source->state!=GIN_CONNECT_TOPIC_JOINED || !source->join_ref || !v->source_join_ref ||
        strcmp(source->join_ref,v->source_join_ref) || strcmp(source->topic,v->fields.topic) ||
        (v->fields.join_ref && strcmp(v->fields.join_ref,source->join_ref)) || v->source_index>count) goto done;
    d->epoch=epoch;
    if(v->source_index) source_target=subs[v->source_index-1].target;
    if(gin_ws_topic(id->subject_name,v->source_index?subs[v->source_index-1].network:NULL,
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
        for(i=0;i<gin_account_plan_network_count(d->plan);i++) {
            net=gin_account_plan_network_at(d->plan,i);if(!strcmp(net->slug,network)) break;
        }
        if(i==gin_account_plan_network_count(d->plan)) goto done;
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
        for(i=0;i<gin_account_plan_window_count(d->plan);i++) {
            window=gin_account_plan_window_at(d->plan,i);
            if(window->network_id==net->id && !strcmp(window->name.canonical_target,name.canonical_target)) break;
        }
        if(i==gin_account_plan_window_count(d->plan)) {code=GIN_SESSION_OK;goto done;}
        selected.window_index=i;selected.role=window->role==GIN_REMOTE_QUERY?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
        goto project_selected;
    }
    if(strcmp(kind,"message")) {code=GIN_SESSION_OK;goto done;}
    kind=dispatch_text(row,"kind");network=dispatch_text(row,"network");target=dispatch_text(row,"channel");peer=dispatch_text(row,"dm_with");
    if(!kind || !network || !target || !v->source_index || !source_target ||
        strcmp(subs[v->source_index-1].network,network)) goto done;
    if(gin_name_prepare(network,target,d->chantypes,&name)<0 ||
        gin_ws_topic(id->subject_name,network,target,topic,sizeof(topic),&length)!=GIN_WS_VALUE_OK || strcmp(topic,source->topic)) goto done;
    for(i=0;i<gin_account_plan_network_count(d->plan);i++) {
        net=gin_account_plan_network_at(d->plan,i);if(!strcmp(net->slug,network)) break;
    }
    if(i==gin_account_plan_network_count(d->plan)) goto done;
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
        for(i=0;i<gin_account_plan_window_count(d->plan);i++) {
            window=gin_account_plan_window_at(d->plan,i);
            if(window->network_id==net->id && !strcmp(window->name.canonical_target,name.canonical_target)) break;
        }
        if(i==gin_account_plan_window_count(d->plan) || (peer?window->role!=GIN_REMOTE_QUERY:window->role!=GIN_REMOTE_CHANNEL) ||
            (!peer && json_object_get(row,"dm_with") && !json_is_null(json_object_get(row,"dm_with")))) goto done;
        selected.window_index=i;selected.role=peer?GIN_RENDER_QUERY:GIN_RENDER_CHANNEL;
    }
project_selected:
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
    if(code==GIN_SESSION_OK && proposal) { *fresh_out=proposal;proposal=NULL; }
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
