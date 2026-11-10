#ifndef GIN_AUTH_H
#define GIN_AUTH_H
#include "gin_http.h"
#include "gin_credential.h"
#define GIN_AUTH_RESPONSE_MAX 65536
#define GIN_AUTH_IDENTIFIER_MAX 255
#define GIN_AUTH_PASSWORD_MAX 4096
#define GIN_AUTH_DEPTH_MAX 32
struct gin_auth_settings {
    const char *origin;
    const char *ca_file;
    long connect_ms;
    long total_ms;
};
enum gin_auth_code {
    GIN_AUTH_OK, GIN_AUTH_INVALID, GIN_AUTH_UNAUTHORIZED, GIN_AUTH_SCOPE,
    GIN_AUTH_FORBIDDEN, GIN_AUTH_THROTTLED, GIN_AUTH_INTERACTIVE,
    GIN_AUTH_SUBJECT, GIN_AUTH_MALFORMED, GIN_AUTH_REMOTE, GIN_AUTH_TRANSPORT,
    GIN_AUTH_TLS, GIN_AUTH_TIMEOUT, GIN_AUTH_LIMIT
};
/* Synchronous, outside runtime callbacks. Caller must perform discovery policy
 * before credentials outside fixtures. No implicit persistence or retries.
 * Outputs clear on every failure; input and output must not alias.
 * Logout never changes caller credentials/local storage, even on success. */
enum gin_auth_code gin_auth_login(const struct gin_auth_settings *settings,
    const char *identifier, const char *password, struct gin_credential *out);
enum gin_auth_code gin_auth_resume(const struct gin_auth_settings *settings,
    const char *bearer, struct gin_credential *out);
enum gin_auth_code gin_auth_identity(const struct gin_auth_settings *settings,
    const struct gin_credential *credential, struct gin_credential *out);
enum gin_auth_code gin_auth_logout(const struct gin_auth_settings *settings,
    const struct gin_credential *credential);
const char *gin_auth_error(enum gin_auth_code code);
#endif
