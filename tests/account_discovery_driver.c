#include "account_discovery.h"
#include "connect_control.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/* Private translation-unit faults: real construction and close run first. */
static int taint, stop_created, connection_created;
static enum gin_connect_code driver_new(const struct gin_connect_settings *s,
 const struct gin_credential *c,const struct gin_connect_subscription *p,size_t n,
 gin_connect_frame_fn f,void *u,struct gin_connect **out) {
 enum gin_connect_code r=gin_connect_new(s,c,p,n,f,u,out);
 if(r==GIN_CONNECT_OK) connection_created=1;
 return r;
}
static int driver_cleanup_failed(const struct gin_connect *c) {
 int actual=gin_connect_cleanup_failed(c); return actual || taint;
}
#define gin_connect_new driver_new
#define gin_connect_cleanup_failed driver_cleanup_failed
#ifdef DISCOVERY_CORE
#include DISCOVERY_CORE
#else
#include "../src/account_discovery.c"
#endif
#undef gin_connect_new
#undef gin_connect_cleanup_failed
static int driver_tick(void *p,unsigned *w) {
 (void)p; (void)w; return stop_created && connection_created;
}
int main(int argc,char **argv) {
 struct gin_connect_settings s; struct gin_credential c, saved;
 struct gin_topology *t=NULL; struct gin_http_control control={driver_tick,NULL,0};
 struct gin_account_discovery_result r={0}; int legacy;
 if(argc!=4) return 2;
 legacy=!strcmp(argv[3],"LEGACY_REST_ONLY_BRIDGE");
 taint=!strcmp(argv[3],"taint") || !strcmp(argv[3],"stop-taint");
 stop_created=!strcmp(argv[3],"stop-taint");
 if(!legacy && !taint && strcmp(argv[3],"discover")) return 2;
 alarm(5); memset(&c,0,sizeof(c)); memset(&s,0,sizeof(s));
 snprintf(c.origin,sizeof(c.origin),"https://127.0.0.1:%s",argv[1]);
 strcpy(c.subject_id,"12345678-1234-1234-1234-123456789abc");
 strcpy(c.subject_name,"Alice"); strcpy(c.token,"fixture-token.DO-NOT-LOG"); saved=c;
 s.http.origin=c.origin; s.http.ca_file=argv[2];
 s.http.connect_ms=500; s.http.total_ms=1000;
 s.policy.client_protocol=4; s.policy.required_server_protocol=4; s.join_ms=500;
 if(gin_http_global_init()!=GIN_HTTP_OK) return 3;
 if(legacy) { r.topology=gin_topology_fetch_controlled(&s.http,&s.policy,&c,"#&",&t,&control);
 r.code=r.topology==GIN_TOPOLOGY_OK?GIN_DISCOVERY_OK:GIN_DISCOVERY_TOPOLOGY; }
#ifndef REST_ONLY
 else r=gin_account_discover(&s,&c,"#&",&control,700,&t);
#endif
 if(memcmp(&c,&saved,sizeof(c))) return 4;
 if(r.code!=GIN_DISCOVERY_OK && t!=NULL) return 5;
 if(taint && (!connection_created || !r.cleanup_failed)) return 6;
 printf("%d %d %zu %zu %d %d %d\n",r.code,gin_topology_queries_known(t),
 gin_topology_network_count(t),gin_topology_window_count(t),r.topology,r.cleanup_failed,r.quarantined);
 gin_topology_free(t); if(!r.quarantined) gin_http_global_cleanup();
 return 0;
}
