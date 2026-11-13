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
int main(void)
{
    int phase, result = 0;
    for (phase = 0; phase < 3; ++phase) {
        int status = exercise(phase);
        if (status) result = status;
    }
    return result;
}
