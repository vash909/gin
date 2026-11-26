#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_credential.h"
#ifndef LEGACY_POLICY
#include "credential_identity.h"
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *origins[] = {"https://example.test", "https://[::1]:443",
        "https://x:999999", "https://[.]", "https://-.", "https://",
        "http://example.test", "HTTPS://x", "https://x/", "https://x:"};
    const char *ids[] = {"01234567-89AB-cdef-0123-456789ABCDEF",
        "01234567-89ab-cdef-0123-456789abcdef", "01234567_89ab-cdef-0123-456789abcdef",
        "01234567-89ab-cdef-0123-456789abcdeg", "", "01234567-89ab-cdef-0123-456789abcdefx"};
    struct gin_credential c;
    char expected_name[sizeof(c.subject_name)];
    size_t i;
    char long_origin[GIN_CREDENTIAL_ORIGIN_MAX + 2], long_id[38];
    memset(&c, 0, sizeof(c));
    strcpy(c.origin, origins[0]); strcpy(c.subject_id, ids[0]);
    strcpy(c.token, "Synthetic-identity-only"); strcpy(c.subject_name, "DifferentDisplay");
    memcpy(expected_name, c.subject_name, sizeof(expected_name));
    for (i = 0; i < sizeof(origins)/sizeof(origins[0]); ++i) {
        strcpy(c.origin, origins[i]);
        assert((gin_credential_validate(&c) == GIN_CREDENTIAL_OK) == (i < 5));
#ifndef LEGACY_POLICY
        assert(!!gin_credential_identity_origin_valid(origins[i]) == (i < 5));
#endif
    }
    strcpy(c.origin, origins[0]);
    for (i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) {
        size_t length = strlen(ids[i]);
        /* The fixed legacy field deliberately has no NUL for oversized input.
         * Keep the longer string intact for the standalone pointer API. */
        if (length < sizeof(c.subject_id)) ++length;
        else length = sizeof(c.subject_id);
        memcpy(c.subject_id, ids[i], length);
        assert(memcmp(c.subject_name, expected_name, sizeof(expected_name)) == 0);
        assert((gin_credential_validate(&c) == GIN_CREDENTIAL_OK) == (i < 2));
#ifndef LEGACY_POLICY
        assert(!!gin_credential_identity_subject_id_valid(ids[i]) == (i < 2));
#endif
    }
    memset(long_origin, 'x', sizeof(long_origin));
    memcpy(long_origin, "https://", 8); long_origin[sizeof(long_origin)-1] = 0;
    memset(long_id, 'a', sizeof(long_id)); long_id[sizeof(long_id)-1] = 0;
    memcpy(c.origin, long_origin, sizeof(c.origin));
    assert(gin_credential_validate(&c) == GIN_CREDENTIAL_INVALID);
#ifndef LEGACY_POLICY
    assert(!gin_credential_identity_origin_valid(NULL));
    assert(!gin_credential_identity_subject_id_valid(NULL));
    assert(!gin_credential_identity_origin_valid(long_origin));
    assert(!gin_credential_identity_subject_id_valid(long_id));
#endif
    puts("native identity policy characterization passed");
    return 0;
}
