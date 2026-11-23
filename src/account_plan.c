#include "account_plan.h"
#include "account_naming.h"
#include "connect_values.h"
#include <stdlib.h>
#include <string.h>

/* Fixed bounded allocation, independent of the potentially 4096-window input.
 * <=63 Native nonroot topics also bounds retained windows and networks. */
struct gin_account_plan {
    struct gin_topology_network networks[GIN_CONNECT_TOPICS_MAX];
    struct gin_topology_window windows[GIN_CONNECT_TOPICS_MAX];
    char slugs[GIN_CONNECT_TOPICS_MAX][256];
    char nicks[GIN_CONNECT_TOPICS_MAX][257];
    char components[GIN_CONNECT_TOPICS_MAX][256];
    char stamps[GIN_CONNECT_TOPICS_MAX][GIN_TOPOLOGY_OPENED_AT_MAX + 1];
    struct gin_connect_subscription subscriptions[GIN_CONNECT_TOPICS_MAX];
    char topics[GIN_CONNECT_TOPICS_MAX][GIN_WS_TOPIC_CAP];
    size_t networks_count, windows_count, subscriptions_count, topics_count;
};
static enum gin_account_plan_code add(struct gin_account_plan *p,
    const char *subject, const char *network, const char *target)
{
    char topic[GIN_WS_TOPIC_CAP]; size_t n, i;
    enum gin_ws_value_code c = gin_ws_topic(subject, network, target, topic, sizeof(topic), &n);
    if (c != GIN_WS_VALUE_OK) return c == GIN_WS_VALUE_LIMIT ? GIN_ACCOUNT_PLAN_LIMIT : GIN_ACCOUNT_PLAN_INVALID;
    for (i = 0; i < p->topics_count; i++)
        if (!strcmp(topic, p->topics[i])) return GIN_ACCOUNT_PLAN_OK;
    if (p->topics_count == GIN_CONNECT_TOPICS_MAX) return GIN_ACCOUNT_PLAN_LIMIT;
    memcpy(p->topics[p->topics_count++], topic, n + 1);
    if (network) {
        p->subscriptions[p->subscriptions_count].network = network;
        p->subscriptions[p->subscriptions_count++].target = target;
    }
    return GIN_ACCOUNT_PLAN_OK;
}
void gin_account_plan_free(struct gin_account_plan *p) { free(p); }
size_t gin_account_plan_network_count(const struct gin_account_plan *p) { return p ? p->networks_count : 0; }
size_t gin_account_plan_window_count(const struct gin_account_plan *p) { return p ? p->windows_count : 0; }
size_t gin_account_plan_topic_count(const struct gin_account_plan *p) { return p ? p->topics_count : 0; }
const struct gin_topology_network *gin_account_plan_network_at(const struct gin_account_plan *p, size_t i)
{ return p && i < p->networks_count ? &p->networks[i] : NULL; }
const struct gin_topology_window *gin_account_plan_window_at(const struct gin_account_plan *p, size_t i)
{ return p && i < p->windows_count ? &p->windows[i] : NULL; }
const struct gin_connect_subscription *gin_account_plan_subscriptions(const struct gin_account_plan *p, size_t *count)
{ if (count) *count = p ? p->subscriptions_count : 0; return p ? p->subscriptions : NULL; }

enum gin_account_plan_code gin_account_plan_new(const struct gin_topology *t,
    const char *chantypes, const char *subject, struct gin_account_plan **out)
{
    struct gin_account_plan *p;
    size_t i, j, nc, wc;
    enum gin_account_plan_code code;
    if (!t || !out || *out || !subject || !chantypes || gin_chantypes_validate(chantypes))
        return GIN_ACCOUNT_PLAN_INVALID;
    if (!gin_topology_queries_known(t)) return GIN_ACCOUNT_PLAN_QUERIES_UNKNOWN;
    nc = gin_topology_network_count(t); wc = gin_topology_window_count(t);
    if (nc >= GIN_CONNECT_TOPICS_MAX || wc > GIN_TOPOLOGY_WINDOW_MAX) return GIN_ACCOUNT_PLAN_LIMIT;
    p = calloc(1, sizeof(*p)); if (!p) return GIN_ACCOUNT_PLAN_NOMEM;
    code = add(p, subject, NULL, NULL); if (code) goto fail;
    for (i = 0; i < nc; i++) {
        const struct gin_topology_network *n = gin_topology_network_at(t, i);
        struct gin_topology_network *d = &p->networks[i];
        if (!n || n->id <= 0 || !n->slug || !n->component || !n->own_nick ||
            strlen(n->slug) > 255 || strlen(n->component) > 255 || strlen(n->own_nick) > 256) {
            code = GIN_ACCOUNT_PLAN_INVALID; goto fail;
        }
        strcpy(p->slugs[i], n->slug); strcpy(p->components[i], n->component); strcpy(p->nicks[i], n->own_nick);
        d->id = n->id; d->slug = p->slugs[i]; d->component = p->components[i]; d->own_nick = p->nicks[i];
        p->networks_count++;
        code = add(p,subject,d->slug,NULL); if (code) goto fail;
    }
    /* Status is transport-only, not a query or filesystem window. */
    for (i = 0; i < nc; i++) {
        code = add(p,subject,p->networks[i].slug,"$server"); if (code) goto fail;
    }
    for (i = 0; i < wc; i++) {
        const struct gin_topology_window *w = gin_topology_window_at(t,i);
        struct gin_topology_window *d;
        if (!w || (w->role != GIN_REMOTE_CHANNEL && w->role != GIN_REMOTE_QUERY)) { code = GIN_ACCOUNT_PLAN_INVALID; goto fail; }
        if (w->role == GIN_REMOTE_CHANNEL && !w->joined) continue;
        if ((w->role == GIN_REMOTE_CHANNEL && w->source != GIN_CHANNEL_JOINED) ||
            (w->role == GIN_REMOTE_QUERY && (w->joined || w->source != GIN_CHANNEL_NONE || !w->opened_at))) {
            code = GIN_ACCOUNT_PLAN_INVALID; goto fail;
        }
        for (j = 0; j < nc && p->networks[j].id != w->network_id; j++) { }
        if (j == nc) { code = GIN_ACCOUNT_PLAN_INVALID; goto fail; }
        if (p->windows_count >= GIN_CONNECT_TOPICS_MAX - 1) { code = GIN_ACCOUNT_PLAN_LIMIT; goto fail; }
        d = &p->windows[p->windows_count]; *d = *w;
        if (gin_account_name_prepare(p->networks[j].slug,w->name.original_wire_target,chantypes,&d->name) ||
            strcmp(d->name.canonical_target,w->name.canonical_target) ||
            strcmp(d->name.input_network_slug,w->name.input_network_slug) || d->name.kind != w->name.kind) {
            code = GIN_ACCOUNT_PLAN_INVALID; goto fail;
        }
        if (w->opened_at) {
            if (strlen(w->opened_at) > GIN_TOPOLOGY_OPENED_AT_MAX) { code = GIN_ACCOUNT_PLAN_INVALID; goto fail; }
            strcpy(p->stamps[p->windows_count],w->opened_at); d->opened_at = p->stamps[p->windows_count];
        }
        p->windows_count++;
        code = add(p,subject,p->networks[j].slug,d->name.original_wire_target); if (code) goto fail;
    }
    for (i = 0; i < nc; i++) if (*p->networks[i].own_nick) {
        code = add(p,subject,p->networks[i].slug,p->networks[i].own_nick); if (code) goto fail;
    }
    *out = p; return GIN_ACCOUNT_PLAN_OK;
fail:
    gin_account_plan_free(p); return code;
}
