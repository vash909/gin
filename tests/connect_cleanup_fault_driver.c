/* Private Native TU: one removal fails before or after detaching a live WS handle. */
#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
static int injected_remove;
static CURLMcode fixture_remove(CURLM *multi, CURL *easy)
{
    const char *mode=getenv("GIN_CONNECT_FAULT");
    if (!injected_remove && mode &&
        (!strcmp(mode,"remove-fail") || !strcmp(mode,"remove-fail-after"))) {
        injected_remove=1;
        if (!strcmp(mode,"remove-fail-after")) {
            CURLMcode result=curl_multi_remove_handle(multi,easy);
            if (result!=CURLM_OK) return result;
        }
        return CURLM_INTERNAL_ERROR;
    }
    return curl_multi_remove_handle(multi,easy);
}
#define curl_multi_remove_handle fixture_remove
#include "../src/connect.c"
#undef curl_multi_remove_handle
