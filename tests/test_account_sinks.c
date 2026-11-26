#include "account_plan.h"
#include "account_root.h"
#include "account_fs.h"
#ifndef LEGACY_SINGLE_SINK_BRIDGE
#include "account_sinks.h"
#endif
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %d: %s errno=%d\n",__LINE__,#x,errno); exit(1); } } while (0)
static const char boot[] = "{\"networks\":[{\"id\":1,\"slug\":\"one\",\"nick\":\"Self\"},{\"id\":2,\"slug\":\"two\",\"nick\":\"Other\"}],\"channels\":{\"one\":[{\"name\":\"#in\",\"joined\":true,\"source\":\"joined\"},{\"name\":\"#out\",\"joined\":true,\"source\":\"joined\"}],\"two\":[]},\"heads\":{}}";
static const char queries[] = "{\"kind\":\"query_windows_list\",\"windows\":{\"2\":[{\"network_id\":2,\"target_nick\":\"Peer\",\"opened_at\":\"x\"}]}}";
#include "sinks_exec_probe.h"
static void tree(int fd, size_t *dirs, size_t *files, size_t *fifos)
{
 DIR *d=fdopendir(dup(fd)); struct dirent *e; CHECK(d);
 while ((e=readdir(d))) { struct stat s; int child; if (!strcmp(e->d_name,".")||!strcmp(e->d_name,"..")) continue; CHECK(fstatat(fd,e->d_name,&s,AT_SYMLINK_NOFOLLOW)==0); CHECK(s.st_uid==geteuid());
 if(S_ISDIR(s.st_mode)) { CHECK((s.st_mode&07777)==0700); ++*dirs; child=openat(fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); CHECK(child>=0); tree(child,dirs,files,fifos); CHECK(close(child)==0); }
 else { CHECK((s.st_mode&07777)==0600 && s.st_nlink==1); if(S_ISFIFO(s.st_mode)) ++*fifos; else { CHECK(S_ISREG(s.st_mode)); ++*files; } } }
 CHECK(closedir(d)==0);
}
#ifndef LEGACY_SINGLE_SINK_BRIDGE
static int fdcount(void) { int i,n=0; for(i=0;i<512;i++) if(fcntl(i,F_GETFD)>=0) n++; return n; }
static void flags(int in,int out) { CHECK(in>=0&&out>=0); CHECK(fcntl(in,F_GETFL)&O_NONBLOCK); CHECK(fcntl(out,F_GETFL)&O_APPEND); CHECK(fcntl(in,F_GETFD)&FD_CLOEXEC); CHECK(fcntl(out,F_GETFD)&FD_CLOEXEC); }
#endif
int main(int argc, char **argv)
{
 int probe = sinks_probe_dispatch(argc, argv);
 if (probe >= 0) return probe;
 if (argc != 1) return 2;
 sinks_test_executable = argv[0];
 struct gin_topology *b=NULL,*t=NULL; struct gin_account_plan *p=NULL;
 struct gin_credential c={"https://fixture.invalid","SyntheticBearer","12345678-1234-1234-1234-123456789abc","SyntheticName"};
 struct gin_root r=GIN_ROOT_INITIALIZER; char path[4096]; const char *tmp=getenv("TMPDIR"); size_t dirs=0,files=0,fifos=0; struct stat st;
 alarm(15); if(!tmp||!*tmp) tmp=".build"; CHECK(snprintf(path,sizeof(path),"%s/sinks-XXXXXX",tmp)>0); CHECK(mkdtemp(path));
 CHECK(gin_topology_parse_boot((const unsigned char *)boot,strlen(boot),"#",&b)==GIN_TOPOLOGY_OK);
 CHECK(gin_topology_apply_queries(b,(const unsigned char *)queries,strlen(queries),&t)==GIN_TOPOLOGY_OK);
 CHECK(gin_account_plan_new(t,"#","SyntheticName",&p)==GIN_ACCOUNT_PLAN_OK); CHECK(gin_account_plan_network_count(p)==2&&gin_account_plan_window_count(p)==3);
 CHECK(gin_root_acquire(path,&r)==0);
#ifdef LEGACY_SINGLE_SINK_BRIDGE
 CHECK(gin_account_root_bind(&r,&c,"#").code==GIN_ACCOUNT_ROOT_OK);
 CHECK(gin_account_root_init(&r,"one","#in","#")==0);
 puts("LEGACY setup complete: actual plan, Root binding, one native FS recipe; complete layout must fail");
#else
 { struct gin_account_sinks *s=NULL,*saved; struct gin_account_sinks_result result; int owned[10],n=0,fd,before; size_t i; ino_t inode; unsigned char metadata[8192],again[8192]; ssize_t len;
 result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_OK&&result.binding_attempted&&result.binding.created&&s);
 CHECK(gin_account_sinks_network_count(s)==2&&gin_account_sinks_window_count(s)==3);
 saved=s; result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_INVALID&&s==saved&&!result.binding_attempted);
 for(i=0;i<2;i++) { owned[n++]=gin_account_sinks_network_input(s,i); owned[n++]=gin_account_sinks_network_output(s,i); flags(owned[n-2],owned[n-1]); }
 for(i=0;i<3;i++) { owned[n++]=gin_account_sinks_window_input(s,i); owned[n++]=gin_account_sinks_window_output(s,i); flags(owned[n-2],owned[n-1]); }
 CHECK(gin_account_sinks_window_input(s,3)==-1); CHECK(write(owned[1],"prefix",6)==6); CHECK(fstat(owned[1],&st)==0); inode=st.st_ino;
 fd=openat(r.rootfd,".gin.account",O_RDONLY|O_NOFOLLOW); CHECK(fd>=0); len=read(fd,metadata,sizeof(metadata)); CHECK(len>0&&close(fd)==0);
 for(i=0;i<(size_t)len;i++) { CHECK((size_t)len-i<strlen(c.token)||memcmp(metadata+i,c.token,strlen(c.token))); CHECK((size_t)len-i<strlen(c.subject_name)||memcmp(metadata+i,c.subject_name,strlen(c.subject_name))); }
 CHECK(gin_account_sinks_close(s)==0); s=NULL; for(i=0;i<(size_t)n;i++) CHECK(fcntl(owned[i],F_GETFD)==-1&&errno==EBADF); lease(path,1);
 strcpy(c.token,"RotatedBearer"); strcpy(c.subject_name,"RotatedName"); result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_OK&&!result.binding.created);
 CHECK(fstat(gin_account_sinks_network_output(s,0),&st)==0&&st.st_ino==inode&&st.st_size==6); CHECK(gin_account_sinks_close(s)==0); s=NULL;
 fd=openat(r.rootfd,".gin.account",O_RDONLY|O_NOFOLLOW); CHECK(fd>=0&&read(fd,again,sizeof(again))==len&&!memcmp(metadata,again,(size_t)len)&&close(fd)==0);
 before=fdcount(); strcpy(c.origin,"https://other.invalid"); result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_BINDING&&result.binding.code==GIN_ACCOUNT_ROOT_MISMATCH&&!s&&fdcount()==before); strcpy(c.origin,"https://fixture.invalid"); c.subject_id[0]='a'; result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_BINDING&&!s); c.subject_id[0]='1';
 }
