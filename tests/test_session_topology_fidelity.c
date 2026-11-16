#include "session_live_projection.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Isolated test process only. Native constructors release their temporary JSON
 * DOMs before returning; returned snapshots/records use native owned storage. */
static size_t allocation_call, failure_at;
static void *fault_malloc(size_t bytes)
{
    if (allocation_call++ == failure_at) return NULL;
    return malloc(bytes);
}
static const char target[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmno";
static const char expected_target[] =
    "abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz0123456789abcdefghijklmno";
static const char boot[] =
    "{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":\"OwnA\"}],"
    "\"channels\":{\"NetA\":[]},\"heads\":{}}";
static const char long_boot[] =
    "{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":"
    "\"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmno\"}],"
    "\"channels\":{\"NetA\":[]},\"heads\":{}}";

static int faithful(const struct gin_topology *snapshot, int phase)
{
    const struct gin_topology_network *network = gin_topology_network_at(snapshot, 0);
    const struct gin_topology_window *window;
    if (!network || gin_topology_network_count(snapshot) != 1 ||
        network->id != INT64_C(3000000001) || strcmp(network->slug, "NetA") ||
        strcmp(network->component, "%4Eet%41") ||
        strcmp(network->own_nick, phase == 0 ? target : "OwnA")) return 0;
    if (phase == 0) return gin_topology_window_count(snapshot) == 0 &&
        !gin_topology_queries_known(snapshot);
    window = gin_topology_window_at(snapshot, 0);
    return window && gin_topology_window_count(snapshot) == 1 &&
        gin_topology_queries_known(snapshot) && window->role == GIN_REMOTE_QUERY &&
        window->network_id == INT64_C(3000000001) &&
        !strcmp(window->name.original_wire_target, target) &&
        !strcmp(window->name.canonical_target, expected_target) &&
        window->opened_at && !strcmp(window->opened_at, "fixture");
}

static int exercise(int phase)
{
    struct gin_topology *base = NULL;
    struct gin_session_settings settings = {0};
    struct gin_live_projection context = {0};
    struct gin_live_view view = {0};
    json_malloc_t original_malloc;
    json_free_t original_free;
    char payload[1024];
    size_t index, successes = 0, failures = 0, violations = 0, max_calls = 0;
    int length;
    if (gin_topology_parse_boot((const unsigned char *)boot, sizeof(boot)-1u,
        "#", &base) != GIN_TOPOLOGY_OK) return 2;
    strcpy(settings.local.network, "NetA");
    strcpy(settings.local.target, target);
    strcpy(settings.local.chantypes, "#"); strcpy(settings.mode, "plain");
    context.settings = &settings; context.subject = "Alice";
    context.own_nick = "OwnA"; context.network_id = INT64_C(3000000001);
    context.query = 1;
    length = snprintf(payload, sizeof(payload),
        "{\"kind\":\"query_windows_list\",\"windows\":{\"3000000001\":[{"
        "\"network_id\":3000000001,\"target_nick\":\"%s\",\"opened_at\":\"fixture\"}]}}", target);
    if (length < 0 || (size_t)length >= sizeof(payload)) return 2;
    view.fields.payload.data = (const unsigned char *)payload;
    view.fields.payload.length = (size_t)length;
    view.source_index = 0;
    json_get_alloc_funcs(&original_malloc, &original_free);
    /* First require successful, exact non-injected setup. The subsequent 256
     * isolated fail-at-N calls must either reject or publish original values. */
    for (index = 0; index <= 256u; ++index) {
        struct gin_topology *next = NULL;
        struct gin_render_record *record = NULL;
        int terminal = 0, code, exact;
        allocation_call = 0;
        failure_at = index ? index - 1u : (size_t)-1;
        json_set_alloc_funcs(fault_malloc, original_free);
        if (phase == 0)
            code = (int)gin_topology_parse_boot((const unsigned char *)long_boot,
                sizeof(long_boot)-1u, "#", &next);
        else if (phase == 1)
            code = (int)gin_topology_apply_queries(base,
                (const unsigned char *)payload, (size_t)length, &next);
        else
            code = (int)gin_live_project(&view, &context, base, &next, &record, &terminal);
        /* No Jansson objects escape the invoked native APIs. */
        json_set_alloc_funcs(original_malloc, original_free);
        if (allocation_call > max_calls) max_calls = allocation_call;
        exact = code != 0 || (next && faithful(next, phase) && !record && !terminal);
        if (!index && (code != 0 || !exact)) {
            fprintf(stderr, "normal fixture rejected or changed: phase=%d code=%d\n", phase, code);
            gin_topology_free(next); gin_render_record_free(record); gin_topology_free(base);
            return 2;
        }
        if (code == 0) {
            ++successes;
            if (!exact) {
                const struct gin_topology_window *window = gin_topology_window_at(next, 0);
                fprintf(stderr, "ROUTING_FIDELITY_VIOLATION phase=%d fail_at=%zu actual=%s\n",
                    phase, failure_at, window ? window->name.canonical_target : "network metadata changed");
                ++violations;
            }
        } else {
            ++failures;
            if (next || record) {
                fprintf(stderr, "failure published owned output: phase=%d code=%d\n", phase, code);
                ++violations;
            }
        }
        gin_topology_free(next); gin_render_record_free(record);
        if (gin_topology_queries_known(base) || gin_topology_window_count(base) != 0)
            ++violations;
    }
    gin_topology_free(base);
    printf("Topology fidelity phase=%d exact_successes=%zu rejected=%zu violations=%zu max_allocations=%zu\n",
        phase, successes, failures, violations, max_calls);
    return violations || !successes || !failures || max_calls >= 256u ? 1 : 0;
}

/* The owner stays selected when an auxiliary JOIN creates a window absent
 * from boot. Only well-formed, non-message lifecycle rows are ignore cases;
 * they neither authorize a new window nor publish output or terminal state. */
static int projection_case(const char *label, const char *payload,
    const struct gin_live_projection *context, const struct gin_topology *base,
    size_t source, const char *topic, enum gin_session_code expected,
    int expected_record, int expected_terminal)
{
    struct gin_live_view view = {0};
    struct gin_topology *next = NULL;
    struct gin_render_record *record = NULL;
    int terminal = 7, failed;
    enum gin_session_code actual;
    view.fields.payload.data = (const unsigned char *)payload;
    view.fields.payload.length = strlen(payload);
    view.fields.topic = topic; view.fields.event = "event";
    view.fields.join_ref = "3"; view.source_join_ref = "3";
    view.observed_epoch = 1; view.source_index = source;
    actual = gin_live_project(&view, context, base, &next, &record, &terminal);
    failed = actual != expected || next || !!record != expected_record ||
        terminal != expected_terminal;
    printf("Projection %s expected=%d actual=%d record=%d terminal=%d %s\n",
        label, (int)expected, (int)actual, !!record, terminal,
        failed ? "FAIL" : "PASS");
    gin_topology_free(next); gin_render_record_free(record);
    return failed;
}

static int unselected_metadata(void)
{
    static const char selected_boot[] =
        "{\"networks\":[{\"id\":3000000001,\"slug\":\"NetA\",\"nick\":\"OwnA\"}],"
        "\"channels\":{\"NetA\":[{\"name\":\"#Room\",\"joined\":true,"
        "\"source\":\"joined\"}]},\"heads\":{}}";
    static const char *kinds[] = {"joined", "window_pending", "window_invited",
        "window_invite_declined", "join_failed", "kicked"};
    static const char *states[] = {"joined", "pending", "invited", "", "failed", "kicked"};
    struct gin_topology *base = NULL, *empty = NULL;
    struct gin_session_settings settings = {0};
    struct gin_live_projection context = {0};
    char payload[512];
    size_t i;
    int failures = 0;
    if (gin_topology_parse_boot((const unsigned char *)selected_boot,
        sizeof(selected_boot)-1u, "#", &base) != GIN_TOPOLOGY_OK ||
        gin_topology_parse_boot((const unsigned char *)boot, sizeof(boot)-1u,
        "#", &empty) != GIN_TOPOLOGY_OK) {
        gin_topology_free(base); gin_topology_free(empty); return 2;
    }
    strcpy(settings.local.network, "NetA"); strcpy(settings.local.target, "#Room");
    strcpy(settings.local.chantypes, "#"); strcpy(settings.mode, "plain");
    context.settings = &settings; context.subject = "Alice";
    context.own_nick = "OwnA"; context.source_target = "#Room";
    context.network_id = INT64_C(3000000001);
    if (!gin_topology_find_window(base, context.network_id, "#room") ||
        gin_topology_find_window(base, context.network_id, "#other")) {
        gin_topology_free(base); gin_topology_free(empty); return 2;
    }
    for (i = 0; i < sizeof(kinds)/sizeof(kinds[0]); ++i) {
        int n = snprintf(payload, sizeof(payload),
            "{\"kind\":\"%s\",\"network\":\"NetA\",\"channel\":\"#Other\","
            "\"state\":\"%s\"}", kinds[i], states[i]);
        if (n < 0 || (size_t)n >= sizeof(payload)) { failures++; continue; }
        failures += projection_case(kinds[i], payload, &context, base, 0,
            "grappa:user:Alice", GIN_SESSION_OK, 0, 0);
    }
#define CASE(label, body, expected, output, stop) \
    failures += projection_case(label, body, &context, base, 0, \
        "grappa:user:Alice/network:NetA/channel:#room", expected, output, stop)
#define JOINED "{\"kind\":\"joined\",\"network\":\"NetA\",\"channel\":\"#rOOM\",\"state\":\"joined\"}"
    CASE("selected-joined", JOINED, GIN_SESSION_OK, 1, 0);
    CASE("selected-kicked", "{\"kind\":\"kicked\",\"network\":\"NetA\",\"channel\":\"#Room\",\"state\":\"kicked\"}", GIN_SESSION_OK, 1, 1);
    failures += projection_case("selected-missing", JOINED, &context, empty, 0,
        "grappa:user:Alice/network:NetA/channel:#room", GIN_SESSION_ROUTE, 0, 0);
    context.query = 1;
    CASE("selected-wrong-role", JOINED, GIN_SESSION_ROUTE, 0, 0);
    context.query = 0;
    CASE("selected-bad-state", "{\"kind\":\"joined\",\"network\":\"NetA\",\"channel\":\"#Room\",\"state\":\"pending\"}", GIN_SESSION_ROUTE, 0, 0);
    CASE("unselected-malformed-channel", "{\"kind\":\"joined\",\"network\":\"NetA\",\"channel\":42,\"state\":\"joined\"}", GIN_SESSION_ROUTE, 0, 0);
    CASE("unknown-network", "{\"kind\":\"joined\",\"network\":\"Unknown\",\"channel\":\"#Other\",\"state\":\"joined\"}", GIN_SESSION_ROUTE, 0, 0);
    CASE("invalid-target", "{\"kind\":\"joined\",\"network\":\"NetA\",\"channel\":\"\",\"state\":\"joined\"}", GIN_SESSION_ROUTE, 0, 0);
    CASE("encrypted-kind", "{\"kind\":\"e2e_future\",\"network\":\"NetA\",\"channel\":\"#Other\"}", GIN_SESSION_UNSUPPORTED, 0, 0);
    CASE("encrypted-message", "{\"kind\":\"message\",\"message\":{\"kind\":\"e2e_future\",\"network\":\"NetA\",\"channel\":\"#Other\"}}", GIN_SESSION_UNSUPPORTED, 0, 0);
    CASE("message-source-mismatch", "{\"kind\":\"message\",\"message\":{\"kind\":\"privmsg\",\"network\":\"NetA\",\"channel\":\"#Other\",\"sender\":\"Peer\",\"body\":\"text\",\"server_time\":0}}", GIN_SESSION_ROUTE, 0, 0);
    context.source_target = "#Other";
    CASE("message-missing-window", "{\"kind\":\"message\",\"message\":{\"kind\":\"privmsg\",\"network\":\"NetA\",\"channel\":\"#Other\",\"sender\":\"Peer\",\"body\":\"text\",\"server_time\":0}}", GIN_SESSION_ROUTE, 0, 0);
    context.source_target = "#Room";
    failures += projection_case("selected-wrong-topic", JOINED, &context, base, 0,
        "grappa:user:Other/network:NetA/channel:#room", GIN_SESSION_ROUTE, 0, 0);
    failures += projection_case("query-list-wrong-source",
        "{\"kind\":\"query_windows_list\",\"windows\":{}}", &context, base, 1,
        "grappa:user:Alice", GIN_SESSION_ROUTE, 0, 0);
    if (gin_topology_window_count(base) != 1 ||
        gin_topology_find_window(base, context.network_id, "#other")) failures++;
    gin_topology_free(base); gin_topology_free(empty);
#undef JOINED
#undef CASE
    return failures ? 1 : 0;
}

int main(void)
{
    int phase, result = 0;
    for (phase = 0; phase < 3; ++phase) {
        int status = exercise(phase);
        if (status) result = status;
    }
    if (unselected_metadata()) result = 1;
    return result;
}
