#include "gin_http.h"

#include <stdint.h>
#include <string.h>
#include <curl/curl.h>

#if LIBCURL_VERSION_NUM < 0x075500
#error libcurl 7.85.0 or newer is required
#endif

struct transfer {
    unsigned char *body;
    size_t capacity;
    size_t length;
    size_t headers;
    size_t header_limit;
    int limited;
};

static size_t receive_body(char *data, size_t size, size_t count, void *opaque)
{
    struct transfer *t = opaque;
    size_t n;
    if (size && count > SIZE_MAX / size) {
        t->limited = 1;
        return 0;
    }
    n = size * count;
    if (n > t->capacity - t->length) {
        t->limited = 1;
        return 0;
    }
    memcpy(t->body + t->length, data, n);
    t->length += n;
    return n;
}

static size_t receive_header(char *data, size_t size, size_t count, void *opaque)
{
    struct transfer *t = opaque;
    size_t n;
    (void)data;
    if (size && count > SIZE_MAX / size) {
        t->limited = 1;
        return 0;
    }
    n = size * count;
    if (n > t->header_limit - t->headers) {
        t->limited = 1;
        return 0;
    }
    t->headers += n;
    return n;
}

enum gin_http_code gin_http_global_init(void)
{
    const curl_version_info_data *version;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return GIN_HTTP_TRANSPORT;
    version = curl_version_info(CURLVERSION_NOW);
    /* NOSIGNAL cannot bound a synchronous DNS resolver. Fail closed. */
    if (!version || version->version_num < 0x075500 ||
        !(version->features & CURL_VERSION_SSL) ||
        !(version->features & CURL_VERSION_ASYNCHDNS)) {
        curl_global_cleanup();
        return GIN_HTTP_TRANSPORT;
    }
    return GIN_HTTP_OK;
}

void gin_http_global_cleanup(void)
{
    curl_global_cleanup();
}

const char *gin_http_error(enum gin_http_code code)
{
    switch (code) {
    case GIN_HTTP_OK: return "ok";
    case GIN_HTTP_STATUS: return "http-status";
    case GIN_HTTP_TLS: return "tls";
    case GIN_HTTP_TIMEOUT: return "timeout";
    case GIN_HTTP_LIMIT: return "limit";
    case GIN_HTTP_INVALID: return "invalid-settings";
    default: return "transport";
    }
}

/* Check raw authority before libcurl can normalize alternate numeric hosts. */
static int valid_url(const char *url, int local)
{
    const char *authority, *end, *p;
    size_t length, host_length;
    int plain;
    CURLU *parsed;
    CURLUcode code;
    if (strncmp(url, "https://", 8) == 0) {
        authority = url + 8;
        plain = 0;
    } else if (strncmp(url, "http://", 7) == 0 && local) {
        authority = url + 7;
        plain = 1;
    } else {
        return 0;
    }
    for (p = url; *p; p++) {
        if ((size_t)(p - url) >= 4096 || (unsigned char)*p <= 32 ||
            (unsigned char)*p >= 127 || *p == '#' || *p == '\\')
            return 0;
    }
    end = authority + strcspn(authority, "/?");
    length = (size_t)(end - authority);
    if (!length || memchr(authority, '@', length))
        return 0;
    if (plain) {
        if (length >= 9 && !memcmp(authority, "127.0.0.1", 9))
            host_length = 9;
        else if (length >= 5 && !memcmp(authority, "[::1]", 5))
            host_length = 5;
        else
            return 0;
        p = authority + host_length;
        if (p != end) {
            unsigned long port = 0;
            if (*p++ != ':' || p == end)
                return 0;
            for (; p != end; p++) {
                if (*p < '0' || *p > '9' || port > 6553)
                    return 0;
                port = port * 10 + (unsigned long)(*p - '0');
                if (port > 65535)
                    return 0;
            }
            if (!port)
                return 0;
        }
    }
    parsed = curl_url();
    if (!parsed)
        return 0;
    code = curl_url_set(parsed, CURLUPART_URL, url, CURLU_DISALLOW_USER);
    curl_url_cleanup(parsed);
    return code == CURLUE_OK;
}

