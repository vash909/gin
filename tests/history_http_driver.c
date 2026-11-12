/* Synthetic native history HTTPS driver; never emits credentials or bodies. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_history.h"
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void field(json_t *j, const char *key, char *dst, size_t cap)
{
    const char *s = json_string_value(json_object_get(j, key));
    assert(s && strlen(s) < cap);
    strcpy(dst, s);
}
int main(int argc, char **argv)
{
    static const char boot[] = "{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":\"Own\"}],\"channels\":{\"NetA\":[{\"name\":\"#MiXé\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
    static const char queries[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"3000000001\":[{\"network_id\":3000000001,\"target_nick\":\"PeEr\",\"opened_at\":\"x\"}]}}";
    struct gin_topology *base = NULL, *topology = NULL;
    struct gin_credential credential = {0}, saved;
    struct gin_auth_settings settings, saved_settings;
    struct gin_protocol_policy policy = {63, 1}, saved_policy;
    struct gin_history_scope scope, saved_scope;
    struct gin_history_request request = {GIN_HISTORY_AFTER, 0, 2}, saved_request;
    struct gin_history_page *page = NULL, *occupied = NULL;
    struct gin_history_fetch_result r;
    json_t *j;
    const char *mode;
    int i, calls;
    assert(argc == 6);
    settings.origin = argv[1]; settings.ca_file = argv[2];
    settings.connect_ms = 1000; settings.total_ms = 2000;
    j = json_load_file(argv[3], JSON_REJECT_DUPLICATES, NULL);
    assert(json_is_object(j));
    field(j, "origin", credential.origin, sizeof(credential.origin));
    field(j, "token", credential.token, sizeof(credential.token));
    field(j, "subject_id", credential.subject_id, sizeof(credential.subject_id));
    field(j, "subject_name", credential.subject_name, sizeof(credential.subject_name));
    json_decref(j);
    assert(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#&", &base) == GIN_TOPOLOGY_OK);
    assert(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &topology) == GIN_TOPOLOGY_OK);
    memset(&scope, 0, sizeof(scope));
    scope.origin = settings.origin; scope.subject_id = credential.subject_id;
    scope.network_slug = "NetA"; scope.target = "#MiXé"; scope.chantypes = "#&";
    scope.own_nick = "Own"; scope.network_id = 3000000001LL; scope.socket_epoch = 9;
    scope.role = GIN_RENDER_CHANNEL; scope.encryption = GIN_RENDER_MODE_PLAIN;
    mode = argv[4]; calls = atoi(argv[5]); assert(calls > 0 && calls <= 2);
    if (!strcmp(mode, "query") || !strcmp(mode, "unknown-queries") || !strcmp(mode, "missing-peer")) {
        scope.role = GIN_RENDER_QUERY; scope.target = !strcmp(mode, "missing-peer") ? "Nobody" : "PeEr";
    }
    if (!strcmp(mode, "status")) { scope.role = GIN_RENDER_STATUS; scope.target = "$server"; }
    if (!strcmp(mode, "before")) { request.cursor = GIN_HISTORY_BEFORE; request.anchor = 99; }
    if (!strcmp(mode, "latest")) request.cursor = GIN_HISTORY_LATEST;
    if (!strcmp(mode, "unknown-mode")) scope.encryption = GIN_RENDER_MODE_UNKNOWN;
    if (!strcmp(mode, "encrypted")) scope.encryption = GIN_RENDER_MODE_ENCRYPTED;
    if (!strcmp(mode, "wrong-origin")) scope.origin = "https://example.invalid";
    if (!strcmp(mode, "wrong-uuid")) scope.subject_id = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
    if (!strcmp(mode, "bad-credential")) credential.token[0] = '\n';
    if (!strcmp(mode, "credential-origin")) strcpy(credential.origin, "https://example.invalid");
    if (!strcmp(mode, "bad-uuid")) credential.subject_id[0] = 'X';
    if (!strcmp(mode, "wrong-id")) scope.network_id = 2;
    if (!strcmp(mode, "wrong-slug")) scope.network_slug = "neta";
    if (!strcmp(mode, "wrong-nick")) scope.own_nick = "Other";
    if (!strcmp(mode, "missing-channel")) scope.target = "#Other";
    if (!strcmp(mode, "zero-epoch")) scope.socket_epoch = 0;
    if (!strcmp(mode, "bad-limit")) request.limit = 0;
    if (!strcmp(mode, "bad-anchor")) request.anchor = -1;
    if (!strcmp(mode, "bad-timeout")) settings.total_ms = 0;
    if (!strcmp(mode, "deadline")) { settings.connect_ms = 200; settings.total_ms = 350; }
    if (!strcmp(mode, "bad-policy")) policy.client_protocol = 0;
    if (!strcmp(mode, "occupied")) {
        assert(gin_history_page_decode(&scope, &request, (const unsigned char *)"[]", 2, &occupied) == GIN_HISTORY_OK);
        page = occupied;
    }
    saved = credential; saved_settings = settings; saved_policy = policy;
    saved_scope = scope; saved_request = request;
    assert(gin_http_global_init() == GIN_HTTP_OK);
    for (i = 0; i < calls; i++) {
        r = gin_history_fetch(!strcmp(mode, "null-settings") ? NULL : &settings,
            !strcmp(mode, "null-policy") ? NULL : &policy,
            !strcmp(mode, "null-credential") ? NULL : &credential,
            !strcmp(mode, "null-topology") ? NULL : !strcmp(mode, "unknown-queries") ? base : topology,
            !strcmp(mode, "null-scope") ? NULL : &scope,
            !strcmp(mode, "null-request") ? NULL : &request,
            !strcmp(mode, "null-out") ? NULL : &page);
        assert(!memcmp(&saved, &credential, sizeof(saved)));
        assert(!memcmp(&saved_settings, &settings, sizeof(settings)));
        assert(!memcmp(&saved_policy, &policy, sizeof(policy)));
        assert(!memcmp(&saved_scope, &scope, sizeof(scope)));
        /* Struct padding is not part of the request value and can remain
         * indeterminate even after an aggregate initializer/assignment. */
        assert(saved_request.cursor == request.cursor &&
            saved_request.anchor == request.anchor && saved_request.limit == request.limit);
        if (occupied) assert(page == occupied);
        else if (r.code != GIN_HISTORY_OK) assert(page == NULL);
        if (r.code == GIN_HISTORY_OK) {
            const struct gin_history_scope *s = gin_history_page_scope(page);
            assert(s && !strcmp(s->origin, scope.origin) && !strcmp(s->target, scope.target));
            assert(s->socket_epoch == scope.socket_epoch);
            assert(gin_history_page_source(page) == GIN_HISTORY_REST);
            assert(gin_history_page_request(page)->anchor == request.anchor);
            if (gin_history_page_count(page)) {
                const struct gin_history_row *row = gin_history_page_row(page, 0);
                assert(row->id == 42 && row->server_time == 1700000000123LL);
                assert(!strcmp(row->body, "owned fixture body"));
            }
        }
        printf("%d %d %d %ld %d %d %zu\n", (int)r.code, (int)r.protocol,
            (int)r.auth, r.http_status, r.auth_attempted, r.get_attempted,
            gin_history_page_count(page));
        gin_history_page_free(page); page = NULL; occupied = NULL;
    }
    gin_http_global_cleanup(); gin_credential_clear(&credential); gin_credential_clear(&saved);
    gin_topology_free(topology); gin_topology_free(base);
    return 0;
}
