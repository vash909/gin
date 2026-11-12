#include "history_values.h"
#include "history_evidence.h"
#include "gin_naming.h"
#include "gin_http.h"
#include "connect_values.h"
#include <stdlib.h>
#include <string.h>

static int eq(const char *a,const char *b) {return a && b ? !strcmp(a,b):a==b;}
static int fold(const char *a,const char *b)
{
    if(!a || !b) return a==b;
    while(*a && *b) {unsigned char x=(unsigned char)*a++,y=(unsigned char)*b++; if(x>='A'&&x<='Z') x+=32; if(y>='A'&&y<='Z') y+=32; if(x!=y) return 0;}
    return *a==*b;
}
static int uuid(const char *s)
{
    size_t i; if(!s || strlen(s)!=36) return 0;
    for(i=0;i<36;i++) {if(i==8||i==13||i==18||i==23) {if(s[i]!='-') return 0;} else if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f'))) return 0;}
    return 1;
}
enum gin_history_code gin_history_scope_check(const struct gin_history_scope *s)
{
    struct gin_name name,own;
    if(!s || !s->origin || !gin_http_origin_valid(s->origin) || !uuid(s->subject_id) || !s->network_slug || !s->target || !s->chantypes || !s->own_nick || s->network_id<=0 || !s->socket_epoch || s->role<GIN_RENDER_CHANNEL || s->role>GIN_RENDER_STATUS || s->encryption<GIN_RENDER_MODE_UNKNOWN || s->encryption>GIN_RENDER_MODE_ENCRYPTED) return GIN_HISTORY_INVALID;
    if(gin_name_prepare(s->network_slug,s->target,s->chantypes,&name) || gin_name_prepare(s->network_slug,s->own_nick,s->chantypes,&own) || own.kind!=GIN_WINDOW_NONCHANNEL) return GIN_HISTORY_INVALID;
    if((s->role==GIN_RENDER_CHANNEL && name.kind!=GIN_WINDOW_CHANNEL) || (s->role==GIN_RENDER_QUERY && (name.kind!=GIN_WINDOW_NONCHANNEL || !strcmp(s->target,"$server"))) || (s->role==GIN_RENDER_STATUS && strcmp(s->target,"$server"))) return GIN_HISTORY_INVALID;
    return s->encryption==GIN_RENDER_MODE_PLAIN?GIN_HISTORY_OK:GIN_HISTORY_UNSUPPORTED;
}
enum gin_history_code gin_history_request_check(const struct gin_history_request *r)
{
    if(!r || r->cursor<GIN_HISTORY_LATEST || r->cursor>GIN_HISTORY_AFTER || !r->limit || r->limit>GIN_HISTORY_PAGE_ROWS_MAX || (r->cursor==GIN_HISTORY_LATEST && r->anchor) || (r->cursor==GIN_HISTORY_BEFORE && r->anchor<=0) || (r->cursor==GIN_HISTORY_AFTER && r->anchor<0)) return GIN_HISTORY_INVALID;
    return GIN_HISTORY_OK;
}
int gin_history_scope_equal(const struct gin_history_scope *a,const struct gin_history_scope *b)
{return a && b && eq(a->origin,b->origin) && eq(a->subject_id,b->subject_id) && eq(a->network_slug,b->network_slug) && eq(a->target,b->target) && eq(a->chantypes,b->chantypes) && eq(a->own_nick,b->own_nick) && a->network_id==b->network_id && a->socket_epoch==b->socket_epoch && a->role==b->role && a->encryption==b->encryption;}
int gin_history_row_equal(const struct gin_history_row *a,const struct gin_history_row *b)
{return a && b && a->id==b->id && a->server_time==b->server_time && a->dm_present==b->dm_present && eq(a->network,b->network) && eq(a->channel,b->channel) && eq(a->kind,b->kind) && eq(a->sender,b->sender) && eq(a->body,b->body) && eq(a->dm_with,b->dm_with);}
static int encrypted(const char *kind,const char *body)
{
    size_t i,n=body?strlen(body):0;
    if(kind && !strncmp(kind,"e2e_",4)) return 1;
    for(i=0;i+3<n;i++) if(body[i]==1 && !memcmp(body+i+1,"E2E",3)) return 1;
    return 0;
}
static enum gin_history_code text_check(const char *s,size_t max,int nullable)
{if(!s) return nullable?GIN_HISTORY_OK:GIN_HISTORY_MALFORMED; if(strlen(s)>max) return GIN_HISTORY_LIMIT; return gin_history_utf8(s)?GIN_HISTORY_OK:GIN_HISTORY_MALFORMED;}
static enum gin_history_code row_check(const struct gin_history_scope *s,const struct gin_history_row *r)
{
    struct gin_name name; enum gin_history_code c;
    if(!r) return GIN_HISTORY_INVALID;
    if(encrypted(r->kind,r->body)) return GIN_HISTORY_UNSUPPORTED;
    if(r->id<=0 || !r->network || !r->channel || !r->kind || !*r->kind || !r->sender || (r->dm_present!=0 && r->dm_present!=1) || (!r->dm_present && r->dm_with)) return GIN_HISTORY_MALFORMED;
#define TEXT(v,m,n) do {c=text_check(v,m,n); if(c!=GIN_HISTORY_OK) return c;} while(0)
    TEXT(r->kind,GIN_HISTORY_KIND_MAX,0); TEXT(r->sender,GIN_HISTORY_SENDER_MAX,0); TEXT(r->body,GIN_HISTORY_BODY_MAX,1); TEXT(r->dm_with,256,1);
#undef TEXT
    if(gin_name_prepare(r->network,r->channel,s->chantypes,&name)) return GIN_HISTORY_MALFORMED;
    if(strcmp(r->network,s->network_slug)) return GIN_HISTORY_ROUTE;
    if(r->dm_with && (gin_name_prepare(r->network,r->dm_with,s->chantypes,&name) || name.kind!=GIN_WINDOW_NONCHANNEL)) return GIN_HISTORY_MALFORMED;
    if(s->role==GIN_RENDER_QUERY) {
        if(!r->dm_present) return GIN_HISTORY_ROUTE;
        if(r->dm_with) return fold(r->dm_with,s->target)?GIN_HISTORY_OK:GIN_HISTORY_ROUTE;
        if(!strcmp(r->kind,"privmsg") || !strcmp(r->kind,"notice") || !strcmp(r->kind,"action")) return GIN_HISTORY_ROUTE;
        return fold(r->channel,s->target)?GIN_HISTORY_OK:GIN_HISTORY_ROUTE;
    }
    return !r->dm_with && (s->role==GIN_RENDER_STATUS?eq(r->channel,"$server"):fold(r->channel,s->target))?GIN_HISTORY_OK:GIN_HISTORY_ROUTE;
}
void gin_history_page_free(struct gin_history_page *p)
{
    size_t i; if(!p) return;
    free((void *)p->scope.origin); free((void *)p->scope.subject_id); free((void *)p->scope.network_slug); free((void *)p->scope.target); free((void *)p->scope.chantypes); free((void *)p->scope.own_nick);
    for(i=0;i<p->count;i++) {free((void *)p->rows[i].network); free((void *)p->rows[i].channel); free((void *)p->rows[i].kind); free((void *)p->rows[i].sender); free((void *)p->rows[i].body); free((void *)p->rows[i].dm_with);}
    free(p->rows); free(p);
}
static enum gin_history_code copy(struct gin_history_page *p,const char *s,const char **out)
{
    size_t n; char *v; if(!s) {*out=NULL;return GIN_HISTORY_OK;} n=strlen(s)+1;
    if(n>GIN_HISTORY_OWNED_MAX-p->owned_bytes) return GIN_HISTORY_LIMIT;
    v=malloc(n); if(!v) return GIN_HISTORY_NOMEM; memcpy(v,s,n); *out=v; p->owned_bytes+=n; return GIN_HISTORY_OK;
}
enum gin_history_code gin_history_copy_rows(const struct gin_history_scope *s,const struct gin_history_row *const *rows,size_t count,enum gin_history_source source,const struct gin_history_request *request,struct gin_history_page **out)
{
    struct gin_history_page *p; size_t i; enum gin_history_code c;
    if(!out || *out || (count && !rows) || source<GIN_HISTORY_REST || source>GIN_HISTORY_MERGED || (source==GIN_HISTORY_REST?gin_history_request_check(request)!=GIN_HISTORY_OK:request!=NULL)) return GIN_HISTORY_INVALID;
    c=gin_history_scope_check(s); if(c!=GIN_HISTORY_OK) return c;
    if(count>(source==GIN_HISTORY_REST?request->limit:source==GIN_HISTORY_LIVE?1u:GIN_HISTORY_ROWS_MAX)) return GIN_HISTORY_LIMIT;
    if(source==GIN_HISTORY_LIVE && count!=1) return GIN_HISTORY_INVALID;
    for(i=0;i<count;i++) {
        c=row_check(s,rows[i]); if(c!=GIN_HISTORY_OK) return c;
        if(source==GIN_HISTORY_REST && ((request->cursor==GIN_HISTORY_BEFORE && rows[i]->id>=request->anchor) || (request->cursor==GIN_HISTORY_AFTER && rows[i]->id<=request->anchor))) return GIN_HISTORY_MALFORMED;
        if(i && (source==GIN_HISTORY_REST && request->cursor!=GIN_HISTORY_AFTER ? rows[i-1]->id<=rows[i]->id : rows[i-1]->id>=rows[i]->id)) return GIN_HISTORY_MALFORMED;
    }
    p=calloc(1,sizeof(*p)); if(!p) return GIN_HISTORY_NOMEM;
    p->owned_bytes=sizeof(*p); p->source=source; if(request) p->request=*request;
    p->scope.network_id=s->network_id; p->scope.socket_epoch=s->socket_epoch; p->scope.role=s->role; p->scope.encryption=s->encryption;
#define COPY(a,b) do {c=copy(p,a,&(b));if(c!=GIN_HISTORY_OK) goto fail;} while(0)
    COPY(s->origin,p->scope.origin); COPY(s->subject_id,p->scope.subject_id); COPY(s->network_slug,p->scope.network_slug); COPY(s->target,p->scope.target); COPY(s->chantypes,p->scope.chantypes); COPY(s->own_nick,p->scope.own_nick);
    if(count) {p->rows=calloc(count,sizeof(*p->rows)); if(!p->rows) {c=GIN_HISTORY_NOMEM;goto fail;} p->owned_bytes+=count*sizeof(*p->rows);p->count=count;}
    for(i=0;i<count;i++) {
        struct gin_history_row *r=&p->rows[i]; const struct gin_history_row *v=rows[i];
        r->id=v->id;r->server_time=v->server_time;r->dm_present=v->dm_present;
        COPY(v->network,r->network);COPY(v->channel,r->channel);COPY(v->kind,r->kind);COPY(v->sender,r->sender);COPY(v->body,r->body);COPY(v->dm_with,r->dm_with);
    }
#undef COPY
    *out=p;return GIN_HISTORY_OK;
fail: gin_history_page_free(p);return c;
}
size_t gin_history_page_count(const struct gin_history_page *p) {return p?p->count:0;}
size_t gin_history_page_owned_bytes(const struct gin_history_page *p) {return p?p->owned_bytes:0;}
const struct gin_history_row *gin_history_page_row(const struct gin_history_page *p,size_t i) {return p && i<p->count?&p->rows[i]:NULL;}
const struct gin_history_scope *gin_history_page_scope(const struct gin_history_page *p) {return p?&p->scope:NULL;}
enum gin_history_source gin_history_page_source(const struct gin_history_page *p) {return p?p->source:GIN_HISTORY_MERGED;}
const struct gin_history_request *gin_history_page_request(const struct gin_history_page *p) {return p && p->source==GIN_HISTORY_REST?&p->request:NULL;}
static enum gin_history_code load(const unsigned char *b,size_t n,json_t **out)
{
    size_t i,depth=0; int quoted=0,escape=0; json_error_t error={0};
    if(n>GIN_HISTORY_RESPONSE_MAX) return GIN_HISTORY_LIMIT;
    if(!n) return GIN_HISTORY_MALFORMED;
    for(i=0;i<n;i++) {unsigned char c=b[i]; if(!c) return GIN_HISTORY_MALFORMED; if(quoted) {if(escape) escape=0;else if(c=='\\') escape=1;else if(c=='"') quoted=0;} else if(c=='"') quoted=1;else if(c=='{'||c=='[') {if(++depth>GIN_HISTORY_DEPTH_MAX) return GIN_HISTORY_LIMIT;} else if(c=='}'||c==']') {if(!depth) return GIN_HISTORY_MALFORMED;depth--;}}
    *out=json_loadb((const char *)b,n,JSON_REJECT_DUPLICATES,&error);
    if(!*out) return json_error_code(&error)==json_error_out_of_memory || !error.text[0]?GIN_HISTORY_NOMEM:GIN_HISTORY_MALFORMED;
    if(!gin_history_evidence(b,n,*out)) {json_decref(*out);*out=NULL;return GIN_HISTORY_MALFORMED;}
    return GIN_HISTORY_OK;
}
static enum gin_history_code read_row(json_t *o,struct gin_history_row *r)
{
    json_t *v,*dm;
    if(!json_is_object(o)) return GIN_HISTORY_MALFORMED;
    if(encrypted(json_string_value(json_object_get(o,"kind")),json_string_value(json_object_get(o,"body")))) return GIN_HISTORY_UNSUPPORTED;
    memset(r,0,sizeof(*r));
    v=json_object_get(o,"id"); if(!json_is_integer(v)) return GIN_HISTORY_MALFORMED;r->id=(int64_t)json_integer_value(v);
    v=json_object_get(o,"server_time");if(!json_is_integer(v)) return GIN_HISTORY_MALFORMED;r->server_time=(int64_t)json_integer_value(v);
    r->network=json_string_value(json_object_get(o,"network"));r->channel=json_string_value(json_object_get(o,"channel"));r->kind=json_string_value(json_object_get(o,"kind"));r->sender=json_string_value(json_object_get(o,"sender"));
    v=json_object_get(o,"body");if(!v || (!json_is_null(v)&&!json_is_string(v))) return GIN_HISTORY_MALFORMED;r->body=json_string_value(v);
    dm=json_object_get(o,"dm_with");r->dm_present=dm!=NULL;if(dm && !json_is_null(dm)&&!json_is_string(dm)) return GIN_HISTORY_MALFORMED;r->dm_with=json_string_value(dm);
    return GIN_HISTORY_OK;
}
enum gin_history_code gin_history_page_decode(const struct gin_history_scope *s,const struct gin_history_request *request,const unsigned char *body,size_t length,struct gin_history_page **out)
{
    json_t *root=NULL; struct gin_history_row rows[GIN_HISTORY_PAGE_ROWS_MAX]; const struct gin_history_row *views[GIN_HISTORY_PAGE_ROWS_MAX];size_t i,n;enum gin_history_code c;
    if(!out || *out || !body || gin_history_request_check(request)!=GIN_HISTORY_OK) return GIN_HISTORY_INVALID;
    c=gin_history_scope_check(s);if(c!=GIN_HISTORY_OK) return c;
    c=load(body,length,&root);if(c!=GIN_HISTORY_OK) return c;
    c=GIN_HISTORY_MALFORMED;if(!json_is_array(root)) goto done;
    n=json_array_size(root);if(n>request->limit) {c=GIN_HISTORY_LIMIT;goto done;}
    for(i=0;i<n;i++) {c=read_row(json_array_get(root,i),&rows[i]);if(c!=GIN_HISTORY_OK) goto done;views[i]=&rows[i];}
    c=gin_history_copy_rows(s,views,n,GIN_HISTORY_REST,request,out);
done:json_decref(root);return c;
}
enum gin_history_code gin_history_live_decode(const struct gin_phoenix_frame *frame,const struct gin_render_context *ctx,const struct gin_history_scope *s,uint64_t observed_epoch,struct gin_history_page **out)
{
    const struct gin_phoenix_fields *f=gin_phoenix_frame_fields(frame);json_t *root=NULL,*o;const char *kind;struct gin_history_row row;const struct gin_history_row *view=&row;enum gin_history_code c;char topic[GIN_WS_TOPIC_CAP];size_t n;
    if(!f || !ctx || !out || *out || !ctx->subject || !ctx->network || !ctx->target || !ctx->chantypes || !ctx->join_ref || !*ctx->join_ref || !ctx->socket_epoch) return GIN_HISTORY_INVALID;
    c=gin_history_scope_check(s);if(c!=GIN_HISTORY_OK) return c;
    if(ctx->encryption!=GIN_RENDER_MODE_PLAIN) return GIN_HISTORY_UNSUPPORTED;
    if(observed_epoch!=s->socket_epoch || observed_epoch!=ctx->socket_epoch) return GIN_HISTORY_STALE;
    if(!eq(ctx->network,s->network_slug) || !eq(ctx->target,s->target) || !eq(ctx->chantypes,s->chantypes) || ctx->role!=s->role) return GIN_HISTORY_ROUTE;
    if(gin_ws_topic(ctx->subject,ctx->source_target?ctx->network:NULL,ctx->source_target,topic,sizeof(topic),&n)!=GIN_WS_VALUE_OK) return GIN_HISTORY_INVALID;
    if(strcmp(topic,f->topic) || (f->join_ref && strcmp(f->join_ref,ctx->join_ref))) return GIN_HISTORY_ROUTE;
    c=load(f->payload.data,f->payload.length,&root);if(c!=GIN_HISTORY_OK) return c;
    kind=json_string_value(json_object_get(root,"kind")); c=GIN_HISTORY_MALFORMED;if(!kind) goto done;
    if(encrypted(kind,json_string_value(json_object_get(root,"body")))) {c=GIN_HISTORY_UNSUPPORTED;goto done;}
    o=json_object_get(root,"message");
    if(json_is_object(o) && encrypted(json_string_value(json_object_get(o,"kind")),json_string_value(json_object_get(o,"body")))) {c=GIN_HISTORY_UNSUPPORTED;goto done;}
    if(strcmp(f->event,"event") || strcmp(kind,"message")) {c=GIN_HISTORY_IGNORE;goto done;}
    c=read_row(o,&row);if(c!=GIN_HISTORY_OK) goto done;
    if(!ctx->source_target || !fold(row.channel,ctx->source_target)) {c=GIN_HISTORY_ROUTE;goto done;}
    c=gin_history_copy_rows(s,&view,1,GIN_HISTORY_LIVE,NULL,out);
done:json_decref(root);return c;
}
const char *gin_history_error(enum gin_history_code c)
{
    static const char *const names[]={"ok","ignored event","invalid history arguments","malformed history","history limit exceeded","history allocation failed","history route mismatch","unsupported encryption","history conflict","stale history epoch","history deadline exceeded","history preflight failed","history authentication failed","history TLS failed","history timeout","history transport failed","history remote failure"};
    return (unsigned)c<sizeof(names)/sizeof(names[0])?names[c]:"unknown history error";
}