struct gin_http_result gin_http_get(const struct gin_http_settings *s,
    unsigned char *body, size_t capacity)
{
    struct gin_http_result r = { GIN_HTTP_TRANSPORT, 0, 0, 0 };
    struct transfer t;
    CURL *easy;
    CURLcode code;
    if (!s || !s->url || !body || !capacity || capacity > GIN_HTTP_BODY_MAX ||
        s->connect_ms < 1 || s->total_ms < s->connect_ms ||
        s->total_ms > GIN_HTTP_TIMEOUT_MAX || !s->header_limit ||
        s->header_limit > GIN_HTTP_HEADER_MAX ||
        (s->allow_loopback_http != 0 && s->allow_loopback_http != 1) ||
        !valid_url(s->url, s->allow_loopback_http)) {
        r.code = GIN_HTTP_INVALID;
        return r;
    }
    memset(&t, 0, sizeof(t));
    t.body = body;
    t.capacity = capacity;
    t.header_limit = s->header_limit;
    easy = curl_easy_init();
    if (!easy)
        return r;
#define SET(option, value) do { \
    if (curl_easy_setopt(easy, option, value) != CURLE_OK) goto done; \
} while (0)
    SET(CURLOPT_URL, s->url);
    SET(CURLOPT_HTTPGET, 1L);
    SET(CURLOPT_WRITEFUNCTION, receive_body);
    SET(CURLOPT_WRITEDATA, &t);
    SET(CURLOPT_HEADERFUNCTION, receive_header);
    SET(CURLOPT_HEADERDATA, &t);
    SET(CURLOPT_CONNECTTIMEOUT_MS, s->connect_ms);
    SET(CURLOPT_TIMEOUT_MS, s->total_ms);
    SET(CURLOPT_PROXY, "");
    SET(CURLOPT_NETRC, (long)CURL_NETRC_IGNORED);
    SET(CURLOPT_HTTPAUTH, (long)CURLAUTH_NONE);
    SET(CURLOPT_PROXYAUTH, (long)CURLAUTH_NONE);
    SET(CURLOPT_FOLLOWLOCATION, 0L);
    SET(CURLOPT_UNRESTRICTED_AUTH, 0L);
    SET(CURLOPT_MAXREDIRS, 0L);
    SET(CURLOPT_PROTOCOLS_STR, s->allow_loopback_http ? "http,https" : "https");
    SET(CURLOPT_REDIR_PROTOCOLS_STR, "https");
    SET(CURLOPT_SSL_VERIFYPEER, 1L);
    SET(CURLOPT_SSL_VERIFYHOST, 2L);
    SET(CURLOPT_SSLVERSION, (long)CURL_SSLVERSION_TLSv1_2);
    if (s->ca_file)
        SET(CURLOPT_CAINFO, s->ca_file);
    SET(CURLOPT_HTTP_CONTENT_DECODING, 0L);
    SET(CURLOPT_NOSIGNAL, 1L);
    code = curl_easy_perform(easy);
    r.header_length = t.headers;
    (void)curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &r.http_status);
    if (t.limited) {
        r.code = GIN_HTTP_LIMIT;
    } else if (code == CURLE_OPERATION_TIMEDOUT) {
        r.code = GIN_HTTP_TIMEOUT;
    } else if (code == CURLE_SSL_CONNECT_ERROR || code == CURLE_PEER_FAILED_VERIFICATION ||
        code == CURLE_SSL_CERTPROBLEM || code == CURLE_SSL_CIPHER ||
        code == CURLE_SSL_CACERT_BADFILE || code == CURLE_SSL_ISSUER_ERROR) {
        r.code = GIN_HTTP_TLS;
    } else if (code == CURLE_OK && r.http_status >= 200 && r.http_status <= 599) {
        r.code = r.http_status < 300 ? GIN_HTTP_OK : GIN_HTTP_STATUS;
        r.body_length = t.length;
    }
done:
    if (r.code != GIN_HTTP_OK && r.code != GIN_HTTP_STATUS)
        memset(body, 0, t.length);
    curl_easy_cleanup(easy);
#undef SET
    return r;
}
