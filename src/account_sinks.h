#ifndef GIN_ACCOUNT_SINKS_H
#define GIN_ACCOUNT_SINKS_H
#include "account_plan.h"
#include "account_root.h"
/* Resources only: neither remote authentication nor plaintext send authority.
 * Borrow root and immutable plan until close; credentials borrowed only during
 * construction. Never release/reopen the root lease. No rollback: partial paths
 * remain on failure. A created binding on failure requires owner stop policy.
 * No command acceptance/READY until future owner completes all other gates.
 * Fresh *out == NULL required. Returned descriptors are borrowed, not owned by
 * callers. Close before releasing root/freeing plan. Close failure taints owner;
 * consumption of the handle does not promise descriptor recovery. */
struct gin_account_sinks;
enum gin_account_sinks_code { GIN_ACCOUNT_SINKS_OK, GIN_ACCOUNT_SINKS_INVALID,
 GIN_ACCOUNT_SINKS_NOMEM, GIN_ACCOUNT_SINKS_BINDING, GIN_ACCOUNT_SINKS_IO };
struct gin_account_sinks_result {
 enum gin_account_sinks_code code;
 bool binding_attempted;
 struct gin_account_root_result binding;
 bool cleanup_failed;
};
struct gin_account_sinks_result gin_account_sinks_new(const struct gin_root *,
 const struct gin_credential *, const struct gin_account_plan *, const char *,
 struct gin_account_sinks **);
int gin_account_sinks_close(struct gin_account_sinks *);
size_t gin_account_sinks_network_count(const struct gin_account_sinks *);
size_t gin_account_sinks_window_count(const struct gin_account_sinks *);
int gin_account_sinks_network_input(const struct gin_account_sinks *, size_t);
int gin_account_sinks_network_output(const struct gin_account_sinks *, size_t);
int gin_account_sinks_window_input(const struct gin_account_sinks *, size_t);
int gin_account_sinks_window_output(const struct gin_account_sinks *, size_t);
#endif
