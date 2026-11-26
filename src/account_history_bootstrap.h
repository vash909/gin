#ifndef GIN_ACCOUNT_HISTORY_BOOTSTRAP_H
#define GIN_ACCOUNT_HISTORY_BOOTSTRAP_H
#include "account_output.h"
#include "http_control.h"
/* Private synchronous initial-history tracer. All handles, snapshots, strings,
 * credentials and HTTP global state are BORROWED for the entire call. Caller
 * guarantees matching receiver/dispatcher/output/plan/topology relationship.
 * No constructor, close, restart, login, discovery, mutation or borrowed free.
 * Exact receiver identity/current epoch are checked, not newly authenticated.
 * Baseline progress is immutable, including zero/unknown; no ID becomes a CP.
 * One monotonic 60s episode; <=64 destinations, 16 pages each, round robin.
 * Conservative preallocation admission includes old + incoming + replacement;
 * LIMIT is incomplete, never eviction. 4096 rows/8MiB and 512 live identities
 * aggregate, not RSS: raw HTTP <=1MiB, DOM, Native 64/2MiB queue, bounded render,
 * fixed coordinator/recovery bookkeeping and recovery stack refs are separate.
 * Initial ID-less/lifecycle records fail closed as UNSUPPORTED; prior output
 * stays committed. Retained destination-scoped pages dedup later live overlap.
 * Output is per drained batch ascending ID, not a complete/atomic server cut.
 * Result drained counts REST-visible drained destinations, not account READY
 * or Native quiescence. Owner stop is checked before each append and success;
 * a stopped incomplete call retains its already committed output prefix.
 * Caller closes output before receiver, retains quarantined handles and HTTP
 * global state per receiver flags. No CLI/live/FIFO or durability claim.
 */
struct gin_account_history_bootstrap_input {
 struct gin_account_receive *receiver;
 struct gin_account_dispatch *dispatcher;
 struct gin_account_output *output;
 const struct gin_account_plan *plan;
 const struct gin_topology *topology;
 const struct gin_auth_settings *http;
 const struct gin_protocol_policy *policy;
 const struct gin_credential *credential;
 const char *chantypes;
 enum gin_render_encryption encryption;
 struct gin_http_control *stop; /* optional outside-callback owner tick */
};
struct gin_account_history_bootstrap_result {
 enum gin_history_code code;
 size_t destinations, drained, appended, pages;
 int incomplete, stopped, cleanup_failed, quarantined;
};
struct gin_account_history_bootstrap_result gin_account_history_bootstrap(
 const struct gin_account_history_bootstrap_input *);
#endif
