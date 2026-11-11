#ifndef GIN_LIVENESS_H
#define GIN_LIVENESS_H
#include "gin_connect.h"

#define GIN_LIVENESS_INTERVAL_MAX_MS 60000u
#define GIN_LIVENESS_TIMEOUT_MAX_MS 60000u
#define GIN_LIVENESS_BACKOFF_MAX_MS 60000u
#define GIN_LIVENESS_RETRIES_MAX 16u
#define GIN_LIVENESS_HEARTBEAT_FRAME_MAX 128u

struct gin_liveness_settings {
    unsigned heartbeat_interval_ms; /* 1..INTERVAL_MAX; no traffic-based reset. */
    unsigned heartbeat_timeout_ms;  /* 1..TIMEOUT_MAX; due-to-reply budget. */
    unsigned retry_base_ms;         /* 1..retry_cap_ms. */
    unsigned retry_cap_ms;          /* <=BACKOFF_MAX. */
    unsigned max_retries;           /* 0..RETRIES_MAX, excludes initial start. */
};

enum gin_liveness_state {
    GIN_LIVENESS_DISABLED, GIN_LIVENESS_ACTIVE, GIN_LIVENESS_WAITING,
    GIN_LIVENESS_RECEIPT_BLOCKED, GIN_LIVENESS_DUE, GIN_LIVENESS_TERMINAL,
    GIN_LIVENESS_EXHAUSTED, GIN_LIVENESS_CANCELLED, GIN_LIVENESS_CLOCK_ERROR
};
enum gin_liveness_reason {
    GIN_LIVENESS_REASON_NONE, GIN_LIVENESS_REASON_IO,
    GIN_LIVENESS_REASON_PEER_CLOSE, GIN_LIVENESS_REASON_HEARTBEAT_TIMEOUT,
    GIN_LIVENESS_REASON_JOIN_TIMEOUT, GIN_LIVENESS_REASON_QUERY_TIMEOUT,
    GIN_LIVENESS_REASON_SESSION_SEVERED, GIN_LIVENESS_REASON_PREFLIGHT,
    GIN_LIVENESS_REASON_AUTH, GIN_LIVENESS_REASON_TLS,
    GIN_LIVENESS_REASON_PROTOCOL, GIN_LIVENESS_REASON_LOCAL,
    GIN_LIVENESS_REASON_EXPLICIT_CLOSE, GIN_LIVENESS_REASON_CLOCK
};
enum gin_heartbeat_state {
    GIN_HEARTBEAT_OFF, GIN_HEARTBEAT_SCHEDULED, GIN_HEARTBEAT_QUEUED,
    GIN_HEARTBEAT_WRITING, GIN_HEARTBEAT_WAITING_REPLY
};
struct gin_liveness_status {
    enum gin_liveness_state state;
    enum gin_liveness_reason reason;
    enum gin_heartbeat_state heartbeat;
    unsigned retries_used, retries_limit;
    unsigned retry_wait_ms; /* Ceil remaining monotonic delay; 0 if due/inactive.
                            * RECEIPT_BLOCKED may retain a nonzero delay. */
    uint64_t socket_epoch;  /* Local ownership only, never transmitted. */
};
enum gin_retry_step {
    GIN_RETRY_ATTEMPTED, GIN_RETRY_NOT_DUE, GIN_RETRY_BLOCKED,
    GIN_RETRY_EXHAUSTED, GIN_RETRY_TERMINAL, GIN_RETRY_CANCELLED,
    GIN_RETRY_DISABLED, GIN_RETRY_INVALID, GIN_RETRY_CLOCK_ERROR
};

/* Opt-in, no I/O; copy/validate all members atomically. Allowed exactly once,
 * only IDLE before any start, not in callback. Failure leaves session unchanged.
 * No production time/I/O hook, no default environment settings. Suggested owner
 * policy: {30000,10000,1000,30000,5}; values are local policy, not negotiation.
 * Unconfigured callers preserve existing explicit close/start behavior.
 * Session-severance recognition is a fail-closed correction for ALL sessions.
 */
enum gin_connect_code gin_connect_set_liveness(struct gin_connect *session,
    const struct gin_liveness_settings *settings);

/* Read-only value snapshot; callback-safe. NULL => DISABLED with zero fields.
 * Uses monotonic time only to calculate wait; clock failure returns CLOCK_ERROR
 * / REASON_CLOCK, never DUE. Does not close, retry, consume receipts or do HTTP.
 */
struct gin_liveness_status gin_connect_liveness_status(
    const struct gin_connect *session);

/* Explicit owner-driven synchronous retry, never from callback/signal handler.
 * Only FAILED + transient cause + elapsed backoff + remaining lifetime budget
 * + query slot NONE permit an attempt. Increment retries_used exactly once
 * immediately before invoking fresh config -> bound /me -> WSS -> root-first
 * joins. ATTEMPTED means start invoked, NOT connected/READY; inspect unchanged
 * gin_connect_last_result/get_state for its real result. No-op outcomes preserve
 * last_result, counters, refs and receipts (clock failure is terminal instead).
 * Retrying never resubmits a query, SEND, JOIN or PART mutation.
 */
enum gin_retry_step gin_connect_retry(struct gin_connect *session);

/* Configured-mode contract additions to existing functions:
 * - service remains bounded WSS/poll ONLY; it schedules but never executes HTTP.
 * - First start is ordinary; subsequent configured starts use the same retry
 *   gate as retry(), not an alternate budget/reset path. A rejected start returns
 *   existing INVALID without replacing the prior failure result.
 * - Successful starts/READY/heartbeat replies never reset retries_used or backoff
 *   index. There is no automatic reset or public reset operation. New explicit
 *   session construction is a new retry scope; owner must not loop construction.
 * - Explicit close cancels this configured scope permanently; free/new required
 *   to begin another. It preserves any existing query receipt.
 * - Security/protocol failure is latched; no retry/start clears it in configured
 *   mode. Recognized SESSION_EXPIRED cannot restart even when unconfigured;
 *   new credentials require a deliberately constructed new session.
 * - Heartbeat uses a bounded private frame, not the query outbound/receipt slot;
 *   one current wire-frame writer, no interleaving partial TEXT frame suffixes.
 * - Query submit returns BUSY while a heartbeat is due/queued/writing, without
 *   ref allocation; a fully written pending query and an unconsumed terminal
 *   receipt do not inhibit heartbeat transmission or reply correlation.
 */
#endif
