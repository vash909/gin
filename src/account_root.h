#ifndef GIN_ACCOUNT_ROOT_H
#define GIN_ACCOUNT_ROOT_H
#include "gin_credential.h"
#include <stdbool.h>

#define GIN_ACCOUNT_ROOT_FILENAME ".gin.account"
#define GIN_ACCOUNT_ROOT_MAX 8192

enum gin_account_root_code {
    GIN_ACCOUNT_ROOT_OK, GIN_ACCOUNT_ROOT_INVALID, GIN_ACCOUNT_ROOT_UNSAFE,
    GIN_ACCOUNT_ROOT_IO, GIN_ACCOUNT_ROOT_MISMATCH,
    GIN_ACCOUNT_ROOT_UNBOUND_NONEMPTY
};
struct gin_account_root_result {
    enum gin_account_root_code code;
    bool created;
};
struct gin_account_root_check_result {
    enum gin_account_root_code code;
    bool cleanup_failed;
};
/* Existing-only exact identity/layout check through one borrowed live root.
 * Missing metadata is IO; corrupt/different bytes are MISMATCH. Never creates,
 * enumerates or repairs. Trusted ancestors/cooperating same UID required;
 * pathname races can open a lock alias. No authentication claim.
 * Initial fstat must prove the opened inode differs from the borrowed lock
 * before close is authorized. An alias returns UNSAFE; failed initial fstat
 * returns IO. Both retain the descriptor without close and set cleanup_failed.
 * This stateless ABI provides no recovery handle: explicit quarantine may retain
 * an FD until process exit; reclamation is not promised. Otherwise close is
 * attempted exactly once, its failure independently sets cleanup_failed, and
 * primary code is preserved. Never retry close or a cleanup-tainted check.
 * Owner must reject either failure, stop admission on cleanup taint, tear down
 * all borrowers before releasing the root, and not retry the operation. */
struct gin_account_root_check_result gin_account_root_check(
    const struct gin_root *root, const char *origin,
    const char *subject_uuid, const char *chantypes);
/* Private identity/layout binding, NOT authentication. Borrow exactly one live
 * process-owned root lease; never copy/reacquire it or open its lock aliases.
 * Inputs are native bounded credentials and NUL-terminated chantypes. No
 * normalization. Persist ONLY origin, subject_id and chantypes, never tokens
 * or display names. No repair/adoption/replacement; any different/corrupt bytes
 * are MISMATCH. created remains true after any exclusive creation, including
 * later failure; preserve partial metadata and stop, do not retry login.
 * Single-threaded umask scope; trusted ancestors/cooperating same UID required.
 * Neither atomic publication, crash durability nor authenticated contents.
 */
struct gin_account_root_result gin_account_root_bind(const struct gin_root *root,
    const struct gin_credential *credential, const char *chantypes);
#endif
