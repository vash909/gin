/* Real component composition: baseline is missing composition, not an old API defect. */
#include "account_registry.h"
#ifndef HOT_BASELINE
#include "account_sinks_hot.h"
#include "fs_private.h"
#include "account_naming.h"
#include <errno.h>
#include <unistd.h>
static int root_calls, pair_calls, post_pair_fault;
static struct gin_account_root_check_result counted_root(const struct gin_root *r,
 const char *o,const char *u,const char *t)
{ ++root_calls; return gin_account_root_check(r,o,u,t); }
static struct gin_fs_pair_result counted_pair(const struct gin_root *r,
 const char *n,const char *w,const char *t,gin_fs_name_prepare_fn f,struct gin_fs_pair *p)
{
 struct gin_fs_pair_result result; ++pair_calls;
 result=gin_root_pair_prepare_named(r,n,w,t,f,p);
 /* Private sink-boundary fault after actual checked FS success, not an
  * injected failure inside FS. Both invocation-owned FDs close once. */
 if(post_pair_fault&&!result.error) {
  int mode=post_pair_fault; post_pair_fault=0;
  (void)close(p->input); (void)close(p->output); p->input=p->output=-1;
  result.error=ENOSPC; result.cleanup_failed=mode==2;
 }
 return result;
}
#define gin_account_root_check counted_root
#define gin_root_pair_prepare_named counted_pair
#include "../src/account_sinks.c"
#undef gin_account_root_check
#undef gin_root_pair_prepare_named
#endif
#define main legacy_main
#include "test_account_sinks.c"
#undef main
static const char first[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"},{\"network_id\":2,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
int main(int argc, char **argv)
{
 int probe = sinks_probe_dispatch(argc, argv);
 if (probe >= 0) return probe;
 if (argc != 1) return 2;
 sinks_test_executable = argv[0];
 struct gin_topology *b=NULL,*t=NULL,*a=NULL;
 struct gin_account_plan *p=NULL;
 struct gin_account_registry *reg=NULL;
 struct gin_account_registry_delta d;
 struct gin_account_registry_scope scope={"https://fixture.invalid","12345678-1234-1234-1234-123456789abc","SyntheticName","#",7};
 struct gin_credential c={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_root r=GIN_ROOT_INITIALIZER;
 struct gin_account_sinks *s=NULL;
 char path[4096]; const char *tmp=getenv("TMPDIR");
 int old[10],flags_before[10],fdflags[10],n=0,exists; off_t offsets[10];
 struct stat before[10],st; size_t i;
 alarm(25); if(!tmp||!*tmp) tmp=".build";
 CHECK(snprintf(path,sizeof(path),"%s/hot-sinks-XXXXXX",tmp)>0&&mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)first,strlen(first),&a)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,"#","SyntheticName",&p)==GIN_ACCOUNT_PLAN_OK);
 CHECK(gin_account_registry_new(t,&scope,&reg)==GIN_ACCOUNT_REGISTRY_OK);
 CHECK(gin_root_acquire(path,&r)==0);
 CHECK(gin_account_sinks_new(&r,&c,p,"#",&s).code==GIN_ACCOUNT_SINKS_OK);
 for(i=0;i<2;i++) { old[n++]=gin_account_sinks_network_input(s,i); old[n++]=gin_account_sinks_network_output(s,i); }
 for(i=0;i<3;i++) { old[n++]=gin_account_sinks_window_input(s,i); old[n++]=gin_account_sinks_window_output(s,i); }
 CHECK(write(old[9],"prefix",6)==6);
 for(i=0;i<10;i++) { CHECK(fstat(old[i],&before[i])==0); flags_before[i]=fcntl(old[i],F_GETFL); fdflags[i]=fcntl(old[i],F_GETFD); offsets[i]=lseek(old[i],0,SEEK_CUR); }
 lease(path,1);
