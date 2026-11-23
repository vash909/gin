#include "gin_history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
#define ROW "{\"id\":99,\"server_time\":0,\"network\":\"Net\",\"channel\":\"$server\",\"kind\":\"server_event\",\"sender\":\"*\",\"body\":\"upstream connect failed: caf\u00e9\\n\\\"\\\\\",\"metadata\":{\"kind\":\"notice\",\"secret\":\"DO NOT DISPLAY\"}}"
static struct gin_history_scope scope = {"https://example.test","01234567-89ab-cdef-0123-456789abcdef","Net","$server","#","Nick",1,7,GIN_RENDER_STATUS,GIN_RENDER_MODE_PLAIN};
static struct gin_render_context context = {"alice","Net","$server","$server","#","join7",7,GIN_RENDER_STATUS,GIN_RENDER_MODE_PLAIN};
static const char wire[] = "[\"join7\",null,\"grappa:user:alice/network:Net/channel:$server\",\"event\",{\"kind\":\"message\",\"message\":" ROW "}]";
static const char rest[] = "[" ROW "]";
static const char expected[] = "[1970-01-01T00:00:00.000Z] server_event sender=\"*\" body=\"upstream connect failed: caf\u00e9\\x0A\\\"\\\\\"\n";
static void bytes(struct gin_render_record *r)
{
    struct gin_phoenix_view v = gin_render_record_bytes(r);
    CHECK(v.length == strlen(expected) && !memcmp(v.data,expected,v.length));
}
static void ignored_live_kind(enum gin_render_role role, const char *kind)
{
    struct gin_phoenix_frame *frame = NULL;
    struct gin_render_record *out = NULL;
    struct gin_render_context c = context;
    const char *target = role == GIN_RENDER_CHANNEL ? "#Room" :
        role == GIN_RENDER_QUERY ? "Peer" : "$server";
    const char *topic_target = role == GIN_RENDER_CHANNEL ? "#room" :
        role == GIN_RENDER_QUERY ? "peer" : "$server";
    const char *dm = role == GIN_RENDER_QUERY ? "\"Peer\"" : "null";
    char input[2048];
    int n;
    c.role = role; c.target = target; c.source_target = target;
    n = snprintf(input,sizeof(input),
        "[\"join7\",null,\"grappa:user:alice/network:Net/channel:%s\",\"event\","
        "{\"kind\":\"message\",\"message\":{\"id\":99,\"server_time\":0,"
        "\"network\":\"Net\",\"channel\":\"%s\",\"kind\":\"%s\","
        "\"sender\":\"*\",\"body\":\"fixture\",\"dm_with\":%s}}]",
        topic_target,target,kind,dm);
    CHECK(n > 0 && (size_t)n < sizeof(input));
    CHECK(gin_phoenix_decode((const unsigned char *)input,(size_t)n,&frame) == GIN_PHOENIX_OK);
    CHECK(gin_render_event(frame,&c,7,0,&out) == GIN_RENDER_IGNORE && !out);
    gin_phoenix_frame_free(frame);
}
int main(void)
{
    struct gin_phoenix_frame *f = NULL;
    struct gin_history_page *p = NULL, *live = NULL;
    struct gin_history_request request = {GIN_HISTORY_AFTER,0,50};
    struct gin_history_recovery *recovery = NULL;
    struct gin_render_record *a = NULL, *b = NULL, *occupied;
    enum gin_render_code ca, cb;
    struct gin_render_context c;
    struct gin_history_scope s;
    unsigned i;
    alarm(15);
    CHECK(gin_phoenix_decode((const unsigned char *)wire,strlen(wire),&f) == GIN_PHOENIX_OK);
    CHECK(gin_history_page_decode(&scope,&request,(const unsigned char *)rest,strlen(rest),&p) == GIN_HISTORY_OK);
    CHECK(gin_history_page_count(p) == 1 && gin_history_page_scope(p)->role == GIN_RENDER_STATUS);
    CHECK(!strcmp(gin_history_page_row(p,0)->kind,"server_event"));
    CHECK(!strcmp(gin_history_page_row(p,0)->sender,"*"));
    CHECK(gin_history_live_decode(f,&context,&scope,7,&live) == GIN_HISTORY_OK);
    ca = gin_render_event(f,&context,7,123,&a);
    cb = gin_history_render(p,0,&b);
    fprintf(stderr,"healthy decode/scope; live=%d history=%d required=%d\n",ca,cb,GIN_RENDER_OK);
    CHECK(ca == GIN_RENDER_OK && cb == GIN_RENDER_OK);
    bytes(a); bytes(b);
    occupied = a;
    CHECK(gin_render_event(f,&context,7,0,&a) == GIN_RENDER_INVALID && a == occupied);
    CHECK(gin_history_render(p,0,&a) == GIN_RENDER_INVALID && a == occupied);
    for (i=0;i<9;i++) {
        struct gin_render_record *out = NULL;
        enum gin_render_code want = GIN_RENDER_ROUTE_MISMATCH;
        c = context;
        switch(i) {
        case 0: c.subject="other"; break;
        case 1: c.join_ref="stale"; break;
        case 2: c.socket_epoch=8; break;
        case 3: c.network="Other"; break;
        case 4: c.source_target="Other"; break;
        case 5: c.target="Other"; want=GIN_RENDER_INVALID; break;
        case 6: c.role=(enum gin_render_role)99; want=GIN_RENDER_INVALID; break;
        case 7: c.encryption=GIN_RENDER_MODE_UNKNOWN; want=GIN_RENDER_UNSUPPORTED_ENCRYPTION; break;
        default: c.encryption=GIN_RENDER_MODE_ENCRYPTED; want=GIN_RENDER_UNSUPPORTED_ENCRYPTION; break;
        }
        CHECK(gin_render_event(f,&c,7,0,&out) == want && !out);
    }
    for(i=0;i<3;i++) {
        struct gin_history_page *out = NULL;
        s=scope;
        if(i==0) s.network_slug="Other";
        if(i==1) s.target="Other";
        if(i==2) s.encryption=GIN_RENDER_MODE_UNKNOWN;
        CHECK(gin_history_page_decode(&s,&request,(const unsigned char *)rest,strlen(rest),&out) != GIN_HISTORY_OK && !out);
    }
    ignored_live_kind(GIN_RENDER_CHANNEL,"server_event");
    ignored_live_kind(GIN_RENDER_QUERY,"server_event");
    ignored_live_kind(GIN_RENDER_CHANNEL,"future");
    ignored_live_kind(GIN_RENDER_QUERY,"future");
    ignored_live_kind(GIN_RENDER_STATUS,"future");
    CHECK(gin_history_recovery_new(&scope,0,50,1,&recovery) == GIN_HISTORY_OK);
    CHECK(gin_history_recovery_live(recovery,live,2) == GIN_HISTORY_OK);
    CHECK(gin_history_recovery_page(recovery,p,3) == GIN_HISTORY_OK);
    CHECK(gin_history_page_count(gin_history_recovery_view(recovery)) == 1);
    gin_history_recovery_free(recovery);
    gin_phoenix_frame_free(f); gin_history_page_free(p); gin_history_page_free(live);
    memset(&context,0,sizeof(context)); memset(&scope,0,sizeof(scope));
    bytes(a); bytes(b); gin_render_record_free(a); gin_render_record_free(b); a=NULL;
    CHECK(gin_render_plain_message("server_event",NULL,0,NULL,0,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_IGNORE && !a);
    /* Final qualification always exercises the explicit role API. */
    for(i=0;i<2;i++) {
        CHECK(gin_render_plain_message_role((enum gin_render_role)i,"server_event",NULL,0,NULL,0,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_IGNORE && !a);
    }
    CHECK(gin_render_plain_message_role((enum gin_render_role)99,"notice",NULL,0,NULL,0,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_INVALID && !a);
    CHECK(gin_render_plain_message_role(GIN_RENDER_STATUS,"future",NULL,0,NULL,0,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_IGNORE && !a);
    CHECK(gin_render_plain_message_role(GIN_RENDER_STATUS,"server_event",NULL,0,(const unsigned char *)"\001E2E",4,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_UNSUPPORTED_ENCRYPTION && !a);
    CHECK(gin_render_plain_message_role(GIN_RENDER_STATUS,"server_event",NULL,0,NULL,GIN_RENDER_BODY_MAX+1u,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_INVALID && !a);
    { const unsigned char bad[]={0xc0,0x80};
      CHECK(gin_render_plain_message_role(GIN_RENDER_STATUS,"server_event",bad,2,NULL,0,0,GIN_RENDER_MODE_PLAIN,&a) == GIN_RENDER_MALFORMED && !a); }
    puts("status vertical render: PASS (unit APIs, no WSS/owner claim)");
    return 0;
}
