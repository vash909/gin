/* Test-only real receive observation; production connect.c is included once. */
#include <curl/curl.h>
#include <curl/websockets.h>
#include <stdlib.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

static CURLcode observed_ws_recv(CURL *curl, void *buffer, size_t buflen,
    size_t *received, const struct curl_ws_frame **metadata)
{
    static int emitted;
    CURLcode result = curl_ws_recv(curl, buffer, buflen, received, metadata);
    if (!emitted && result == CURLE_OK && received && *received && metadata &&
        *metadata && ((*metadata)->flags & CURLWS_TEXT) &&
        ((*metadata)->flags & CURLWS_CONT)) {
        const char *value = getenv("GIN_TEST_RECEIVE_FD");
        char *end;
        long fd = value ? strtol(value, &end, 10) : -1;
        if (value && *value && !*end && fd >= 0 && fd <= INT_MAX) {
            int flags = fcntl((int)fd, F_GETFL);
            if (flags >= 0 && (flags & O_NONBLOCK)) {
                ssize_t written = write((int)fd, "T", 1);
                if (written == 1) emitted = 1;
            }
        }
    }
    return result;
}
#define curl_ws_recv observed_ws_recv
#include "../src/connect.c"