#ifndef HOT_BASELINE
 { struct gin_account_registry_scope copy=scope; char *origin=strdup(scope.origin), *uuid=strdup(scope.subject_uuid), *subject=strdup(scope.topic_subject), *types=strdup(scope.chantypes);
 CHECK(origin&&uuid&&subject&&types); copy.origin=origin; copy.subject_uuid=uuid; copy.topic_subject=subject; copy.chantypes=types;
 CHECK(gin_account_sinks_hot_append(s,&scope,0,0).code==GIN_ACCOUNT_SINKS_HOT_INVALID);
 copy.epoch=0; CHECK(gin_account_sinks_hot_bind(s,reg,&copy,0).code==GIN_ACCOUNT_SINKS_HOT_SCOPE); copy.epoch=scope.epoch;
 CHECK(gin_account_sinks_hot_bind(s,reg,&copy,1).code==GIN_ACCOUNT_SINKS_HOT_STALE);
 CHECK(gin_account_sinks_hot_bind(s,reg,&copy,0).code==GIN_ACCOUNT_SINKS_HOT_OK);
 memset(origin,'x',strlen(origin)); free(origin); free(uuid); free(subject); free(types);
 CHECK(gin_account_sinks_hot_bind(s,reg,&scope,0).code==GIN_ACCOUNT_SINKS_HOT_INVALID);
 }
