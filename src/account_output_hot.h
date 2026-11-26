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
/* Pure metadata attestation for the exact borrowed output/root/sinks/cold-plan/
 * receiver/registry binding. Calls are serialized; handles and C strings remain
 * live. SIZE_MAX checks only the binding; other selectors are stable registry
 * window indices. OK plus window_admitted reports an existing output entry,
 * not merely a registry/sink slot. UNKNOWN checkpoints may be admitted.
 * Counts exclude network STATUS entries. generation is the bound registry's
 * current value (zero when unbound). Stored output/entry/sink failure or cleanup
 * taint takes FAILED precedence; non-OK never reports window_admitted.
 * No I/O, allocation, clock, close, checkpoint action, mutation or mutating API
 * delegation. Preserves errno and borrowed object bytes. This is not fresh
 * authentication, FD/lease integrity, durable safe-prefix proof, subscription,
 * deduplication, history completion or READY. Existing append guards still apply.
 */
enum gin_account_output_hot_code {
 GIN_ACCOUNT_OUTPUT_HOT_OK, GIN_ACCOUNT_OUTPUT_HOT_INVALID,
 GIN_ACCOUNT_OUTPUT_HOT_SCOPE, GIN_ACCOUNT_OUTPUT_HOT_STALE,
 GIN_ACCOUNT_OUTPUT_HOT_FAILED
};
struct gin_account_output_hot_result {
 enum gin_account_output_hot_code code;
 uint64_t generation;
 size_t window_count;
 int window_admitted;
 int cleanup_failed;
};
struct gin_account_output_hot_result gin_account_output_hot_status(
 const struct gin_account_output *, const struct gin_root *,
 const struct gin_account_sinks *, const struct gin_account_plan *,
 const struct gin_account_receive *, const struct gin_account_registry *,
 const struct gin_account_registry_scope *, uint64_t, size_t);
#endif
