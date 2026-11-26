#ifndef GIN_ACCOUNT_HISTORY_ONE_H
#define GIN_ACCOUNT_HISTORY_ONE_H
#include "account_output_hot.h"
#include "account_dispatch_hot.h"
#include "http_control.h"
/* Private synchronous one-new-QUERY component, not account READY or FIFO wiring.
 * All inputs are borrowed live and serialized through return; no restart or
 * inventory resolution may invalidate the adopted topology during this call.
 * The owner asserts new_window_first/count from the exact preceding registry
 * apply delta and previous window count. These are not authentication proofs.
 * No all-account bootstrap, retry, old history fetch or safe-anchor promotion.
 * stop tick may only check owner stop/reduce wait, not reenter this composition.
 */
struct gin_account_history_one_input {
 const struct gin_root *root;
 const struct gin_account_sinks *sinks;
 const struct gin_account_plan *plan;
 struct gin_account_receive *receiver;
 struct gin_account_dispatch *dispatcher;
 struct gin_account_output *output;
 const struct gin_account_registry *registry;
 const struct gin_account_registry_scope *scope;
 uint64_t generation, deadline_ms; /* absolute CLOCK_MONOTONIC, <=60s ahead */
 size_t new_window_first, new_window_count, window_index;
 const struct gin_auth_settings *http;
 const struct gin_protocol_policy *policy;
 const struct gin_credential *credential;
 enum gin_render_encryption encryption;
 struct gin_http_control *stop;
};
struct gin_account_history_one_result {
 enum gin_history_code code;
 size_t pages, appended, old_appended;
 int drained, incomplete, stopped, cleanup_failed, quarantined;
 int output_attempted;
 struct gin_account_output_result output; /* last attempted output, including ambiguity */
 struct gin_account_inventory *pending; /* emitted proposal: caller owns after unwind */
 struct gin_history_recovery *retained; /* success only: caller owns immutable attempt view */
};
/* Success requires code OK, drained, !incomplete and !stopped. retained must
 * remain alive until a separately specified new-scope handoff/dedup policy is
 * established; this call does not enable ordinary new-query live/FIFO routing.
 * On failure recovery allocations are freed, borrowed owners stay open and any
 * emitted pending proposal is returned without resolution/free. Caller keeps
 * it alive until dispatcher resolution or destruction makes release safe.
 * Old and selected processing anchors remain their independent entry snapshots.
 * Appended prefixes/published checkpoint ambiguity are never rolled back/replayed.
 */
struct gin_account_history_one_result gin_account_history_one(
 const struct gin_account_history_one_input *);
#endif
