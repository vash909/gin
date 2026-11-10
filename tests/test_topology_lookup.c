/* Isolated lookup contract fixtures: only the public snapshot read facade is
 * supplied here. Integrated parser tests separately use real owned snapshots. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_topology.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct gin_topology {
    struct gin_topology_network networks[2];
    struct gin_topology_window windows[2];
};
size_t gin_topology_network_count(const struct gin_topology *t)
{ return t ? 2 : 0; }
size_t gin_topology_window_count(const struct gin_topology *t)
{ return t ? 2 : 0; }
const struct gin_topology_network *gin_topology_network_at(
    const struct gin_topology *t, size_t i)
{ return t && i < 2 ? &t->networks[i] : NULL; }
const struct gin_topology_window *gin_topology_window_at(
    const struct gin_topology *t, size_t i)
{ return t && i < 2 ? &t->windows[i] : NULL; }

int main(void)
{
    struct gin_topology t = {
        {{7, "Net", "Alice", "%4Eet"}, {INT64_C(4294967297), "net", "Bob", "net"}},
        {{.network_id = 7, .role = GIN_REMOTE_CHANNEL},
         {.network_id = INT64_C(4294967297), .role = GIN_REMOTE_QUERY}}
    };
    assert(gin_name_prepare("Net", "#Same", "#", &t.windows[0].name) == 0);
    assert(gin_name_prepare("net", "Peer", "#", &t.windows[1].name) == 0);
    /* One identity-axes tracer: never confuse exact slug, numeric ID and target. */
    assert(gin_topology_find_id(&t, 7) == &t.networks[0]);
    assert(gin_topology_find_id(&t, INT64_C(4294967297)) == &t.networks[1]);
    assert(gin_topology_find_slug(&t, "Net") == &t.networks[0]);
    assert(gin_topology_find_slug(&t, "net") == &t.networks[1]);
    assert(gin_topology_find_window(&t, 7, "#same") == &t.windows[0]);
    assert(gin_topology_find_window(&t, INT64_C(4294967297), "peer") == &t.windows[1]);
    assert(gin_topology_find_id(NULL, 7) == NULL);
    assert(gin_topology_find_id(&t, 0) == NULL);
    assert(gin_topology_find_id(&t, -1) == NULL);
    assert(gin_topology_find_id(&t, 1) == NULL); /* int32 truncation collision. */
    assert(gin_topology_find_id(&t, INT64_MAX) == NULL);
    assert(gin_topology_find_slug(NULL, "Net") == NULL);
    assert(gin_topology_find_slug(&t, NULL) == NULL);
    assert(gin_topology_find_slug(&t, "") == NULL);
    assert(gin_topology_find_slug(&t, "NET") == NULL);
    assert(gin_topology_find_slug(&t, "%4Eet") == NULL);
    assert(gin_topology_find_slug(&t, "Ne") == NULL);
    assert(gin_topology_find_window(NULL, 7, "#same") == NULL);
    assert(gin_topology_find_window(&t, 0, "#same") == NULL);
    assert(gin_topology_find_window(&t, 7, NULL) == NULL);
    assert(gin_topology_find_window(&t, 7, "") == NULL);
    assert(gin_topology_find_window(&t, 7, "#Same") == NULL);
    assert(gin_topology_find_window(&t, 7, "peer") == NULL);
    assert(gin_topology_find_window(&t, INT64_C(4294967297), "#same") == NULL);
    assert(gin_topology_find_window(&t, INT64_C(4294967297), "Peer") == NULL);
    assert(gin_topology_find_window(&t, INT64_C(4294967297), "pe") == NULL);
    {
        char slug[257], stored_slug[256], target[258];
        memset(slug, 'a', sizeof(slug)); slug[255] = 0;
        memcpy(stored_slug, slug, sizeof(stored_slug));
        memset(target, 'b', sizeof(target)); target[0] = '#'; target[256] = 0;
        t.networks[0].slug = stored_slug; t.networks[0].component = stored_slug;
        assert(gin_name_prepare(slug, target, "#", &t.windows[0].name) == 0);
        assert(gin_topology_find_slug(&t, slug) == &t.networks[0]);
        assert(gin_topology_find_window(&t, 7, target) == &t.windows[0]);
        slug[255] = 'a'; slug[256] = 0;
        target[256] = 'b'; target[257] = 0;
        assert(gin_topology_find_slug(&t, slug) == NULL);
        assert(gin_topology_find_window(&t, 7, target) == NULL);
    }
    puts("topology lookup identity axes and boundary controls passed");
    return 0;
}
