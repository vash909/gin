#include "session_live_projection.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static struct gin_session_settings settings;
static struct gin_live_projection context;
static struct gin_history_scope scope = {"https://example.test", "01234567-89ab-cdef-0123-456789abcdef", "NetA", "$server", "#", "OwnA", 3000000001LL, 7, GIN_RENDER_STATUS, GIN_RENDER_MODE_PLAIN};
static struct gin_topology *topology;
static struct gin_live_view view;
static struct gin_phoenix_frame *frame;
static struct gin_render_record *record;
static struct gin_history_page *history;
static void load(const char *payload)
{
    char raw[4096];
    int n = snprintf(raw,sizeof(raw),"[null,null,\"grappa:user:Alice/network:NetA/channel:$server\",\"event\",%s]",payload);
    CHECK(n > 0 && (size_t)n < sizeof(raw));
    gin_phoenix_frame_free(frame); frame = NULL;
    CHECK(gin_phoenix_decode((const unsigned char *)raw,(size_t)n,&frame) == GIN_PHOENIX_OK);
    view.fields = *gin_phoenix_frame_fields(frame);
}
static enum gin_session_code invoke(void)
{
#ifdef STATUS_OLD_BRIDGE
    struct gin_topology *next = NULL;
    int terminal = 0;
    enum gin_session_code code = gin_live_project_history(&view,&context,topology,&next,&record,&terminal,&scope,&history);
    CHECK(!next); return code;
#else
    return gin_live_project_status_history(&view,&context,topology,&scope,&record,&history);
#endif
}
static void expect(enum gin_session_code code, int rec, int page)
{
    enum gin_session_code actual = invoke();
    if(actual != code) fprintf(stderr,"expected=%d actual=%d\n",code,actual);
    CHECK(actual == code); CHECK(!!record == rec); CHECK(!!history == page);
    if(history) { CHECK(gin_history_page_count(history)==1); CHECK(gin_history_page_source(history)==GIN_HISTORY_LIVE); CHECK(gin_history_page_row(history,0)->id==99); }
    gin_render_record_free(record); record=NULL; gin_history_page_free(history); history=NULL;
}
#define ROW(kind,id) "{\"kind\":\"message\",\"message\":{\"kind\":\"" kind "\",\"network\":\"NetA\",\"channel\":\"$server\",\"sender\":\"server\",\"body\":\"hello\",\"server_time\":123" id "}}"
int main(void)
{
    const char boot[]="{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":\"OwnA\"}],\"channels\":{\"NetA\":[]},\"heads\":{}}";
    const char *metadata[]={"topic_changed","channel_modes_changed","members_seeded","joined","join_failed","kicked","query_windows_list","own_nick_changed"};
    size_t i; char payload[256];
    alarm(15);
    CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&topology)==GIN_TOPOLOGY_OK);
    strcpy(settings.origin,"https://example.test"); strcpy(settings.local.network,"NetA"); strcpy(settings.local.target,"$server"); strcpy(settings.local.chantypes,"#"); strcpy(settings.mode,"plain");
    context.settings=&settings; context.subject="Alice"; context.own_nick="OwnA"; context.source_target="$server"; context.network_id=3000000001LL;
    view.source_index=37; view.source_join_ref="join7"; view.observed_epoch=7;
    load(ROW("server_event",",\"id\":99"));
    expect(GIN_SESSION_OK,1,1);
