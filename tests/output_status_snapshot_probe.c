/* Test-only private inspection. Compile four times under disjoint selectors;
 * replace, never additionally link, the corresponding production source.
 * Objects are copied with their real complete type, never inferred sizes.
 * No production storage receives definedness annotations. */
#if (defined(GIN_STATUS_PROBE_PLAN) + defined(GIN_STATUS_PROBE_REGISTRY) + defined(GIN_STATUS_PROBE_SINKS) + defined(GIN_STATUS_PROBE_CHECKPOINT)) != 1
#error Select exactly one inspection translation unit
#endif
#if defined(GIN_STATUS_PROBE_PLAN)
#include "account_plan.c"
#define OBJECT gin_account_plan
#define COPY gin_status_plan_copy
#elif defined(GIN_STATUS_PROBE_REGISTRY)
#include "account_registry.c"
#define OBJECT gin_account_registry
#define COPY gin_status_registry_copy
#elif defined(GIN_STATUS_PROBE_SINKS)
#include "account_sinks.c"
#define OBJECT gin_account_sinks
#define COPY gin_status_sinks_copy
/* Controlled metadata fault ONLY, unrelated to actual close/FD reclamation. */
int gin_status_sinks_cleanup(struct gin_account_sinks *s, int value)
{
    int old = s->hot_cleanup;
    s->hot_cleanup = value != 0;
    return old;
}
#elif defined(GIN_STATUS_PROBE_CHECKPOINT)
#include "checkpoint.c"
#define OBJECT gin_checkpoint
#define COPY gin_status_checkpoint_copy
#endif
size_t COPY(const struct OBJECT *object, void *destination, size_t capacity)
{
    if (!object) return 0;
    if (destination && capacity >= sizeof(*object))
        memcpy(destination, object, sizeof(*object));
    return sizeof(*object);
}
