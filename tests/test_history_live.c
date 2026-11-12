#include "gin_history.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)) {fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
int main(void)
{
    const char raw[]="[null,null,\"grappa:user:alice/network:Net/channel:oldnick\",\"event\",{\"kind\":\"message\",\"message\":{\"id\":99,\"server_time\":123,\"network\":\"Net\",\"channel\":\"OldNick\",\"kind\":\"privmsg\",\"sender\":\"peer\",\"body\":\"hello\",\"dm_with\":\"PEER\"}}]";
    struct gin_history_scope s={"https://example.test","01234567-89ab-cdef-0123-456789abcdef","Net","Peer","#","NewNick",1,7,GIN_RENDER_QUERY,GIN_RENDER_MODE_PLAIN};
    struct gin_render_context c={"alice","Net","Peer","OldNick","#","join7",7,GIN_RENDER_QUERY,GIN_RENDER_MODE_PLAIN};
    struct gin_phoenix_frame *f=NULL;struct gin_history_page *p=NULL;
    CHECK(gin_phoenix_decode((const unsigned char *)raw,strlen(raw),&f)==GIN_PHOENIX_OK);
    CHECK(gin_history_live_decode(f,&c,&s,6,&p)==GIN_HISTORY_STALE);CHECK(!p);
    c.source_target="Other";CHECK(gin_history_live_decode(f,&c,&s,7,&p)==GIN_HISTORY_ROUTE);c.source_target="OldNick";
    CHECK(gin_history_live_decode(f,&c,&s,7,&p)==GIN_HISTORY_OK);
    CHECK(gin_history_page_source(p)==GIN_HISTORY_LIVE && !gin_history_page_request(p));
    gin_phoenix_frame_free(f);f=NULL;c.source_target="gone";
    CHECK(!strcmp(gin_history_page_row(p,0)->body,"hello"));CHECK(!strcmp(gin_history_page_row(p,0)->dm_with,"PEER"));
    gin_history_page_free(p);p=NULL;
    {
        const char reserved[]="[null,null,\"grappa:user:alice/network:Net/channel:oldnick\",\"event\",{\"kind\":\"future\",\"body\":\"prefix\\u0001E2Ebad\"}]";
        c.source_target="OldNick";
        CHECK(gin_phoenix_decode((const unsigned char *)reserved,strlen(reserved),&f)==GIN_PHOENIX_OK);
        CHECK(gin_history_live_decode(f,&c,&s,7,&p)==GIN_HISTORY_UNSUPPORTED);CHECK(!p);gin_phoenix_frame_free(f);
    }
    puts("history live: PASS");return 0;
}