#ifndef STATUS_OLD_BRIDGE
    load(ROW("notice",",\"id\":99")); expect(GIN_SESSION_OK,1,1);
    load(ROW("privmsg","")); expect(GIN_SESSION_OK,1,0);
    load(ROW("action",",\"id\":99")); expect(GIN_SESSION_OK,1,1);
    load(ROW("notice",",\"id\":null")); expect(GIN_SESSION_ROUTE,0,0);
    load(ROW("notice",",\"id\":\"99\"")); expect(GIN_SESSION_ROUTE,0,0);
    load(ROW("notice",",\"id\":1.5")); expect(GIN_SESSION_ROUTE,0,0);
    load(ROW("notice",",\"id\":99"));
    view.fields.topic="grappa:user:Alice"; expect(GIN_SESSION_ROUTE,0,0); view.fields=*gin_phoenix_frame_fields(frame);
    view.fields.join_ref="wrong"; expect(GIN_SESSION_ROUTE,0,0); view.fields.join_ref="join7"; expect(GIN_SESSION_OK,1,1); view.fields.join_ref=NULL;
    view.observed_epoch=8; expect(GIN_SESSION_ROUTE,0,0); view.observed_epoch=7;
    context.network_id=42; expect(GIN_SESSION_ROUTE,0,0); scope.network_id=42; expect(GIN_SESSION_ROUTE,0,0); scope.network_id=context.network_id=3000000001LL;
    scope.role=GIN_RENDER_CHANNEL; expect(GIN_SESSION_ROUTE,0,0); scope.role=GIN_RENDER_STATUS;
    scope.encryption=GIN_RENDER_MODE_UNKNOWN; expect(GIN_SESSION_UNSUPPORTED,0,0); scope.encryption=GIN_RENDER_MODE_ENCRYPTED; expect(GIN_SESSION_UNSUPPORTED,0,0); scope.encryption=GIN_RENDER_MODE_PLAIN;
    context.query=1; expect(GIN_SESSION_ROUTE,0,0); context.query=0;
    context.source_target="$status"; expect(GIN_SESSION_ROUTE,0,0); context.source_target="$server";
    scope.own_nick="Other"; expect(GIN_SESSION_ROUTE,0,0); scope.own_nick="OwnA";
    scope.subject_id="Alice"; expect(GIN_SESSION_INVALID,0,0); scope.subject_id="01234567-89ab-cdef-0123-456789abcdef";
    for(i=0;i<sizeof(metadata)/sizeof(metadata[0]);i++) {
        (void)snprintf(payload,sizeof(payload),"{\"kind\":\"%s\"}",metadata[i]); load(payload); expect(GIN_SESSION_OK,0,0);
        CHECK(gin_topology_network_count(topology)==1 && gin_topology_window_count(topology)==0 && !gin_topology_queries_known(topology));
        CHECK(!strcmp(gin_topology_find_id(topology,3000000001LL)->own_nick,"OwnA"));
    }
    load(ROW("future",",\"id\":null")); expect(GIN_SESSION_OK,0,0);
    load("{\"kind\":\"e2e_future\"}"); expect(GIN_SESSION_UNSUPPORTED,0,0);
    load(ROW("e2e_future","")); expect(GIN_SESSION_UNSUPPORTED,0,0);
    load("{\"kind\":\"future\",\"body\":\"\\u0001E2Ebad\"}"); expect(GIN_SESSION_UNSUPPORTED,0,0);
    load("{\"kind\":\"message\",\"message\":{\"kind\":\"future\",\"body\":\"\\u0001E2Ebad\"}}"); expect(GIN_SESSION_UNSUPPORTED,0,0);
    load(ROW("notice",",\"id\":99"));
    record=(struct gin_render_record *)&settings; CHECK(invoke()==GIN_SESSION_INVALID); CHECK(record==(struct gin_render_record *)&settings && !history); record=NULL;
    history=(struct gin_history_page *)&settings; CHECK(invoke()==GIN_SESSION_INVALID); CHECK(history==(struct gin_history_page *)&settings && !record); history=NULL;
    CHECK(invoke()==GIN_SESSION_OK); gin_phoenix_frame_free(frame); frame=NULL; gin_topology_free(topology); topology=NULL; memset(&settings,0,sizeof(settings)); memset(&context,0,sizeof(context));
    CHECK(!strcmp(gin_history_page_row(history,0)->body,"hello"));
    { struct gin_phoenix_view bytes=gin_render_record_bytes(record); CHECK(bytes.length && strstr((const char *)bytes.data,"notice sender=\"server\" body=\"hello\"\n")); }
    gin_render_record_free(record); gin_history_page_free(history);
#else
    (void)metadata; (void)i; (void)payload;
#endif
    gin_phoenix_frame_free(frame); gin_topology_free(topology); puts("status projection: PASS"); return 0;
}