#endif
 /* Independent exact endpoint oracle, unchanged in legacy and green. */
 CHECK(fstatat(r.rootfd,"one/in",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISFIFO(st.st_mode));
 CHECK(fstatat(r.rootfd,"one/out",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode));
 CHECK(fstatat(r.rootfd,"two/in",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISFIFO(st.st_mode));
 CHECK(fstatat(r.rootfd,"two/out",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode));
 CHECK(fstatat(r.rootfd,"one/#in/in",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISFIFO(st.st_mode));
 CHECK(fstatat(r.rootfd,"one/#out/out",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode));
 CHECK(fstatat(r.rootfd,"two/@peer/out",&st,AT_SYMLINK_NOFOLLOW)==0&&S_ISREG(st.st_mode));
 CHECK(fstatat(r.rootfd,"one/@self",&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT);
 tree(r.rootfd,&dirs,&files,&fifos); CHECK(dirs==5&&files==7&&fifos==5); lease(path,1);
#ifndef LEGACY_SINGLE_SINK_BRIDGE
 { struct gin_account_sinks *s=NULL; struct gin_account_sinks_result result; int fd,before,kind; char bytes[8];
 /* Already bound tree: guard, not nonempty-root rejection. */
 for(kind=0;kind<2;kind++) { CHECK(renameat(r.rootfd,"two/out",r.rootfd,"two/kept")==0);
 if(!kind) CHECK(symlinkat("kept",r.rootfd,"two/out")==0); else { fd=openat(r.rootfd,"two/out",O_WRONLY|O_CREAT|O_EXCL,0600); CHECK(fd>=0&&write(fd,"victim",6)==6&&fchmod(fd,0644)==0&&close(fd)==0); }
 before=fdcount(); result=gin_account_sinks_new(&r,&c,p,"#",&s); CHECK(result.code==GIN_ACCOUNT_SINKS_IO&&result.binding.code==GIN_ACCOUNT_ROOT_OK&&!result.binding.created&&!s&&fdcount()==before); lease(path,1);
 fd=openat(r.rootfd,kind?"two/out":"one/out",O_RDONLY); CHECK(fd>=0&&read(fd,bytes,sizeof(bytes))==6&&!memcmp(bytes,kind?"victim":"prefix",6)&&close(fd)==0);
 CHECK(unlinkat(r.rootfd,"two/out",0)==0&&renameat(r.rootfd,"two/kept",r.rootfd,"two/out")==0); }
 CHECK(gin_account_sinks_close(NULL)==0);
 }
#endif
 CHECK(gin_root_release(&r)==0); lease(path,0); gin_account_plan_free(p); gin_topology_free(t); gin_topology_free(b); puts("account-sinks: ok"); return 0;
}
