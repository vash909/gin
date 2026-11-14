#ifdef NDEBUG
#undef NDEBUG
#endif
#include "session_operation.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *const kinds[] = {"INPUT", "SEND", "JOIN", "PART", "QUERY"};
static const char *const states[] = {"PENDING", "REJECTED", "NOT_ATTEMPTED",
    "ACCEPTED_PERSISTED", "ACCEPTED_NO_PERSIST", "ACCEPTED_PENDING", "ACCEPTED_ACK",
    "REMOTE_FAILURE", "NOT_SENT", "UNCERTAIN"};
/* Literal contract matrix, independent of the formatter's implementation. */
static const unsigned char allowed[5][10] = {
    {0,1,0,0,0,0,0,0,0,0},
    {1,0,1,1,1,0,0,1,0,1},
    {1,0,1,0,0,1,0,1,0,1},
    {1,0,1,0,0,1,0,1,0,1},
    {1,0,1,0,0,0,1,1,1,1}
};
static void rejected(struct gin_operation_event event, size_t capacity)
{
    char out[GIN_OPERATION_RECORD_MAX+1u], before[GIN_OPERATION_RECORD_MAX+1u];
    size_t length = 123u;
    memset(out, 'Z', sizeof(out)); memcpy(before, out, sizeof(out));
    assert(!gin_operation_format(&event, out, capacity, &length));
    assert(length == 123u && !memcmp(out, before, sizeof(out)));
}
int main(void)
{
    struct gin_operation_event event = {1, GIN_OPERATION_SEND,
        GIN_OPERATION_ACCEPTED_PERSISTED, 0};
    char out[GIN_OPERATION_RECORD_MAX+1u];
    size_t length = 0, i, j;
    if (!gin_operation_format(&event, out, sizeof(out), &length)) {
        fputs("SEMANTIC_RED: valid operation record was not formatted\n", stderr);
        return 1;
    }
    assert(!strcmp(out, "OP1 1 SEND ACCEPTED_PERSISTED 0\n"));
    assert(length == strlen(out));
    for (i=0;i<5u;++i) for(j=0;j<10u;++j) {
        char expected[GIN_OPERATION_RECORD_MAX+1u];
        int n;
        event.id=7; event.kind=(enum gin_operation_kind)i;
        event.state=(enum gin_operation_state)j; event.code=23;
        if (!allowed[i][j]) { rejected(event, sizeof(out)); continue; }
        memset(out,'Z',sizeof(out)); length=0;
        n=snprintf(expected,sizeof(expected),"OP1 7 %s %s 23\n",kinds[i],states[j]);
        assert(n>0 && (size_t)n<sizeof(expected));
        assert(gin_operation_format(&event,out,sizeof(out),&length));
        assert(length==(size_t)n && !memcmp(out,expected,length+1u));
        assert(event.id==7 && event.kind==(enum gin_operation_kind)i &&
            event.state==(enum gin_operation_state)j && event.code==23);
        assert(out[length+1u]=='Z');
        assert(gin_operation_format(&event,out,length+1u,&length));
        rejected(event,(size_t)n); rejected(event,0);
    }
    event.id=UINT64_MAX; event.kind=GIN_OPERATION_QUERY;
    event.state=GIN_OPERATION_UNCERTAIN; event.code=GIN_OPERATION_CODE_MAX;
    assert(gin_operation_format(&event,out,sizeof(out),&length));
    assert(!strcmp(out,"OP1 18446744073709551615 QUERY UNCERTAIN 65535\n"));
    assert(length<=GIN_OPERATION_RECORD_MAX);
    event.id=0;rejected(event,sizeof(out));event.id=1;
    event.kind=GIN_OPERATION_KIND_COUNT;rejected(event,sizeof(out));
    event.kind=(enum gin_operation_kind)-1;rejected(event,sizeof(out));
    event.kind=GIN_OPERATION_SEND;event.state=GIN_OPERATION_STATE_COUNT;
    rejected(event,sizeof(out));event.state=(enum gin_operation_state)-1;
    rejected(event,sizeof(out));event.state=GIN_OPERATION_PENDING;
    event.code=GIN_OPERATION_CODE_MAX+1u;rejected(event,sizeof(out));event.code=0;
    assert(!gin_operation_format(NULL,out,sizeof(out),&length));
    assert(!gin_operation_format(&event,NULL,sizeof(out),&length));
    assert(!gin_operation_format(&event,out,sizeof(out),NULL));
    puts("operation formatting: valid states, bounds and rejection immutability OK");
    return 0;
}
