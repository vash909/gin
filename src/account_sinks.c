#include "account_sinks_hot.h"
#include "fs_private.h"
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
    const struct gin_account_registry *registry;
    struct gin_account_registry_scope scope;
    char hot_origin[2049], hot_uuid[37], hot_subject[256], hot_types[5];
    struct gin_topology_window retained[SINK_LIMIT];
    char stamps[SINK_LIMIT][GIN_TOPOLOGY_OPENED_AT_MAX + 1];
    char source_network[GIN_CONNECT_TOPICS_MAX][256];
    char source_target[GIN_CONNECT_TOPICS_MAX][257];
    size_t sources;
    uint64_t generation;
    bool hot_failed, hot_cleanup;
    int hot_error;
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
    memset(sinks, 0, sizeof(*sinks));
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

static int
hot_scope_equal(const struct gin_account_registry_scope *a,
                const struct gin_account_registry_scope *b)
{
    return a && b && a->origin && a->subject_uuid && a->topic_subject &&
        a->chantypes && b->origin && b->subject_uuid && b->topic_subject &&
        b->chantypes && a->epoch && a->epoch == b->epoch &&
        !strcmp(a->origin, b->origin) && !strcmp(a->subject_uuid, b->subject_uuid) &&
        !strcmp(a->topic_subject, b->topic_subject) && !strcmp(a->chantypes, b->chantypes);
}
static int
hot_window_equal(const struct gin_topology_window *a,
                 const struct gin_topology_window *b)
{
    return a && b && a->network_id == b->network_id && a->role == b->role &&
        a->joined == b->joined && a->source == b->source &&
        ((!a->opened_at && !b->opened_at) || (a->opened_at && b->opened_at &&
        !strcmp(a->opened_at, b->opened_at))) && a->name.kind == b->name.kind &&
        !strcmp(a->name.input_network_slug, b->name.input_network_slug) &&
        !strcmp(a->name.original_wire_target, b->name.original_wire_target) &&
        !strcmp(a->name.canonical_target, b->name.canonical_target) &&
        !strcmp(a->name.network_component, b->name.network_component) &&
        !strcmp(a->name.window_component, b->name.window_component);
}
static struct gin_account_sinks_hot_result
hot_result(const struct gin_account_sinks *s, enum gin_account_sinks_hot_code code)
{
    struct gin_account_sinks_hot_result r = { GIN_ACCOUNT_SINKS_HOT_OK,
        false, false, false, 0, 0, 0 };
    r.code = code;
    if (s) {
        r.window_count = s->windows; r.generation = s->generation;
        r.cleanup_failed = s->hot_cleanup;
        if (s->hot_failed) { r.code = GIN_ACCOUNT_SINKS_HOT_FAILED; r.error = s->hot_error; }
    }
    return r;
}
struct gin_account_sinks_hot_result
gin_account_sinks_hot_status(const struct gin_account_sinks *s,
    const struct gin_root *root, const struct gin_account_plan *plan,
    const struct gin_account_registry *reg,
    const struct gin_account_registry_scope *scope, uint64_t expected)
{
    if (!s || !root || !plan || !reg || !scope || !s->registry)
        return hot_result(s, GIN_ACCOUNT_SINKS_HOT_INVALID);
    if (s->root != root || s->plan != plan || s->registry != reg ||
        !hot_scope_equal(scope, &s->scope) ||
        !hot_scope_equal(scope, gin_account_registry_scope(reg)))
        return hot_result(s, GIN_ACCOUNT_SINKS_HOT_SCOPE);
    if (expected != s->generation ||
        expected != gin_account_registry_generation(reg))
        return hot_result(s, GIN_ACCOUNT_SINKS_HOT_STALE);
    return hot_result(s, GIN_ACCOUNT_SINKS_HOT_OK);
}

