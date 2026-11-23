#define _XOPEN_SOURCE 700
#include "account_output.h"
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>
#include <unistd.h>
static void lease(const struct gin_root *r) { pid_t p=fork(); int s; if(!p) { struct flock f={0}; f.l_type=F_WRLCK; f.l_whence=SEEK_SET; _exit(fcntl(r->lockfd,F_SETLK,&f)==-1 && (errno==EACCES || errno==EAGAIN)?0:1); } if(p<0 || waitpid(p,&s,0)!=p || s) exit(1); }
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define R(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
int main(int argc,char **argv)
{
 const char boot[]="{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Own1\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Own2\"}],\"channels\":{\"one\":[{\"name\":\"#old1\",\"joined\":true,\"source\":\"joined\"}],\"two\":[{\"name\":\"#old2\",\"joined\":true,\"source\":\"joined\"}]},\"heads\":{}}";
 const char queries[]="{\"kind\":\"query_windows_list\",\"windows\":{\"1\":[{\"network_id\":1,\"target_nick\":\"Peer\",\"opened_at\":\"opaque\"}]}}";
 struct gin_connect_settings s={0}; struct gin_credential c={0};
 struct gin_topology *b=NULL,*t=NULL; struct gin_account_plan *p=NULL;
 struct gin_account_receive *a=NULL; struct gin_account_dispatch *d=NULL;
 struct gin_account_destination dest; struct gin_render_record *record=NULL;
 struct gin_history_page *page=NULL; unsigned i,n=0; const char *mode=getenv("GIN_DISPATCH_CASE");
 const size_t nets[]={0,1,0,1,0,0}; const size_t wins[]={SIZE_MAX,SIZE_MAX,0,2,1,1};
 const char *kinds[]={"server_event","notice","privmsg","privmsg","privmsg","privmsg"};
 int life=0; struct gin_root root=GIN_ROOT_INITIALIZER; struct gin_account_sinks *sinks=NULL; struct gin_account_output *output=NULL;
 int barrier=mode && (!strcmp(mode,"wrong_event_query") || !strcmp(mode,"wrong_event_nick") || !strcmp(mode,"query_same") || !strcmp(mode,"nick_same") || !strcmp(mode,"life_unknown") || !strcmp(mode,"diagnostic") || !strcmp(mode,"life_joined") || !strcmp(mode,"life_kicked"));
 R(!mode || !strcmp(mode,"positive") || barrier || !strcmp(mode,"query_changed") || !strcmp(mode,"nick_changed") || !strcmp(mode,"life_offscope") || !strcmp(mode,"life_malformed") || !strcmp(mode,"life_encrypted") || !strcmp(mode,"wrong_source") || !strcmp(mode,"wrong_network") || !strcmp(mode,"unknown_dm") || !strcmp(mode,"encrypted") || !strcmp(mode,"channel_dm") || !strcmp(mode,"fidelity_kind") || !strcmp(mode,"fidelity_peer") || !strcmp(mode,"fidelity_id"));
 R(argc==4); alarm(15); strcpy(c.origin,argv[1]); strcpy(c.subject_id,"22345678-1234-1234-1234-123456789abc"); strcpy(c.subject_name,"Alice"); strcpy(c.token,"fixture-token.DO-NOT-LOG");
 s.http.origin=c.origin; s.http.ca_file=argv[2]; s.http.connect_ms=700; s.http.total_ms=1500; s.policy.client_protocol=4; s.policy.required_server_protocol=4; s.join_ms=1500;
 R(gin_http_global_init()==GIN_HTTP_OK);
 R(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#&",&b)==GIN_TOPOLOGY_OK);
 R(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 R(gin_account_plan_new(t,"#&","Alice",&p)==GIN_ACCOUNT_PLAN_OK);
 R(gin_account_receive_new(&s,&c,p,&a)==GIN_CONNECT_OK);
 R(gin_account_dispatch_new(a,p,t,"#&",GIN_RENDER_MODE_UNKNOWN,&d)==GIN_SESSION_UNSUPPORTED && !d);
 R(gin_account_dispatch_new(a,p,t,"#&",GIN_RENDER_MODE_PLAIN,&d)==GIN_SESSION_OK);
 R(!gin_account_receive_epoch(a)); R(!gin_account_receive_drained(a));
 R(gin_root_acquire(argv[3],&root)==0);
 R(gin_account_sinks_new(&root,&c,p,"#&",&sinks).code==GIN_ACCOUNT_SINKS_OK);
 R(gin_account_receive_start(a)==GIN_CONNECT_OK);
 R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_UNKNOWN,&output).code!=GIN_CHECKPOINT_OK && !output);
 R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_PLAIN,&output).code==GIN_CHECKPOINT_OK); lease(&root);
 for(i=0;i<150;i++) {
  R(gin_account_receive_service(a,20,123)==GIN_CONNECT_OK);
  while(gin_account_receive_peek(a)) {
   const struct gin_live_view *before=gin_account_receive_peek(a);
   enum gin_session_code code;
   dest.network_index=12345;dest.window_index=23456;dest.role=GIN_RENDER_STATUS;
   code=gin_account_dispatch_next(d,&dest,&record,&page);
   if(barrier && (!strcmp(mode,"life_joined") || !strcmp(mode,"life_kicked")) && !life) {
    struct gin_phoenix_view bytes;
    const char *expected=!strcmp(mode,"life_kicked") ? "[1970-01-01T00:00:00.123Z recv] kicked target=\"#old1\" state=\"kicked\"\n" : "[1970-01-01T00:00:00.123Z recv] joined target=\"#old1\" state=\"joined\"\n";
    R(code==GIN_SESSION_OK && record && !page);
    R(dest.network_index==0 && dest.window_index==0 && dest.role==GIN_RENDER_CHANNEL);
    bytes=gin_render_record_bytes(record);R(bytes.length==strlen(expected) && !memcmp(bytes.data,expected,bytes.length));

    R(dest.terminal==!strcmp(mode,"life_kicked"));

    gin_render_record_free(record);record=NULL;life=1;
    if(!strcmp(mode,"life_kicked")) {
     dest.network_index=12345;dest.window_index=23456;
     R(gin_account_dispatch_next(d,&dest,&record,&page)==GIN_SESSION_ROUTE && !record && !page);
     R(dest.network_index==12345 && dest.window_index==23456);goto done;
    }
    continue;
   }
   if(mode && strcmp(mode,"positive") && !barrier) {
    if(code!=((!strcmp(mode,"encrypted") || !strcmp(mode,"life_encrypted"))?GIN_SESSION_UNSUPPORTED:GIN_SESSION_ROUTE) || record || page)
     fprintf(stderr,"observed code=%d record=%d history=%d destination=%zu/%zu role=%d\n",code,!!record,!!page,dest.network_index,dest.window_index,dest.role);
    R(code==((!strcmp(mode,"encrypted") || !strcmp(mode,"life_encrypted"))?GIN_SESSION_UNSUPPORTED:GIN_SESSION_ROUTE) && !record && !page);
    R(dest.network_index==12345 && dest.window_index==23456 && dest.role==GIN_RENDER_STATUS);
    R(gin_account_receive_peek(a)==before);
    R(gin_account_dispatch_next(d,&dest,&record,&page)==code && !record && !page);
    goto done;
   }
   R(code==GIN_SESSION_OK);

   if(record) R(!dest.terminal);

   if(record) {
    char expected[256]; struct gin_phoenix_view bytes=gin_render_record_bytes(record);
    R(n<6 && dest.network_index==nets[n] && dest.window_index==wins[n]);
    R(dest.role==(n<2?GIN_RENDER_STATUS:n<4?GIN_RENDER_CHANNEL:GIN_RENDER_QUERY));
    snprintf(expected,sizeof(expected),"[1970-01-01T00:00:00.123Z] %s sender=\"sender\" body=\"hello\"\n",kinds[n]);
    R(bytes.length==strlen(expected) && !memcmp(bytes.data,expected,bytes.length));
    R(page && gin_history_page_row(page,0)->id==99);
    R(!strcmp(gin_history_page_scope(page)->subject_id,c.subject_id)); { struct gin_account_output_result wr; struct gin_account_destination bad=dest; bad.role=GIN_RENDER_STATUS; if(dest.role!=GIN_RENDER_STATUS) R(gin_account_output_append(output,&bad,record,0).code==GIN_CHECKPOINT_INVALID); wr=gin_account_output_append(output,&dest,record,0); R(wr.code==GIN_CHECKPOINT_OK && wr.appended==bytes.length && wr.checkpoint.published); } n++;
    gin_render_record_free(record);gin_history_page_free(page);record=NULL;page=NULL;
   } else R(!page);
  }
  if(n==6 && gin_account_receive_state(a)==GIN_CONNECT_READY && gin_account_receive_drained(a)) break;
 }
 R(n==6 && i<150); if(!mode || !strcmp(mode,"positive")) puts("PASS exact6 destinations bytes IDs local-drain");
