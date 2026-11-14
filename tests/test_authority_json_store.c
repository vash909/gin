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

static void mutate(json_t *v) { assert(!json_object_set_new(v,"token",json_string("changed-token"))); }
#define json_loadb authority_loadb
#include "../src/credential.c"
#undef json_loadb

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
static const char raw[]="{\"version\":1,\"origin\":\"https://example.invalid\",\"token\":\"fixture-token\",\"subject_id\":\"12345678-1234-1234-1234-123456789abc\",\"subject_name\":\"Alice-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz\"}";
static struct gin_root root;
static int check(int reject)
{
    struct gin_credential out,zero={0};
    enum gin_credential_code c=gin_credential_load(&root,"https://example.invalid",&out);
    if(c==GIN_CREDENTIAL_OK) {
        if(reject || strcmp(out.origin,"https://example.invalid") || strcmp(out.token,"fixture-token") || strcmp(out.subject_id,"12345678-1234-1234-1234-123456789abc") || strcmp(out.subject_name,"Alice-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz-abcdefghijklmnopqrstuvwxyz")) {
            fprintf(stderr,"FAIL store consumed mismatching DOM: token=%s\n",out.token); return 1;
        }
    } else { assert(!memcmp(out.origin,zero.origin,sizeof(out.origin)) && !memcmp(out.token,zero.token,sizeof(out.token)) && !memcmp(out.subject_id,zero.subject_id,sizeof(out.subject_id)) && !memcmp(out.subject_name,zero.subject_name,sizeof(out.subject_name))); if(reject) assert(c==GIN_CREDENTIAL_INVALID); }
    if(!fail_at && !reject) assert(c==GIN_CREDENTIAL_OK);
    return 0;
}
int main(int argc,char **argv)
{
    char path[4096]; int fd,result=0,n; size_t i,total; struct flock lock={0};
    const char *temporary = getenv("TMPDIR");
    assert(argc == 2 && (!strcmp(argv[1],"mutation") || !strcmp(argv[1],"sweep"))); if (!temporary || !*temporary) temporary = ".";
    n=snprintf(path,sizeof(path),"%s/gin-authority-store-XXXXXX",temporary);
    assert(n>0 && (size_t)n<sizeof(path));
    assert(mkdtemp(path)); assert(!chmod(path,0700)); root.rootfd=open(path,O_RDONLY|O_DIRECTORY); assert(root.rootfd>=0);
    root.lockfd=openat(root.rootfd,".gin.lock",O_RDWR|O_CREAT|O_EXCL,0600); assert(root.lockfd>=0);
    lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET; assert(!fcntl(root.lockfd,F_SETLK,&lock));
    fd=openat(root.rootfd,GIN_CREDENTIAL_FILENAME,O_WRONLY|O_CREAT|O_EXCL,0600); assert(fd>=0);
    assert(write(fd,raw,sizeof(raw)-1)==(ssize_t)(sizeof(raw)-1)); assert(!close(fd));
    json_get_alloc_funcs(&saved_malloc,&saved_free); assert(!check(0)); total=allocations;
    if(!strcmp(argv[1],"mutation")) { mutation=1; result=check(1); }
    if(!strcmp(argv[1],"sweep")) {
        for(i=1;i<=total;i++) { fail_at=i; if(check(0)) {result=1;break;} }
        printf("actual store Jansson sweep: sites=%zu fault_hits=%zu\n",total,fault_hits);
    }
    assert(!unlinkat(root.rootfd,GIN_CREDENTIAL_FILENAME,0)); assert(!unlinkat(root.rootfd,".gin.lock",0));
    assert(!close(root.lockfd)); assert(!close(root.rootfd)); assert(!rmdir(path));
    if(!result) puts("private synthetic store exact control and fidelity checks passed");
    return result;
}
