#include "session_live_projection.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static int mutation;
static size_t parses, allocation_call, failure_at;
static json_t *projection_loadb(const char *raw, size_t length, size_t flags, json_error_t *error)
{
    json_t *o = json_loadb(raw, length, flags, error);
    ++parses;
    if (o && mutation == 1) CHECK(json_object_set_new(o, "kind", json_string("future")) == 0);
    if (o && mutation == 2) CHECK(json_object_del(json_object_get(o, "message"), "id") == 0);
    return o;
}
#define json_loadb projection_loadb
#include "../src/session_live_projection.c"
#undef json_loadb
static void *fault_malloc(size_t bytes)
{
    if (allocation_call++ == failure_at) return NULL;
    return malloc(bytes);
}
static const char boot[] = "{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":\"OwnA\"}],\"channels\":{\"NetA\":[]},\"heads\":{}}";
static const char raw[] = "[null,null,\"grappa:user:Alice/network:NetA/channel:$server\",\"event\",{\"kind\":\"message\",\"message\":{\"kind\":\"server_event\",\"network\":\"NetA\",\"channel\":\"$server\",\"sender\":\"server\",\"body\":\"hello\",\"server_time\":123,\"id\":99}}]";
static const char expected[] = "[1970-01-01T00:00:00.123Z] server_event sender=\"server\" body=\"hello\"\n";
static struct gin_history_scope scope = {"https://example.test", "01234567-89ab-cdef-0123-456789abcdef", "NetA", "$server", "#", "OwnA", 3000000001LL, 7, GIN_RENDER_STATUS, GIN_RENDER_MODE_PLAIN};
static void exact(struct gin_render_record *record, struct gin_history_page *page)
{
    struct gin_phoenix_view bytes = gin_render_record_bytes(record);
    const struct gin_history_row *row;
    const struct gin_history_scope *s;
    CHECK(record && page);
    CHECK(bytes.length == sizeof(expected)-1u && !memcmp(bytes.data, expected, bytes.length));
    CHECK(gin_history_page_count(page) == 1 && gin_history_page_source(page) == GIN_HISTORY_LIVE);
    row = gin_history_page_row(page, 0); s = gin_history_page_scope(page);
    CHECK(row && row->id == 99 && row->server_time == 123);
    CHECK(!strcmp(row->kind,"server_event") && !strcmp(row->network,"NetA") && !strcmp(row->channel,"$server"));
    CHECK(!strcmp(row->sender,"server") && !strcmp(row->body,"hello") && !row->dm_present && !row->dm_with);
    CHECK(s && !strcmp(s->origin,scope.origin) && !strcmp(s->subject_id,scope.subject_id));
    CHECK(!strcmp(s->network_slug,"NetA") && !strcmp(s->target,"$server") && !strcmp(s->own_nick,"OwnA") && !strcmp(s->chantypes,"#"));
    CHECK(s->network_id == 3000000001LL && s->socket_epoch == 7 && s->role == GIN_RENDER_STATUS && s->encryption == GIN_RENDER_MODE_PLAIN);
}
int main(void)
{
    struct gin_topology *topology = NULL;
    struct gin_phoenix_frame *frame = NULL;
    struct gin_session_settings settings = {0};
    struct gin_live_projection context = {0};
    struct gin_live_view view = {0};
    struct gin_render_record *record = NULL;
    struct gin_history_page *page = NULL;
    json_malloc_t original_malloc; json_free_t original_free;
    unsigned char saved[sizeof(raw)];
    size_t n, calls, failures = 0, successes = 0;
    enum gin_session_code code;
    CHECK(gin_topology_parse_boot((const unsigned char *)boot,sizeof(boot)-1u,"#",&topology) == GIN_TOPOLOGY_OK);
    CHECK(gin_phoenix_decode((const unsigned char *)raw,sizeof(raw)-1u,&frame) == GIN_PHOENIX_OK);
    strcpy(settings.origin,scope.origin); strcpy(settings.local.network,"NetA"); strcpy(settings.local.target,"$server"); strcpy(settings.local.chantypes,"#"); strcpy(settings.mode,"plain");
    context.settings = &settings; context.subject = "Alice"; context.own_nick = "OwnA"; context.source_target = "$server"; context.network_id = 3000000001LL;
    view.fields = *gin_phoenix_frame_fields(frame); view.source_join_ref = "join7"; view.observed_epoch = 7; view.source_index = 37;
    CHECK(view.fields.payload.length < sizeof(saved)); memcpy(saved,view.fields.payload.data,view.fields.payload.length);
    CHECK(strcmp(context.subject,scope.subject_id));
    CHECK(gin_live_project_status_history(&view,&context,topology,&scope,&record,&page) == GIN_SESSION_OK);
    exact(record,page); gin_render_record_free(record); gin_history_page_free(page); record = NULL; page = NULL;
    puts("healthy exact baseline initialized"); fflush(stdout);
    for (mutation = 1; mutation <= 2; ++mutation) {
        parses = 0;
        code = gin_live_project_status_history(&view,&context,topology,&scope,&record,&page);
        CHECK(parses == 1);
        CHECK(!memcmp(saved,view.fields.payload.data,view.fields.payload.length));
        if (code != GIN_SESSION_ROUTE || record || page) fprintf(stderr,"NEW_PARSE_EVIDENCE_ASSERTION mutation=%d code=%d\n",mutation,code);
        CHECK(code == GIN_SESSION_ROUTE && !record && !page);
    }
    mutation = 0;
    json_get_alloc_funcs(&original_malloc,&original_free);
    allocation_call = 0; failure_at = (size_t)-1;
    json_set_alloc_funcs(fault_malloc,original_free);
    code = gin_live_project_status_history(&view,&context,topology,&scope,&record,&page);
    calls = allocation_call;
    CHECK(code == GIN_SESSION_OK); exact(record,page);
    gin_render_record_free(record); gin_history_page_free(page); record = NULL; page = NULL;
    json_set_alloc_funcs(original_malloc,original_free);
    CHECK(calls > 0 && calls < 4096);
    for (n = 0; n < calls; ++n) {
        allocation_call = 0; failure_at = n;
        json_set_alloc_funcs(fault_malloc,original_free);
        code = gin_live_project_status_history(&view,&context,topology,&scope,&record,&page);
        if (code == GIN_SESSION_OK) { exact(record,page); ++successes; }
        else { CHECK(!record && !page); ++failures; }
        /* All outputs and transient DOMs freed before restoring allocators. */
        gin_render_record_free(record); gin_history_page_free(page); record = NULL; page = NULL;
        json_set_alloc_funcs(original_malloc,original_free);
        CHECK(!memcmp(saved,view.fields.payload.data,view.fields.payload.length));
    }
    CHECK(failures > 0);
    printf("status fidelity PASS: allocation sites=%zu exact-success=%zu atomic-rejections=%zu\n",calls,successes,failures);
    gin_phoenix_frame_free(frame); gin_topology_free(topology);
    return 0;
}
