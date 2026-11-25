#ifndef GIN_ACCOUNT_OUTPUT_HOT_H
#define GIN_ACCOUNT_OUTPUT_HOT_H
#include "account_output.h"
#include "account_registry.h"
/* Private serialized-owner opt-in. Bind once at generation zero after sinks
 * bind; require exact cold root/plan/registry and current receiver identity and
 * epoch. Append only the next contiguous QUERY already admitted by sinks.
 * Existing valid indices are checked no-ops. OK means local entry availability,
 * not authentication, subscription, history completion, deduplication or READY.
 * Registry, root, sinks, immutable cold plan, receiver and chantypes are borrowed
 * through output close. Registry's fixed owned strings stay stable after apply;
 * caller proposals may be freed. Old entries/objects/FDs/scopes/anchors never
 * move or rebuild. New checkpoint baseline can be UNKNOWN. Safe processing
 * anchors must still be supplied independently through the typed history API.
 * INVALID covers null, duplicate bind, generation/order/capacity rejection;
 * SCOPE covers active/receiver identity mismatch; UNSAFE covers sink taint.
 * Admitted I/O failure stops further admission. Root checks precede checkpoint
 * I/O; no lease reopening. No rollback/replay. Nested checkpoint open/load close
 * errors remain partly discarded by unchanged helpers; complete fault accounting
 * is NOT promised. Existing partial append/published ambiguity is unchanged.
 * Close checkpoints/output before sinks/root/registry or other borrowed owners. */
struct gin_account_output_result gin_account_output_hot_bind(
 struct gin_account_output *, const struct gin_account_registry *,
 const struct gin_account_registry_scope *, uint64_t);
struct gin_account_output_result gin_account_output_hot_append(
 struct gin_account_output *, const struct gin_account_registry_scope *,
 uint64_t, size_t);
#endif
