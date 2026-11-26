#ifndef GIN_ACCOUNT_DISPATCH_HOT_H
#define GIN_ACCOUNT_DISPATCH_HOT_H
#include "account_dispatch.h"
#include "account_registry.h"
/* Opt-in routing capture only. Receiver must have been constructed new_hot.
 * Borrow receiver, registry, cold plan/topology until free; no restart beneath
 * borrowers. Cold generation must be zero. No storage/history/input readiness.
 */
enum gin_session_code gin_account_dispatch_new_hot(struct gin_account_receive *,
 const struct gin_account_plan *,const struct gin_topology *,struct gin_account_registry *,
 const char *,enum gin_render_encryption,struct gin_account_dispatch **);
/* Only this handle's outstanding proposal is accepted. Caller keeps pending
 * proposal alive; success transfers ownership and clears *proposal. Failure
 * preserves proposal, registry, accepted topology and delta. Unsupported full
 * replacements require owner stop, not ignoring the proposal. */
enum gin_session_code gin_account_dispatch_resolve_inventory(struct gin_account_dispatch *,
 struct gin_account_inventory **,uint64_t,struct gin_account_registry_delta *);
enum gin_session_code gin_account_dispatch_admit_window(struct gin_account_dispatch *,
 const struct gin_account_registry_scope *,uint64_t,size_t);
struct gin_account_dispatch_hot_result {
 enum gin_session_code code;
 uint64_t generation;
 int proposal_pending;
 int window_admitted;
};
/* Pure errno-preserving getter. SIZE_MAX checks binding only. A valid binding
 * may have a pending proposal or an unadmitted window. */
struct gin_account_dispatch_hot_result gin_account_dispatch_hot_status(
 const struct gin_account_dispatch *,const struct gin_account_receive *,
 const struct gin_account_registry *,const struct gin_account_registry_scope *,uint64_t,size_t);
/* Borrow cold/adopted topology until next successful resolve or free. Pending
 * does not change this view. NULL for invalid/stale/failed or legacy binding. */
const struct gin_topology *gin_account_dispatch_hot_topology(const struct gin_account_dispatch *);
#endif
