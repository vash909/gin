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

static void mutate(json_t *v)
{
    if(json_object_get(v,"protocol_version")) assert(!json_object_set_new(v,"protocol_version",json_integer(62)));
    else assert(!json_object_set_new(v,mutation==2?"code":"kind",json_string("ignored")));
}
#define json_loadb authority_loadb
#include "../src/connect.c"
#undef json_loadb

static int ack_case;
static int check(int reject)
{
    static const char ack[]="[\"1\",\"1\",\"grappa:user:Alice\",\"phx_reply\",{\"status\":\"ok\",\"response\":{\"protocol_version\":63}}]";
    static const char event[]="[null,null,\"grappa:user:Alice\",\"event\",{\"kind\":\"web_session_severed\",\"code\":\"rate_limit_flood\"}]";
    struct gin_connect s; struct gin_phoenix_frame *frame=NULL;
    const char *raw=ack_case?ack:event; enum gin_connect_code c;
    memset(&s,0,sizeof(s)); s.count=1; s.result.socket_epoch=1; s.settings.policy.required_server_protocol=36;
    s.state=ack_case?GIN_CONNECT_JOINING:GIN_CONNECT_READY;
    strcpy(s.topics[0].text,"grappa:user:Alice"); strcpy(s.topics[0].ref,"1");
    s.topics[0].status.topic=s.topics[0].text; s.topics[0].status.join_ref=s.topics[0].ref;
    s.topics[0].status.state=ack_case?GIN_CONNECT_TOPIC_PENDING:GIN_CONNECT_TOPIC_JOINED;
    assert(!clock_gettime(CLOCK_MONOTONIC,&s.deadline)); s.deadline.tv_sec+=60;
    assert(gin_phoenix_decode((const unsigned char *)raw,strlen(raw),&frame)==GIN_PHOENIX_OK);
    c=receive_frame(&s,frame); gin_phoenix_frame_free(frame);
    if(ack_case && c==GIN_CONNECT_OK) {
        if(reject || s.root_protocol!=63 || s.state!=GIN_CONNECT_READY || s.topics[0].status.state!=GIN_CONNECT_TOPIC_JOINED) {
            fprintf(stderr,"FAIL rootACK consumed mismatching DOM: protocol=%d\n",s.root_protocol); return 1;
        }
    } else if(!ack_case && c==GIN_CONNECT_SESSION_EXPIRED) {
        assert(!reject && s.session_expired && s.state==GIN_CONNECT_FAILED);
    } else {
        if(!ack_case && c==GIN_CONNECT_OK) { fprintf(stderr,"FAIL severance consumed mismatching DOM: ignored authority\n"); return 1; }
        assert(c==(ack_case?GIN_CONNECT_PREFLIGHT:GIN_CONNECT_MALFORMED) || c==GIN_CONNECT_NOMEM);
        assert(!s.root_protocol && !s.session_expired && s.state==GIN_CONNECT_FAILED);
    }
    if(!fail_at && !reject) assert(c==(ack_case?GIN_CONNECT_OK:GIN_CONNECT_SESSION_EXPIRED));
    gin_phoenix_bytes_free(&s.outbound); return 0;
}
int main(int argc,char **argv)
{
    size_t i,total; assert(argc==2);
    assert(!strcmp(argv[1],"ack-mutation") || !strcmp(argv[1],"ack-sweep") ||
        !strcmp(argv[1],"ack-null") || !strcmp(argv[1],"severance-mutation") ||
        !strcmp(argv[1],"severance-code-mutation") || !strcmp(argv[1],"severance-null") ||
        !strcmp(argv[1],"severance-sweep"));
    json_get_alloc_funcs(&saved_malloc,&saved_free);
    ack_case=!strncmp(argv[1],"ack",3); assert(!check(0)); total=allocations;
    if(strstr(argv[1],"mutation")) { mutation=strstr(argv[1],"code")?2:1; return check(1); }
    if(strstr(argv[1],"null")) { null_decode=1; return check(1); }
    if(strstr(argv[1],"sweep")) {
        for(i=1;i<=total;i++) { fail_at=i; if(check(0)) return 1; }
        printf("actual %s Jansson sweep: sites=%zu fault_hits=%zu\n",ack_case?"rootACK":"severance",total,fault_hits);
    }
    puts("connect exact control and fidelity checks passed"); return 0;
}
