#ifndef GIN_ACCOUNT_SINKS_HOT_H
#define GIN_ACCOUNT_SINKS_HOT_H
#include "account_sinks.h"
#include "account_registry.h"
/* Private opt-in resources only. One serialized owner; no callbacks/mutation
 * during calls. Bind before registry apply. Root, original immutable plan and
 * stable-address registry remain borrowed through close. Scope is copied, no
 * credential/bearer retained. Active scope is owner asserted, not socket proof.
 * Existing descriptors/indices never move. Close output/checkpoint borrowers
 * first. Failed admitted I/O stops admission permanently, without rollback,
 * unlink, retry or permission repair. Cleanup taint may mean FD quarantine until
 * process exit, not reclaimed resources. No READY/history/output authority.
 * Root result normalization: MISMATCH => SCOPE/EPERM, INVALID/UNSAFE => IO/EPERM,
 * IO or cleanup-only => IO/EIO. Checked FS preserves its primary errno. */
enum gin_account_sinks_hot_code { GIN_ACCOUNT_SINKS_HOT_OK,
 GIN_ACCOUNT_SINKS_HOT_INVALID, GIN_ACCOUNT_SINKS_HOT_SCOPE,
 GIN_ACCOUNT_SINKS_HOT_STALE, GIN_ACCOUNT_SINKS_HOT_LIMIT,
 GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED, GIN_ACCOUNT_SINKS_HOT_IO,
 GIN_ACCOUNT_SINKS_HOT_FAILED };
struct gin_account_sinks_hot_result {
 enum gin_account_sinks_hot_code code;
 bool added; bool paths_attempted; bool cleanup_failed; int error;
 size_t window_count; uint64_t generation;
};
/* Pure metadata status, not authentication or proof of live descriptors/lease.
 * Inputs are borrowed for this call only; exact bound root/plan/registry pointer
 * identities are required. Active scope must match stored and registry scopes.
 * Expected generation must equal both last sink admission and current registry.
 * Null/unbound is INVALID; identity/scope mismatch is SCOPE; generation mismatch
 * is STALE. Existing sticky FAILED/error/cleanup state retains hot_result
 * precedence even for an otherwise invalid request. No I/O, allocation, clock,
 * close, mutation or bind/append delegation. Preserves errno and object bytes;
 * added and paths_attempted are always false. Zero/cold generation is valid. */
struct gin_account_sinks_hot_result gin_account_sinks_hot_status(
 const struct gin_account_sinks *sinks, const struct gin_root *root,
 const struct gin_account_plan *plan, const struct gin_account_registry *registry,
 const struct gin_account_registry_scope *active_scope, uint64_t expected_generation);
/* Bind requires registry generation zero and the exact cold plan layout.
 * A repeated bind returns INVALID without I/O, including an identical repeat;
 * a no-addition registry apply retains generation zero and remains bindable.
 * Append before bind is INVALID; append of an existing bound index is a no-op. */
struct gin_account_sinks_hot_result gin_account_sinks_hot_bind(
 struct gin_account_sinks *sinks, const struct gin_account_registry *registry,
 const struct gin_account_registry_scope *active_scope, uint64_t expected_generation);
struct gin_account_sinks_hot_result gin_account_sinks_hot_append(
 struct gin_account_sinks *sinks, const struct gin_account_registry_scope *active_scope,
 uint64_t expected_generation, size_t window_index);
#endif
