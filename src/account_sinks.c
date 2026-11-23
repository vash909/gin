#include "account_sinks.h"
#include "account_fs.h"
#include "account_naming.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define SINK_LIMIT (GIN_CONNECT_TOPICS_MAX - 1)
struct sink_pair {
    int input;
    int output;
};
struct gin_account_sinks {
    const struct gin_root *root;
    const struct gin_account_plan *plan;
    size_t networks;
    size_t windows;
    struct sink_pair network[SINK_LIMIT];
    struct sink_pair window[SINK_LIMIT];
};

static int
plan_valid(const struct gin_account_plan *plan, const char *chantypes)
{
    size_t i, j, nc, wc;
    char component[256];
    if (!plan || !chantypes || gin_chantypes_validate(chantypes))
        return 0;
    nc = gin_account_plan_network_count(plan);
    wc = gin_account_plan_window_count(plan);
    if (nc > SINK_LIMIT || wc > SINK_LIMIT ||
        gin_account_plan_topic_count(plan) > GIN_CONNECT_TOPICS_MAX)
        return 0;
    for (i = 0; i < nc; ++i) {
        const struct gin_topology_network *n = gin_account_plan_network_at(plan, i);
        if (!n || n->id <= 0 || !n->slug || !n->component || !n->own_nick ||
            gin_network_encode(n->slug, component, sizeof(component)) ||
            strcmp(component, n->component))
            return 0;
        for (j = 0; j < i; ++j) {
            const struct gin_topology_network *other = gin_account_plan_network_at(plan, j);
            if (other->id == n->id || !strcmp(other->component, n->component))
                return 0;
        }
    }
    for (i = 0; i < wc; ++i) {
        const struct gin_topology_window *w = gin_account_plan_window_at(plan, i);
        struct gin_name name;
        if (!w)
            return 0;
        for (j = 0; j < nc; ++j)
            if (gin_account_plan_network_at(plan, j)->id == w->network_id)
                break;
        if (j == nc ||
            (w->role != GIN_REMOTE_CHANNEL && w->role != GIN_REMOTE_QUERY))
            return 0;
        if (w->role == GIN_REMOTE_CHANNEL) {
            if (!w->joined || w->source != GIN_CHANNEL_JOINED || w->opened_at ||
                w->name.kind != GIN_WINDOW_CHANNEL)
                return 0;
        } else if (w->joined || w->source != GIN_CHANNEL_NONE || !w->opened_at ||
                   w->name.kind != GIN_WINDOW_NONCHANNEL) {
            return 0;
        }
        if (gin_account_name_prepare(gin_account_plan_network_at(plan, j)->slug,
                                    w->name.original_wire_target, chantypes, &name) ||
            strcmp(name.input_network_slug, w->name.input_network_slug) ||
            strcmp(name.original_wire_target, w->name.original_wire_target) ||
            strcmp(name.canonical_target, w->name.canonical_target) ||
            strcmp(name.network_component, w->name.network_component) ||
            strcmp(name.window_component, w->name.window_component) ||
            name.kind != w->name.kind)
            return 0;
    }
    return 1;
}

int
gin_account_sinks_close(struct gin_account_sinks *sinks)
{
    size_t i;
    int error = 0;
    if (!sinks)
        return 0;
    for (i = 0; i < SINK_LIMIT; ++i) {
        if (sinks->network[i].input >= 0 && close(sinks->network[i].input) < 0 && !error)
            error = errno;
        if (sinks->network[i].output >= 0 && close(sinks->network[i].output) < 0 && !error)
            error = errno;
        if (sinks->window[i].input >= 0 && close(sinks->window[i].input) < 0 && !error)
            error = errno;
        if (sinks->window[i].output >= 0 && close(sinks->window[i].output) < 0 && !error)
            error = errno;
    }
    free(sinks);
    if (error) {
        errno = error;
        return -1;
    }
    return 0;
}

