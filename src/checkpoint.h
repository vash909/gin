#ifndef GIN_CHECKPOINT_H
#define GIN_CHECKPOINT_H
#include "gin_fs.h"
#include "fs_private.h"
#include "gin_history.h"
#define GIN_CHECKPOINT_FILENAME ".gin.checkpoint"
#define GIN_CHECKPOINT_BYTES_MAX 8192u
enum gin_checkpoint_code {
    GIN_CHECKPOINT_OK, GIN_CHECKPOINT_UNKNOWN, GIN_CHECKPOINT_INVALID,
    GIN_CHECKPOINT_UNSAFE, GIN_CHECKPOINT_MALFORMED, GIN_CHECKPOINT_SCOPE,
    GIN_CHECKPOINT_AMBIGUOUS, GIN_CHECKPOINT_NOMEM, GIN_CHECKPOINT_IO
};
struct gin_checkpoint_progress { int64_t anchor; int unknown_before; };
struct gin_checkpoint;
struct gin_checkpoint_write_result {
    enum gin_checkpoint_code code;
    int published; /* Rename succeeded, even if a later directory fsync failed. */
    int cleanup_failed;
};
/* Private single-owner storage, not a history planner or authenticated server
 * generation. Borrow root/output until close. Output must be the checked append
 * descriptor for this existing window. Fresh NULL handle required. OK/UNKNOWN
 * publish an owned handle and progress; other outcomes leave both unchanged.
 * Missing metadata alone is UNKNOWN. Unsafe/malformed/different scope is not
 * silently repaired. A log inode/size mismatch is AMBIGUOUS, not clean progress.
 * Stable binding excludes socket epoch/own nick; canonical destination follows
 * naming policy. No bearer, message body, read cursor or REST fetch cursor.
 * Local v1 storage is bounded canonical binary, not a recursive JSON decoder. */
enum gin_checkpoint_code gin_checkpoint_open(const struct gin_root *,
    const struct gin_history_scope *, int output,
    struct gin_checkpoint **, struct gin_checkpoint_progress *);
/* Explicit private codec opt-in. Only compiled trusted pure lexical callbacks;
 * NULL is invalid. Preparation and existing-window traversal share this codec.
 * Same borrowed lifetimes, guards and binary v1 identity as the legacy API.
 * This is storage only, not account/root authentication or adoption. */
enum gin_checkpoint_code gin_checkpoint_open_named(const struct gin_root *,
    const struct gin_history_scope *, int output,
    struct gin_checkpoint **, struct gin_checkpoint_progress *, gin_fs_name_prepare_fn);
/* Explicit network storage for STATUS with exact target "$server" only.
 * Existing network directory and its borrowed append out; never window fallback.
 * Same binary v1 key, validation, ownership and publication rules as above.
 * This storage primitive does not establish account or credential policy. */
enum gin_checkpoint_code gin_checkpoint_open_network(const struct gin_root *,
    const struct gin_history_scope *, int output,
    struct gin_checkpoint **, struct gin_checkpoint_progress *);
/* Call ONLY after complete successful output. safe_anchor is an independently
 * established processed prefix, never simply max buffered-live/fetch ID. Zero
 * retains an explicitly unknown prefix. Requires increased checked output size;
 * cannot verify that caller completed a renderer append. Never replays output.
 * Storage/consistency failures latch the handle; invalid calls make no progress.
 * After published failure do not infer unchanged
 * state or roll back. Matching inode/size is consistency, not content integrity,
 * server continuity, durable output or append/checkpoint atomicity. */
struct gin_checkpoint_write_result gin_checkpoint_commit(struct gin_checkpoint *, int64_t safe_anchor);
/* Consumes and clears the handle even on close failure. Never closes root,
 * lock or borrowed output. No retry of interrupted close. */
int gin_checkpoint_close(struct gin_checkpoint **);
#endif
