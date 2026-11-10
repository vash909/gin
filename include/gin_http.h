#ifndef GIN_HTTP_H
#define GIN_HTTP_H

#include <stddef.h>

#define GIN_HTTP_BODY_MAX ((size_t)1048576)
#define GIN_HTTP_HEADER_MAX ((size_t)65536)
#define GIN_HTTP_TIMEOUT_MAX 60000L

enum gin_http_code {
    GIN_HTTP_OK, GIN_HTTP_STATUS, GIN_HTTP_TRANSPORT, GIN_HTTP_TLS,
    GIN_HTTP_TIMEOUT, GIN_HTTP_LIMIT, GIN_HTTP_INVALID
};

struct gin_http_settings {
    const char *url;
    const char *ca_file; /* NULL uses libcurl's compiled trust store. */
    long connect_ms;
    long total_ms;
    size_t header_limit;
    int allow_loopback_http; /* Explicit local fixtures/policy, never remote HTTP. */
};

struct gin_http_result {
    enum gin_http_code code;
    long http_status;
    size_t body_length;
    size_t header_length;
};

/* Owner calls init once before requests/threads, cleanup after all users stop.
 * Never pair this cleanup with another library's global initialization.
 * See docs/HTTP.md for lifecycle, resolver and nonblocking-runtime restrictions. */
enum gin_http_code gin_http_global_init(void);
void gin_http_global_cleanup(void);
/* Synchronous bounded GET; caller owns settings and body for the whole call.
 * Completed non-2xx responses return STATUS, not OK. Failed/incomplete bodies
 * have zero length and their written bytes are cleared. No NUL terminator. */
struct gin_http_result gin_http_get(const struct gin_http_settings *settings,
    unsigned char *body, size_t capacity);
/* Static diagnostics only; never includes peer or caller-controlled bytes. */
const char *gin_http_error(enum gin_http_code code);

#endif
