/* Isolated dependency-fault sweep and transparent TEST-ONLY DOM mutation.
 * Mutation proves evidence policy, not a runtime allocator exploit. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <jansson.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static size_t allocation, fail_at, allocations, fault_hits, decode_calls;
static int mutation, null_decode;
static json_malloc_t saved_malloc;
static json_free_t saved_free;
static void *fault_malloc(size_t n)
{
    allocation++;
    if (fail_at && allocation == fail_at) { fault_hits++; return NULL; }
    return saved_malloc(n);
}
static void fault_free(void *p) { saved_free(p); }
static void mutate(json_t *v);
static json_t *authority_loadb(const char *p, size_t n, size_t flags, json_error_t *e)
{
    json_t *v;
    decode_calls++;
    if (null_decode) return NULL;
    allocation = 0;
    json_set_alloc_funcs(fault_malloc, fault_free);
    v = json_loadb(p, n, flags, e);
    /* Both allocator pairs use the exact same underlying malloc/free. */
    json_set_alloc_funcs(saved_malloc, saved_free);
    allocations = allocation;
    if (v && mutation) mutate(v);
    return v;
}

static void mutate(json_t *v) { assert(!json_object_set_new(v,"protocol_version",json_integer(62))); }
#define json_loadb authority_loadb
#include "../src/protocol_parse.c"
#undef json_loadb

static const char raw[] = "{\"server\":\"grappa\",\"protocol_version\":63,\"min_protocol_version\":1,\"version\":\"fixture\"}";
static int check(int expect_reject)
{
    struct gin_protocol_policy p = {63, 36};
    struct gin_protocol_info out;
    enum gin_protocol_code c = gin_protocol_parse(&p, (const unsigned char *)raw, strlen(raw), &out);
    if (c == GIN_PROTOCOL_OK) {
        if (expect_reject || out.server_protocol != 63 || out.minimum_client_protocol != 1 || strcmp(out.software_version,"fixture")) {
            fprintf(stderr,"FAIL config consumed mismatching DOM: code=%d protocol=%d\n", c,out.server_protocol); return 1;
        }
    } else assert(!out.server_protocol && !out.minimum_client_protocol && !out.software_version[0]);
    if (!fail_at && !expect_reject) assert(c == GIN_PROTOCOL_OK);
    return 0;
}
int main(int argc, char **argv)
{
    size_t total, i;
    assert(argc == 2 && (!strcmp(argv[1],"mutation") || !strcmp(argv[1],"sweep"))); json_get_alloc_funcs(&saved_malloc,&saved_free);
    assert(!check(0)); total=allocations;
    if (!strcmp(argv[1],"mutation")) { mutation=1; return check(1); }
    if (!strcmp(argv[1],"sweep")) {
        for(i=1;i<=total;i++) { fail_at=i; if(check(0)) return 1; }
        printf("actual Jansson allocation sweep: sites=%zu fault_hits=%zu\n",total,fault_hits);
    }
    printf("config exact control and fidelity checks passed; decoder calls=%zu\n",decode_calls); return 0;
}
