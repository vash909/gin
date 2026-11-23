#ifndef GIN_ACCOUNT_DISPATCH_H
#define GIN_ACCOUNT_DISPATCH_H
#include "account_receive.h"
#include "session_live_projection.h"
struct gin_account_dispatch;
struct gin_account_inventory;
/* Owned validated full metadata proposal, NOT authentication, readiness or
 * route/SEND/filesystem authority. No borrowed Native or credential secrets.
 * Caller must bind identity/epoch and resolve the proposal before continued use
 * of old routing. Equivalent proposals are allowed; dedup belongs to caller.
 * Getters borrow owned bytes until free; NULL getters return NULL/0. */
const struct gin_topology *gin_account_inventory_topology(const struct gin_account_inventory *);
const struct gin_account_identity *gin_account_inventory_identity(const struct gin_account_inventory *);
uint64_t gin_account_inventory_epoch(const struct gin_account_inventory *);
void gin_account_inventory_free(struct gin_account_inventory *);
struct gin_account_destination {
    size_t network_index, window_index; /* SIZE_MAX window => network STATUS. */
    enum gin_render_role role;
    int terminal; /* Final owned lifecycle record; subsequent calls stop. */
};
/* Private single-owner composition. Borrow receiver, immutable plan and topology
 * until free; no I/O, commands, retry, discovery or output fanout. Identity comes
 * only from this receiver's factory; cold metadata is not authentication.
 * PLAIN must be explicit. Every next consumes at most one real queued frame.
 * Fresh NULL owned outputs and destination publish only on complete success.
 * Metadata consumes without output; routing/projection failures are sticky.
 * No cross-destination dedup or history aggregation is implemented here. */
enum gin_session_code gin_account_dispatch_new(struct gin_account_receive *,
    const struct gin_account_plan *, const struct gin_topology *, const char *,
    enum gin_render_encryption, struct gin_account_dispatch **);
enum gin_session_code gin_account_dispatch_next(struct gin_account_dispatch *,
    struct gin_account_destination *, struct gin_render_record **,
    struct gin_history_page **);
void gin_account_dispatch_free(struct gin_account_dispatch *);
/* Explicit opt-in; fresh_out and *fresh_out==NULL required. Invalid caller
 * arguments are nonsticky and consume nothing. Metadata publishes only after
 * successful queue drop; original borrowed topology/plan are never replaced. */
enum gin_session_code gin_account_dispatch_next_inventory(struct gin_account_dispatch *,
    struct gin_account_destination *, struct gin_render_record **,
    struct gin_history_page **, struct gin_account_inventory **fresh_out);
#endif