struct gin_account_sinks_result
gin_account_sinks_new(const struct gin_root *root,
                      const struct gin_credential *credential,
                      const struct gin_account_plan *plan, const char *chantypes,
                      struct gin_account_sinks **out)
{
    struct gin_account_sinks_result result = {
        GIN_ACCOUNT_SINKS_INVALID, false, { GIN_ACCOUNT_ROOT_INVALID, false }, false
    };
    struct gin_account_sinks *sinks;
    size_t i;
    int saved_errno;
    if (!root || root->rootfd < 0 || root->lockfd < 0 || !credential ||
        !out || *out || !plan_valid(plan, chantypes)) {
        errno = EINVAL;
        return result;
    }
    sinks = malloc(sizeof(*sinks));
    if (!sinks) {
        result.code = GIN_ACCOUNT_SINKS_NOMEM;
        return result;
    }
    sinks->root = root;
    sinks->plan = plan;
    sinks->networks = gin_account_plan_network_count(plan);
    sinks->windows = gin_account_plan_window_count(plan);
    for (i = 0; i < SINK_LIMIT; ++i) {
        sinks->network[i].input = -1;
        sinks->network[i].output = -1;
        sinks->window[i].input = -1;
        sinks->window[i].output = -1;
    }
    result.binding_attempted = true;
    result.binding = gin_account_root_bind(root, credential, chantypes);
    if (result.binding.code != GIN_ACCOUNT_ROOT_OK) {
        result.code = GIN_ACCOUNT_SINKS_BINDING;
        goto fail;
    }
    result.code = GIN_ACCOUNT_SINKS_IO;
    for (i = 0; i < sinks->networks; ++i)
        if (gin_account_network_init(root, gin_account_plan_network_at(plan, i)->slug))
            goto fail;
    for (i = 0; i < sinks->windows; ++i) {
        const struct gin_name *name = &gin_account_plan_window_at(plan, i)->name;
        if (gin_account_root_init(root, name->input_network_slug,
                                  name->original_wire_target, chantypes))
            goto fail;
    }
    for (i = 0; i < sinks->networks; ++i) {
        const char *slug = gin_account_plan_network_at(plan, i)->slug;
        sinks->network[i].input = gin_account_network_input_open(root, slug);
        if (sinks->network[i].input < 0)
            goto fail;
        sinks->network[i].output = gin_account_network_output_open(root, slug);
        if (sinks->network[i].output < 0)
            goto fail;
    }
    for (i = 0; i < sinks->windows; ++i) {
        const struct gin_name *name = &gin_account_plan_window_at(plan, i)->name;
        sinks->window[i].input = gin_account_root_input_open(root,
            name->input_network_slug, name->original_wire_target, chantypes);
        if (sinks->window[i].input < 0)
            goto fail;
        sinks->window[i].output = gin_account_root_output_open(root,
            name->input_network_slug, name->original_wire_target, chantypes);
        if (sinks->window[i].output < 0)
            goto fail;
    }
    result.code = GIN_ACCOUNT_SINKS_OK;
    *out = sinks;
    return result;
fail:
    saved_errno = errno;
    result.cleanup_failed = gin_account_sinks_close(sinks) < 0;
    errno = saved_errno;
    return result;
}

size_t
gin_account_sinks_network_count(const struct gin_account_sinks *sinks)
{
    return sinks ? sinks->networks : 0;
}
size_t
gin_account_sinks_window_count(const struct gin_account_sinks *sinks)
{
    return sinks ? sinks->windows : 0;
}
int
gin_account_sinks_network_input(const struct gin_account_sinks *sinks, size_t index)
{
    return sinks && index < sinks->networks ? sinks->network[index].input : -1;
}
int
gin_account_sinks_network_output(const struct gin_account_sinks *sinks, size_t index)
{
    return sinks && index < sinks->networks ? sinks->network[index].output : -1;
}
int
gin_account_sinks_window_input(const struct gin_account_sinks *sinks, size_t index)
{
    return sinks && index < sinks->windows ? sinks->window[index].input : -1;
}
int
gin_account_sinks_window_output(const struct gin_account_sinks *sinks, size_t index)
{
    return sinks && index < sinks->windows ? sinks->window[index].output : -1;
}
