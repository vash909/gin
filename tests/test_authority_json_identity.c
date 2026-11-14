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

static void mutate(json_t *v) { assert(!json_object_set_new(v,"name",json_string("Mallory"))); }
#include "gin_auth.h"
static struct gin_http_result fixture_request(const struct gin_http_request *, unsigned char *, size_t);
#define gin_http_request(q, p, n) fixture_request(q, p, n)
#define json_loadb authority_loadb
#include "../src/auth.c"
#undef json_loadb
#undef gin_http_request

static const char expected_name[] = "Alice-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz";
static const char raw[] = "{\"kind\":\"user\",\"id\":\"12345678-1234-1234-1234-123456789abc\",\"name\":\"Alice-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz\"}";
static struct gin_http_result fixture_request(const struct gin_http_request *q, unsigned char *out, size_t cap)
{
    struct gin_http_result r = {0};
    assert(q->method == GIN_HTTP_GET && !strcmp(q->settings->url,"https://example.invalid/me"));
    assert(!strcmp(q->bearer,"fixture-token")); assert(cap >= sizeof(raw));
    memcpy(out,raw,sizeof(raw)-1); r.code=GIN_HTTP_OK; r.http_status=200; r.body_length=sizeof(raw)-1; return r;
}
static int check(int reject)
{
    struct gin_auth_settings s={"https://example.invalid",NULL,100,200};
    struct gin_credential input={"https://example.invalid","fixture-token","12345678-1234-1234-1234-123456789abc","Alice"}, out, zero={0};
    enum gin_auth_code c=gin_auth_identity(&s,&input,&out);
    if(c==GIN_AUTH_OK) {
        if(reject || strcmp(out.origin,"https://example.invalid") || strcmp(out.token,"fixture-token") || strcmp(out.subject_id,"12345678-1234-1234-1234-123456789abc") || strcmp(out.subject_name,expected_name)) {
            fprintf(stderr,"FAIL identity consumed mismatching DOM: name=%s\n",out.subject_name); return 1;
        }
    } else { assert(!memcmp(out.origin,zero.origin,sizeof(out.origin)) && !memcmp(out.token,zero.token,sizeof(out.token)) && !memcmp(out.subject_id,zero.subject_id,sizeof(out.subject_id)) && !memcmp(out.subject_name,zero.subject_name,sizeof(out.subject_name))); if(reject) assert(c==GIN_AUTH_MALFORMED); }
    if(!fail_at && !reject) assert(c==GIN_AUTH_OK);
    return 0;
}
int main(int argc,char **argv)
{
    size_t i,total; assert(argc == 2 && (!strcmp(argv[1],"mutation") || !strcmp(argv[1],"sweep"))); json_get_alloc_funcs(&saved_malloc,&saved_free);
    assert(!check(0)); total=allocations;
    if(!strcmp(argv[1],"mutation")) { mutation=1; return check(1); }
    if(!strcmp(argv[1],"sweep")) {
        for(i=1;i<=total;i++) { fail_at=i; if(check(0)) return 1; }
        printf("actual identity Jansson sweep: sites=%zu fault_hits=%zu\n",total,fault_hits);
    }
    puts("identity exact control and fidelity checks passed"); return 0;
}
