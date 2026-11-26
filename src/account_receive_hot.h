#ifndef GIN_ACCOUNT_RECEIVE_HOT_H
#define GIN_ACCOUNT_RECEIVE_HOT_H
#include "account_receive.h"
/* Explicit opt-in, single-owner membership only; no routing, plaintext,
 * history, filesystem or readiness authority. Legacy objects cannot upgrade.
 * Fresh NULL output required; inputs and outputs must be disjoint. Constructor
 * performs no I/O and owns all membership strings and validated CHANTYPES.
 * Native alone retains the bearer and binds origin/UUID through fresh /me.
 * Queue capacity is 64 sources, still 64 rows/2 MiB; spare capacity is not
 * admission. After any admitted start attempt, successful or failed,
 * subject_name is per-attempt mutable metadata reconstructed from Native's
 * actual root only after validated, complete owned membership; origin/UUID
 * never change. Identity/view pointers retain their existing borrowed
 * lifetime, but callers must not cache subject_name bytes across any attempt.
 * Legacy rename policy unchanged.
 * Submit uses copied CHANTYPES and actual owned Native, outside callbacks.
 * Expected nonzero epoch, READY and joined root/network are Native preconditions.
 * Duplicate joined sources reuse their index even at capacity. New membership
 * appends PENDING without moving old sources or queued views. Drain retained
 * work before further service. No allocation or callback after Native accepts
 * and before table publication. Failure leaves caller output unchanged; an
 * unexpected Native success latches SCOPE, never pretends to roll back Native.
 * Sticky admission failure rejects submit, but independent query result/take
 * remain usable after failure/close. Close/start discard queued generations,
 * retaining registered membership. Cleanup/quarantine lifetimes are unchanged.
 * Private probes are not real HTTPS/WSS or authentication qualification.
 */
enum gin_connect_code gin_account_receive_new_hot(
 const struct gin_connect_settings *, const struct gin_credential *,
 const struct gin_account_plan *, const char *chantypes,
 struct gin_account_receive **);
enum gin_connect_code gin_account_receive_subscribe_query_live(
 struct gin_account_receive *, const char *exact_network_slug,
 const char *original_peer, uint64_t expected_epoch, size_t *source_out);
#endif
