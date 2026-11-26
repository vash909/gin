#define _POSIX_C_SOURCE 200809L
#include "account_owner.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#define R(x) do { if(!(x)) {fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static int statusfd,stopfd;
static int stop_tick(void *unused,unsigned *wait) {
 char c;ssize_t n;(void)unused;(void)wait;n=read(stopfd,&c,1);
 R(n==1 || (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK)));return n==1;
}
static void phase(void *unused,unsigned phase) {char c=phase==1?'R':'H';(void)unused;R(write(statusfd,&c,1)==1);}
static void lease(int fd,int held) {
 pid_t p=fork();int s;R(p>=0);
 if(!p) {struct flock f={0};int rc;f.l_type=F_WRLCK;f.l_whence=SEEK_SET;rc=fcntl(fd,F_SETLK,&f);
 _exit(held?(rc==-1 && (errno==EACCES || errno==EAGAIN)?0:1):(rc==0?0:1));}
 R(waitpid(p,&s,0)==p && s==0);
}
int main(int argc,char **argv) {
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#aquiet\",\"joined\":true,\"source\":\"joined\"},{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}],\"2\":[{\"network_id\":2,\"target_nick\":\"Other\",\"opened_at\":\"opaque\"}]}}";
 struct gin_connect_settings settings={0};struct gin_credential c={0};
 struct gin_topology *b=NULL,*t=NULL;struct gin_account_plan *plan=NULL;
 struct gin_root root=GIN_ROOT_INITIALIZER;struct gin_http_control stop={stop_tick,NULL,0};
 struct gin_account_owner_input input;struct gin_account_owner_result result;int probe;
 R(argc==6);alarm(18);statusfd=atoi(argv[4]);stopfd=atoi(argv[5]);
 R((fcntl(statusfd,F_GETFL)&O_NONBLOCK) && (fcntl(stopfd,F_GETFL)&O_NONBLOCK));
 R(strlen(argv[1])<sizeof(c.origin));strcpy(c.origin,argv[1]);strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc");strcpy(c.subject_name,"Alice");strcpy(c.token,"fixture-token.DO-NOT-LOG");
 settings.http.origin=c.origin;settings.http.ca_file=argv[2];settings.http.connect_ms=700;settings.http.total_ms=3000;
 settings.policy.client_protocol=4;settings.policy.required_server_protocol=4;settings.join_ms=2000;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&plan)==GIN_ACCOUNT_PLAN_OK && gin_account_plan_topic_count(plan)==12);
 R(gin_root_acquire(argv[3],&root)==0);probe=dup(root.lockfd);R(probe>=0);lease(probe,1);
 input.root=&root;input.plan=plan;input.topology=t;input.settings=&settings;input.credential=&c;
 input.chantypes="#&";input.encryption=GIN_RENDER_MODE_PLAIN;input.stop=&stop;input.phase=phase;input.phase_context=NULL;
 result=gin_account_owner_run(&input);
 if(result.code!=GIN_ACCOUNT_OWNER_OK) fprintf(stderr,"owner code=%d receive=%d dispatch=%d history=%d cleanup=%d quarantine=%d\n",result.code,result.receive,result.dispatch,result.history.code,result.cleanup_failed,result.quarantined);
 R(result.code==GIN_ACCOUNT_OWNER_OK && result.stopped && !result.cleanup_failed && !result.quarantined);
 R(result.history.destinations==7 && result.history.drained==7 && result.history.pages==7 && result.history.appended==8 && result.appended==1);
 lease(probe,1);R(gin_account_plan_topic_count(plan)==12);R(gin_credential_validate(&c)==GIN_CREDENTIAL_OK);
 R(gin_root_release(&root)==0);lease(probe,0);R(close(probe)==0);
 gin_account_plan_free(plan);gin_topology_free(t);gin_topology_free(b);
 if(!result.quarantined) gin_http_global_cleanup();
 puts("PASS owner late LIVE; held root retained then caller release verified");return 0;
}
