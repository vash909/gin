#include "gin_topology.h"

#include <string.h>

const struct gin_topology_network *gin_topology_find_id(
    const struct gin_topology *topology, int64_t id)
{
    size_t count;
    size_t i;

    if (topology == NULL || id <= 0) {
        return NULL;
    }
    count = gin_topology_network_count(topology);
    if (count > (size_t)GIN_TOPOLOGY_NETWORK_MAX) {
        count = (size_t)GIN_TOPOLOGY_NETWORK_MAX;
    }
    for (i = 0; i < count; i++) {
        const struct gin_topology_network *network =
            gin_topology_network_at(topology, i);
        if (network != NULL && network->id == id) {
            return network;
        }
    }
    return NULL;
}

const struct gin_topology_network *gin_topology_find_slug(
    const struct gin_topology *topology, const char *slug)
{
    size_t length;
    size_t count;
    size_t i;

    if (topology == NULL || slug == NULL) {
        return NULL;
    }
    length = strnlen(slug, (size_t)256);
    if (length == 0 || length > (size_t)255) {
        return NULL;
    }
    count = gin_topology_network_count(topology);
    if (count > (size_t)GIN_TOPOLOGY_NETWORK_MAX) {
        count = (size_t)GIN_TOPOLOGY_NETWORK_MAX;
    }
    for (i = 0; i < count; i++) {
        const struct gin_topology_network *network =
            gin_topology_network_at(topology, i);
        if (network != NULL && network->slug != NULL &&
            strcmp(network->slug, slug) == 0) {
            return network;
        }
    }
    return NULL;
}

const struct gin_topology_window *gin_topology_find_window(
    const struct gin_topology *topology, int64_t network_id,
    const char *canonical_target)
{
    size_t length;
    size_t count;
    size_t i;

    if (topology == NULL || network_id <= 0 || canonical_target == NULL) {
        return NULL;
    }
    length = strnlen(canonical_target, (size_t)257);
    if (length == 0 || length > (size_t)256) {
        return NULL;
    }
    count = gin_topology_window_count(topology);
    if (count > (size_t)GIN_TOPOLOGY_WINDOW_MAX) {
        count = (size_t)GIN_TOPOLOGY_WINDOW_MAX;
    }
    for (i = 0; i < count; i++) {
        const struct gin_topology_window *window =
            gin_topology_window_at(topology, i);
        if (window != NULL && window->network_id == network_id &&
            strcmp(window->name.canonical_target, canonical_target) == 0) {
            return window;
        }
    }
    return NULL;
}