#endif
 CHECK(gin_account_registry_apply(reg,a,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK&&d.windows_added==1&&d.generation==1);
 CHECK(!strcmp(gin_account_registry_window_at(reg,3)->name.original_wire_target,"Aaron"));
 CHECK(fstatat(r.rootfd,"two/@aaron/in",&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT);
 CHECK(fstatat(r.rootfd,"two/@aaron/out",&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT);
#ifndef HOT_BASELINE
 { struct gin_account_registry_scope bad=scope; int calls=root_calls, pairs=pair_calls; struct gin_account_sinks_hot_result h;
 for(i=0;i<5;i++) { bad=scope; if(i==0) bad.origin="https://other.invalid"; if(i==1) bad.subject_uuid="aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"; if(i==2) bad.topic_subject="Other"; if(i==3) bad.chantypes="#&"; if(i==4) bad.epoch++;
 h=gin_account_sinks_hot_append(s,&bad,1,3); CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_SCOPE&&!h.error&&!h.paths_attempted); }
 CHECK(gin_account_sinks_hot_append(s,&scope,0,3).code==GIN_ACCOUNT_SINKS_HOT_STALE);
 CHECK(gin_account_sinks_hot_append(s,&scope,2,3).code==GIN_ACCOUNT_SINKS_HOT_STALE);
 CHECK(gin_account_sinks_hot_append(s,&scope,1,4).code==GIN_ACCOUNT_SINKS_HOT_INVALID);
 /* Private injected-state guard, not publicly reachable registry behavior. */
 { struct gin_topology_window *w=(struct gin_topology_window *)gin_account_registry_window_at(reg,3); enum gin_window_kind saved=w->name.kind;
 w->name.kind=GIN_WINDOW_CHANNEL;
 CHECK(gin_account_sinks_hot_append(s,&scope,1,3).code==GIN_ACCOUNT_SINKS_HOT_UNSUPPORTED); w->name.kind=saved; }
 CHECK(root_calls==calls&&pair_calls==pairs);
 }
 { struct gin_account_sinks_hot_result h=gin_account_sinks_hot_append(s,&scope,1,3);
 CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_OK&&h.added&&h.paths_attempted&&h.window_count==4&&h.generation==1&&!h.error&&!h.cleanup_failed); }
#endif
 exists=fstatat(r.rootfd,"two/@aaron/in",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISFIFO(st.st_mode);
 exists=exists&&fstatat(r.rootfd,"two/@aaron/out",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode);
 for(i=0;i<10;i++) { CHECK(fstat(old[i],&st)==0&&st.st_dev==before[i].st_dev&&st.st_ino==before[i].st_ino&&st.st_size==before[i].st_size); CHECK(fcntl(old[i],F_GETFL)==flags_before[i]&&fcntl(old[i],F_GETFD)==fdflags[i]&&lseek(old[i],0,SEEK_CUR)==offsets[i]); }
 CHECK(gin_account_sinks_window_output(s,2)==old[9]);
 { char bytes[6]; int readfd=openat(r.rootfd,"two/@peer/out",O_RDONLY|O_NOFOLLOW); CHECK(readfd>=0&&read(readfd,bytes,6)==6&&!memcmp(bytes,"prefix",6)&&close(readfd)==0); }
#ifndef HOT_BASELINE
 { int calls=root_calls,pairs=pair_calls; struct gin_account_sinks_hot_result h=gin_account_sinks_hot_append(s,&scope,1,3);
 CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_OK&&!h.added&&!h.paths_attempted&&h.generation==1&&root_calls==calls&&pair_calls==pairs); }
 { const char *extra="{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"},{\"network_id\":2,\"target_nick\":\"Beta\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Gamma\",\"opened_at\":\"x\"}]}}";
 const char *own="{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Peer\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Aaron\",\"opened_at\":\"new\"},{\"network_id\":2,\"target_nick\":\"Beta\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Gamma\",\"opened_at\":\"x\"},{\"network_id\":2,\"target_nick\":\"Other\",\"opened_at\":\"x\"}]}}";
 struct gin_topology *next=NULL; struct gin_account_sinks_hot_result h;
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)extra,strlen(extra),&next)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_registry_apply(reg,next,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK&&d.windows_added==2&&d.generation==2); gin_topology_free(next); next=NULL;
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)own,strlen(own),&next)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_registry_apply(reg,next,&scope,&d)==GIN_ACCOUNT_REGISTRY_OK&&d.windows_added==1&&!d.sources_added&&d.generation==3); gin_topology_free(next);
 for(i=4;i<6;i++) { h=gin_account_sinks_hot_append(s,&scope,3,i); CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_OK&&h.added&&h.generation==3&&h.window_count==i+1); }
 /* Existing safe partial layout is reused without replacing the file. */
 CHECK(gin_account_root_init(&r,"two","Other","#")==0);
 CHECK(fstatat(r.rootfd,"two/@other/out",&st,AT_SYMLINK_NOFOLLOW)==0);
 { ino_t inode=st.st_ino; const char *fault=getenv("HOT_FAULT"); int count=fdcount();
 if(fault&&!strcmp(fault,"missing")) CHECK(unlinkat(r.rootfd,".gin.account",0)==0);
 if(fault&&!strcmp(fault,"mismatch")) { int meta=openat(r.rootfd,".gin.account",O_WRONLY|O_NOFOLLOW); CHECK(meta>=0&&pwrite(meta,"X",1,0)==1&&close(meta)==0); }
 if(fault&&!strcmp(fault,"symlink")) { CHECK(unlinkat(r.rootfd,"two/@other/out",0)==0); CHECK(symlinkat("../@peer/out",r.rootfd,"two/@other/out")==0); }
 if(fault&&!strcmp(fault,"postpair")) post_pair_fault=1;
 if(fault&&!strcmp(fault,"taint")) post_pair_fault=2;
 h=gin_account_sinks_hot_append(s,&scope,3,6);
 if(fault&&*fault) {
 int calls=root_calls,pairs=pair_calls; struct gin_account_sinks_hot_result again;
 CHECK(!h.added&&h.window_count==6&&h.generation==3&&h.error);
 if(!strcmp(fault,"mismatch")) CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_SCOPE&&h.error==EPERM&&!h.paths_attempted);
 else CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_IO);
 if(!strcmp(fault,"missing")) CHECK(h.error==EIO&&!h.paths_attempted);
 if(!strcmp(fault,"postpair")||!strcmp(fault,"taint")) CHECK(h.error==ENOSPC&&h.paths_attempted);
 CHECK(h.cleanup_failed==(!strcmp(fault,"taint")));
 again=gin_account_sinks_hot_append(s,&scope,3,6);
 CHECK(again.code==GIN_ACCOUNT_SINKS_HOT_FAILED&&again.error==h.error&&again.cleanup_failed==h.cleanup_failed&&!again.paths_attempted&&root_calls==calls&&pair_calls==pairs);
 CHECK(gin_account_sinks_window_input(s,6)==-1&&fdcount()==count);
 } else { CHECK(h.code==GIN_ACCOUNT_SINKS_HOT_OK&&h.added&&h.window_count==7); CHECK(fstat(gin_account_sinks_window_output(s,6),&st)==0&&st.st_ino==inode); }
 }
 CHECK(gin_account_sinks_window_output(s,2)==old[9]); CHECK(fstat(old[9],&st)==0&&st.st_ino==before[9].st_ino&&st.st_size==6);
 }
#endif
 lease(path,1); CHECK(gin_account_sinks_close(s)==0); lease(path,1);
 CHECK(gin_root_release(&r)==0); lease(path,0);
 gin_account_registry_free(reg); gin_account_plan_free(p); gin_topology_free(a); gin_topology_free(t); gin_topology_free(b);
#ifdef HOT_BASELINE
 puts("Healthy cold sinks + real registry metadata append; ordered cleanup complete; missing composition oracle follows");
#endif
 CHECK(exists); puts("hot sinks real composition: ok"); return 0;
}
