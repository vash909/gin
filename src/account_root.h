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