static struct gin_account_sinks_hot_result
hot_root_check(struct gin_account_sinks *s, const struct gin_account_registry_scope *scope)
{
    struct gin_account_sinks_hot_result r = hot_result(s, GIN_ACCOUNT_SINKS_HOT_OK);
    struct gin_account_root_check_result check = gin_account_root_check(s->root,
        scope->origin, scope->subject_uuid, scope->chantypes);
    if (check.code != GIN_ACCOUNT_ROOT_OK || check.cleanup_failed) {
        r.code = check.code == GIN_ACCOUNT_ROOT_MISMATCH ?
            GIN_ACCOUNT_SINKS_HOT_SCOPE : GIN_ACCOUNT_SINKS_HOT_IO;
        r.error = check.code == GIN_ACCOUNT_ROOT_MISMATCH ||
            check.code == GIN_ACCOUNT_ROOT_INVALID || check.code == GIN_ACCOUNT_ROOT_UNSAFE ? EPERM : EIO;
        s->hot_failed = true; s->hot_error = r.error;
        s->hot_cleanup = check.cleanup_failed; r.cleanup_failed = check.cleanup_failed;
    }
    return r;
}
static int
hot_prefix(const struct gin_account_sinks *s, const struct gin_account_registry *reg, int initial)
{
    size_t i;
    if (gin_account_registry_network_count(reg) != s->networks ||
        gin_account_registry_window_count(reg) < s->windows) return 0;
    for (i = 0; i < s->networks; ++i) {
        const struct gin_topology_network *a = gin_account_plan_network_at(s->plan, i);
        const struct gin_topology_network *b = gin_account_registry_network_at(reg, i);
        if (!b || a->id != b->id || strcmp(a->slug,b->slug) ||
            strcmp(a->own_nick,b->own_nick) || strcmp(a->component,b->component)) return 0;
    }
    for (i = 0; i < s->windows; ++i)
        if (!hot_window_equal(initial ? gin_account_plan_window_at(s->plan,i) :
            &s->retained[i],gin_account_registry_window_at(reg,i))) return 0;
    if (!initial) {
        if (gin_account_registry_source_count(reg) < s->sources) return 0;
        for (i = 0; i < s->sources; ++i) {
            const struct gin_connect_subscription *v = gin_account_registry_source_at(reg,i);
            if (!v || strcmp(v->network ? v->network : "",s->source_network[i]) ||
                strcmp(v->target ? v->target : "",s->source_target[i])) return 0;
        }
    }
    return 1;
}
static void
hot_remember(struct gin_account_sinks *s)
{
    size_t i;
    for (i = 0; i < s->windows; ++i) {
        s->retained[i] = *gin_account_registry_window_at(s->registry,i);
        if (s->retained[i].opened_at) {
            strcpy(s->stamps[i],s->retained[i].opened_at);
            s->retained[i].opened_at = s->stamps[i];
        }
    }
    s->sources = gin_account_registry_source_count(s->registry);
    for (i = 0; i < s->sources; ++i) {
        const struct gin_connect_subscription *v = gin_account_registry_source_at(s->registry,i);
        strcpy(s->source_network[i],v->network ? v->network : "");
        strcpy(s->source_target[i],v->target ? v->target : "");
    }
}
struct gin_account_sinks_hot_result
gin_account_sinks_hot_bind(struct gin_account_sinks *s,
    const struct gin_account_registry *reg,
    const struct gin_account_registry_scope *scope, uint64_t expected)
{
    const struct gin_account_registry_scope *rs = gin_account_registry_scope(reg);
    struct gin_account_sinks_hot_result r = hot_result(s,GIN_ACCOUNT_SINKS_HOT_INVALID);
    const struct gin_connect_subscription *subs;
    size_t i, count;
    if (!s || s->hot_failed || s->registry || !reg || !scope) return r;
    if (!hot_scope_equal(scope,rs)) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_SCOPE);
    if (expected != gin_account_registry_generation(reg)) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_STALE);
    /* Bind only the initial cold generation, even if a later registry happens
     * to have an identical cold layout through own-nick source reuse. */
    if (expected != 0) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    if (gin_account_registry_source_count(reg) > GIN_CONNECT_TOPICS_MAX ||
        s->networks + s->windows > GIN_CONNECT_TOPICS_MAX) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_LIMIT);
    if (gin_account_registry_window_count(reg) != s->windows || !hot_prefix(s,reg,1))
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    subs = gin_account_plan_subscriptions(s->plan,&count);
    if (gin_account_registry_source_count(reg) != count + 1) return r;
    for (i = 0; i < count; ++i) {
        const struct gin_connect_subscription *v = gin_account_registry_source_at(reg,i+1);
        if (!v || strcmp(v->network ? v->network : "",subs[i].network ? subs[i].network : "") ||
            strcmp(v->target ? v->target : "",subs[i].target ? subs[i].target : "")) return r;
    }
    r = hot_root_check(s,scope);
    if (r.code != GIN_ACCOUNT_SINKS_HOT_OK) return r;
    strcpy(s->hot_origin,rs->origin); strcpy(s->hot_uuid,rs->subject_uuid);
    strcpy(s->hot_subject,rs->topic_subject); strcpy(s->hot_types,rs->chantypes);
    s->scope.origin=s->hot_origin; s->scope.subject_uuid=s->hot_uuid;
    s->scope.topic_subject=s->hot_subject; s->scope.chantypes=s->hot_types; s->scope.epoch=rs->epoch;
    s->registry=reg; s->generation=expected; hot_remember(s);
    return hot_result(s,GIN_ACCOUNT_SINKS_HOT_OK);
}
struct gin_account_sinks_hot_result
gin_account_sinks_hot_append(struct gin_account_sinks *s,
    const struct gin_account_registry_scope *scope, uint64_t expected, size_t index)
{
    struct gin_account_sinks_hot_result r = hot_result(s,GIN_ACCOUNT_SINKS_HOT_INVALID);
    const struct gin_topology_window *w;
    struct gin_fs_pair pair = { -1, -1 };
    struct gin_fs_pair_result fs;
    size_t i;
    if (!s || s->hot_failed || !s->registry || !scope) return r;
    if (!hot_scope_equal(scope,&s->scope) ||
        !hot_scope_equal(gin_account_registry_scope(s->registry),&s->scope))
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_SCOPE);
    if (expected != gin_account_registry_generation(s->registry) || expected < s->generation)
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_STALE);
    if (gin_account_registry_source_count(s->registry) > GIN_CONNECT_TOPICS_MAX ||
        gin_account_registry_window_count(s->registry) > SINK_LIMIT ||
        gin_account_registry_network_count(s->registry) > SINK_LIMIT)
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_LIMIT);
    if (!hot_prefix(s,s->registry,0)) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    if (index > s->windows || index >= gin_account_registry_window_count(s->registry)) return r;
    if (index < s->windows) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_OK);
    if (s->windows == SINK_LIMIT || s->networks + s->windows >= GIN_CONNECT_TOPICS_MAX)
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_LIMIT);
    w = gin_account_registry_window_at(s->registry,index);
    if (!w || w->role != GIN_REMOTE_QUERY || w->joined || w->source != GIN_CHANNEL_NONE || !w->opened_at)
        return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    for (i=0; i<s->networks; ++i)
        if (gin_account_registry_network_at(s->registry,i)->id == w->network_id &&
            !strcmp(gin_account_registry_network_at(s->registry,i)->slug,w->name.input_network_slug)) break;
    if (i == s->networks) return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    {
        struct gin_topology_window normalized = *w;
        if (gin_account_name_prepare(w->name.input_network_slug,
            w->name.original_wire_target,s->scope.chantypes,&normalized.name) ||
            normalized.name.kind != GIN_WINDOW_NONCHANNEL || !hot_window_equal(w,&normalized))
            return hot_result(s,GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED);
    }
    r = hot_root_check(s,&s->scope);
    if (r.code != GIN_ACCOUNT_SINKS_HOT_OK) return r;
    fs = gin_root_pair_prepare_named(s->root,w->name.input_network_slug,
        w->name.original_wire_target,s->scope.chantypes,gin_account_name_prepare,&pair);
    r.paths_attempted = fs.mutation_attempted != 0;
    if (fs.error || fs.cleanup_failed) {
        r.code=GIN_ACCOUNT_SINKS_HOT_IO; r.error=fs.error ? fs.error : EIO;
        r.cleanup_failed=fs.cleanup_failed != 0; s->hot_failed=true;
        s->hot_error=r.error; s->hot_cleanup=r.cleanup_failed; return r;
    }
    s->window[index].input=pair.input; s->window[index].output=pair.output;
    ++s->windows; s->generation=expected; hot_remember(s);
    r=hot_result(s,GIN_ACCOUNT_SINKS_HOT_OK); r.added=true;
    r.paths_attempted=fs.mutation_attempted != 0; return r;
}
