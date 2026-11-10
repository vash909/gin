#ifndef GIN_CREDENTIAL_H
#define GIN_CREDENTIAL_H

#include "gin_fs.h"

#define GIN_CREDENTIAL_ORIGIN_MAX 1024
#define GIN_CREDENTIAL_TOKEN_MAX 4096
#define GIN_CREDENTIAL_NAME_MAX 255
#define GIN_CREDENTIAL_FILE_MAX 16384
#define GIN_CREDENTIAL_FILENAME ".gin.credentials"

struct gin_credential {
    char origin[GIN_CREDENTIAL_ORIGIN_MAX + 1];
    char token[GIN_CREDENTIAL_TOKEN_MAX + 1];
    char subject_id[37];
    char subject_name[GIN_CREDENTIAL_NAME_MAX + 1];
};

enum gin_credential_code {
    GIN_CREDENTIAL_OK, GIN_CREDENTIAL_INVALID, GIN_CREDENTIAL_UNSAFE,
    GIN_CREDENTIAL_IO, GIN_CREDENTIAL_NOT_FOUND
};

/* Account-user credentials only. All arrays must be bounded NUL-terminated;
 * origin is HTTPS authority only, token is a safe bearer and id is a UUID.
 * Authentication additionally validates the origin with libcurl's parser. */
enum gin_credential_code gin_credential_validate(const struct gin_credential *value);
void gin_credential_clear(struct gin_credential *value);

/* Caller retains one live gin_root lock throughout; never reacquire or close
 * another descriptor for its lock inode. No caller-owned fd is consumed.
 * Plaintext private storage, not encryption or secure disk erasure.
 * Load clears output on error and binds it to the exact expected origin.
 * Forget removes local storage only: it never claims remote revocation. */
enum gin_credential_code gin_credential_load(const struct gin_root *root,
    const char *expected_origin, struct gin_credential *value);
enum gin_credential_code gin_credential_save(const struct gin_root *root,
    const struct gin_credential *value);
enum gin_credential_code gin_credential_forget(const struct gin_root *root);
const char *gin_credential_error(enum gin_credential_code code);

#endif
