#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int stop_before_transfer(void *opaque, unsigned *wait)
{
    unsigned *calls=opaque;
    assert(*wait<=50u);
    ++*calls;
    return 1;
}
int main(void)
{
    unsigned char body[16];
    unsigned calls=0;
    struct gin_http_settings s={"https://127.0.0.1:1/",NULL,100,200,1024,0};
    struct gin_http_control control={stop_before_transfer,&calls,0};
    struct gin_http_request q={&s,GIN_HTTP_GET,NULL,NULL,NULL,0};
    struct gin_http_result r;
    memset(body,'x',sizeof(body));
    assert(gin_http_global_init()==GIN_HTTP_OK);
    r=gin_http_get_controlled(&s,body,sizeof(body),&control);
    assert(control.aborted==1 && calls==1 && r.code==GIN_HTTP_TRANSPORT);
    assert(r.http_status==0 && !r.body_length && !r.header_length);
    assert(body[0]=='x'); /* No failed-transfer bytes were written. */
    r=gin_http_request_controlled(&q,body,sizeof(body),&control);
    assert(control.aborted==1 && calls==1 && r.code==GIN_HTTP_TRANSPORT);
    assert(!r.body_length && !r.header_length);
    control.aborted=0; control.tick=NULL;
    r=gin_http_get_controlled(&s,body,sizeof(body),&control);
    assert(r.code==GIN_HTTP_INVALID && !control.aborted && calls==1);
    control.tick=stop_before_transfer; s.total_ms=0;
    r=gin_http_get_controlled(&s,body,sizeof(body),&control);
    assert(r.code==GIN_HTTP_INVALID && !control.aborted && calls==1);
    s.total_ms=200; q.method=GIN_HTTP_POST_JSON;
    r=gin_http_request_controlled(&q,body,sizeof(body),&control);
    assert(r.code==GIN_HTTP_INVALID && !control.aborted && calls==1);
    gin_http_global_cleanup();
    puts("Controlled HTTP validation and pre-transfer abort tests passed.");
    return 0;
}
