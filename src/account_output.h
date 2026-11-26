#ifndef GIN_ACCOUNT_OUTPUT_H
#define GIN_ACCOUNT_OUTPUT_H
#include "account_dispatch.h"
#include "account_sinks.h"
#include "checkpoint.h"
struct gin_account_output;
struct gin_account_output_result {
 enum gin_checkpoint_code code;
 size_t appended;
 struct gin_checkpoint_write_result checkpoint;
 int cleanup_failed;
};
/* Private storage ONLY. No authentication, deduplication, history scheduling,
 * replay, recovery or readiness. Trusted single owner supplies the matching
 * owned receiver/immutable plan and sinks constructed under THIS held root.
 * Borrow all inputs (including chantypes) for the whole handle lifetime; no
 * reentry, callback use, concurrent mutation or receiver restart. Caller owns
 * HTTP global lifetime. PLAIN explicit; current receiver epoch required.
 * Fresh NULL output. Failure preserves output, but never repairs existing data.
 * Close private checkpoints BEFORE sinks, root, plan or receiver are destroyed.
 * A typed destination and record must be the SAME trusted dispatcher admission;
 * opaque presentation bytes cannot authenticate their destination. No arbitrary
 * paths or descriptors accepted. Metadata must never be passed as a record.
 * safe_anchor is explicit caller-established processed prefix, NOT a record ID,
 * fetch/read cursor or live maximum; use zero when prefix is unknown. This API
 * cannot authenticate that assertion. Two live admissions with equal IDs both
 * append. Completed append precedes commit, without atomicity/durability promise.
 * Any storage failure stops that sink; never rewind/truncate/replay a prefix.
 * published on commit failure means state may already have changed. */
struct gin_account_output_result gin_account_output_new(const struct gin_root *,
 const struct gin_account_sinks *, const struct gin_account_plan *,
 const struct gin_account_receive *, const char *, enum gin_render_encryption,
 struct gin_account_output **);
struct gin_account_output_result gin_account_output_append(struct gin_account_output *,
 const struct gin_account_destination *, const struct gin_render_record *, int64_t);
int gin_account_output_progress(const struct gin_account_output *,
 const struct gin_account_destination *, struct gin_checkpoint_progress *);
int gin_account_output_close(struct gin_account_output **);

/* Private opt-in history path, not the live dispatcher admission API above.
 * Borrow a caller-owned decoded REST page or recovery's borrowed MERGED view
 * for this call; render internally and free the temporary record. No opaque
 * presentation record, arbitrary pathname or descriptor input. A decoded page
 * is not authentication: the trusted owner still binds fetch and credential.
 * Require exact origin, UUID, network slug/ID, original target, chantypes,
 * own nick, current epoch, role and explicit PLAIN policy agreement.
 * NULL/range/negative anchor => HISTORY_INVALID; destination, terminal,
 * epoch, scope or non-REST/MERGED source mismatch => HISTORY_ROUTE.
 * Render errors map to history codes; IGNORE performs no storage operation.
 * HISTORY_OK means a rendered record and a storage attempt, NOT successful
 * output: inspect output.code and checkpoint.published independently.
 * Other history outcomes leave output at its default INVALID/zero result.
 * Anchor remains an explicit caller-established processing prefix, never
 * inferred from a row ID, fetch cursor or maximum live ID. The same private
 * storage core preserves sticky failure, partial-prefix accounting, held-root
 * borrowing and checkpoint publication ambiguity; no retry or rollback. */
struct gin_account_history_output_result {
 enum gin_history_code history;
 struct gin_account_output_result output;
};
struct gin_account_history_output_result gin_account_output_append_history(struct gin_account_output *h,const struct gin_account_destination *d,const struct gin_history_page *page,size_t index,int64_t anchor);
#endif