done:
 R(gin_account_output_close(&output)==0);lease(&root);
 R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_PLAIN,&output).code==GIN_CHECKPOINT_OK);
 { struct gin_account_destination sd={0,SIZE_MAX,GIN_RENDER_STATUS,0}; struct gin_checkpoint_progress progress; struct gin_render_record *r=NULL; R(gin_account_output_progress(output,&sd,&progress)==0 && progress.anchor==0 && progress.unknown_before); R(gin_render_plain_message_role(GIN_RENDER_STATUS,"notice",(const unsigned char *)"trusted",7,(const unsigned char *)"prefix",6,123,GIN_RENDER_MODE_PLAIN,&r)==GIN_RENDER_OK); R(gin_account_output_append(output,&sd,r,7).code==GIN_CHECKPOINT_OK); gin_render_record_free(r); R(gin_account_output_close(&output)==0); R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_PLAIN,&output).code==GIN_CHECKPOINT_OK); R(gin_account_output_progress(output,&sd,&progress)==0 && progress.anchor==7); }
 /* Characterization on unchanged production: private fresh root only. */
 { const char *fault=getenv("GIN_OUTPUT_CASE");
  R(!fault || !strcmp(fault,"replace") || !strcmp(fault,"partial"));
  if(fault) {
   struct gin_account_destination sd={0,SIZE_MAX,GIN_RENDER_STATUS,0};
   struct gin_checkpoint_progress progress={999,0};
   struct gin_account_output_result wr,again;
   struct gin_render_record *r=NULL; struct gin_phoenix_view bytes;
   struct stat old,after,cpold,cpafter; struct rlimit saved,limited;
   struct sigaction previous,ignore;
   unsigned char cpbytes[GIN_CHECKPOINT_BYTES_MAX],now[GIN_CHECKPOINT_BYTES_MAX],logbytes[1024],got[1024];
   ssize_t cpn,ln; int dir,fd; size_t prefix;
   R(gin_account_output_progress(output,&sd,&progress)==0 && progress.anchor==7);
   dir=openat(root.rootfd,"one",O_RDONLY|O_DIRECTORY|O_CLOEXEC);R(dir>=0);
   fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_CLOEXEC);R(fd>=0);
   R(fstat(fd,&cpold)==0);cpn=read(fd,cpbytes,sizeof(cpbytes));R(cpn>0 && close(fd)==0);
   fd=gin_account_sinks_network_output(sinks,0);R(fstat(fd,&old)==0);
   { int readfd=openat(dir,"out",O_RDONLY|O_CLOEXEC);R(readfd>=0);ln=read(readfd,logbytes,sizeof(logbytes));R(ln==old.st_size && close(readfd)==0); }
   R(gin_render_plain_message_role(GIN_RENDER_STATUS,"notice",(const unsigned char *)"fault",5,(const unsigned char *)"boundary",8,123,GIN_RENDER_MODE_PLAIN,&r)==GIN_RENDER_OK);
   bytes=gin_render_record_bytes(r);
   if(!strcmp(fault,"replace")) {
    R(renameat(dir,"out",dir,"out.saved")==0);
    { int replacement=openat(dir,"out",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);R(replacement>=0 && write(replacement,"replacement\n",12)==12 && close(replacement)==0); }
    wr=gin_account_output_append(output,&sd,r,8);
    R(wr.code==GIN_CHECKPOINT_UNSAFE && wr.appended==0 && !wr.checkpoint.published);
   } else {
    R(getrlimit(RLIMIT_FSIZE,&saved)==0);limited=saved;limited.rlim_cur=(rlim_t)old.st_size+5;
    memset(&ignore,0,sizeof(ignore));ignore.sa_handler=SIG_IGN;R(sigemptyset(&ignore.sa_mask)==0);
    R(sigaction(SIGXFSZ,&ignore,&previous)==0 && setrlimit(RLIMIT_FSIZE,&limited)==0);
    wr=gin_account_output_append(output,&sd,r,8);
    R(setrlimit(RLIMIT_FSIZE,&saved)==0 && sigaction(SIGXFSZ,&previous,NULL)==0);
    R(wr.code==GIN_CHECKPOINT_IO && wr.appended>0 && wr.appended<bytes.length && wr.appended==5 && !wr.checkpoint.published);
   }
   R(!wr.cleanup_failed && !wr.checkpoint.cleanup_failed);
   R(fstat(fd,&after)==0 && after.st_ino==old.st_ino && after.st_size==old.st_size+(off_t)wr.appended);
   prefix=wr.appended;
   R(gin_account_output_progress(output,&sd,&progress)==-1 && progress.anchor==7);
   again=gin_account_output_append(output,&sd,r,8);
   R(again.code==GIN_CHECKPOINT_INVALID && !again.appended && !again.checkpoint.published);
   R(fstat(fd,&after)==0 && after.st_size==old.st_size+(off_t)prefix);
   { int readfd=openat(dir,prefix?"out":"out.saved",O_RDONLY|O_CLOEXEC);R(readfd>=0);R(read(readfd,got,sizeof(got))==ln+(ssize_t)prefix && close(readfd)==0);R(!memcmp(got,logbytes,(size_t)ln) && !memcmp(got+ln,bytes.data,prefix)); }
   fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_CLOEXEC);R(fd>=0);
   R(fstat(fd,&cpafter)==0 && cpafter.st_ino==cpold.st_ino && cpafter.st_size==cpold.st_size);
   R(read(fd,now,sizeof(now))==cpn && !memcmp(now,cpbytes,(size_t)cpn) && close(fd)==0);
   gin_render_record_free(r);R(close(dir)==0);lease(&root);
   R(gin_account_output_close(&output)==0);lease(&root);
   /* checkpoint.c output_guard rejects held/path inode mismatch before decode;
    * matching inode with a partial uncheckpointed tail reaches AMBIGUOUS. */
   R(gin_account_output_new(&root,sinks,p,a,"#&",GIN_RENDER_MODE_PLAIN,&output).code==(!strcmp(fault,"replace")?GIN_CHECKPOINT_UNSAFE:GIN_CHECKPOINT_AMBIGUOUS) && !output);
   puts("PASS storage boundary sticky unchanged-checkpoint ambiguous-reopen");
  }
 }
 R(gin_account_output_close(&output)==0);lease(&root); R(gin_account_sinks_close(sinks)==0); R(gin_root_release(&root)==0);
 gin_account_dispatch_free(d);gin_account_receive_free(a);gin_account_plan_free(p);gin_topology_free(t);gin_topology_free(b);gin_http_global_cleanup();return 0;
}
