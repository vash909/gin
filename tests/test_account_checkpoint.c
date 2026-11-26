#ifdef NDEBUG
#undef NDEBUG
#endif
#include "checkpoint.h"
#include "account_naming.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef LEGACY_CHECKPOINT_BRIDGE
#define OPEN(r,s,f,h,p) gin_checkpoint_open(r,s,f,h,p)
#else
#define OPEN(r,s,f,h,p) gin_checkpoint_open_named(r,s,f,h,p,gin_account_name_prepare)
#endif
static void held(int fd)
{
    pid_t p=fork(); int status;
    assert(p>=0);
    if (!p) {
        struct flock l; int rc, saved;
        alarm(2); memset(&l,0,sizeof(l)); l.l_type=F_WRLCK; l.l_whence=SEEK_SET;
        rc=fcntl(fd,F_SETLK,&l); saved=errno;
        _exit(rc==-1 && (saved==EAGAIN || saved==EACCES) ? 0 : 1);
    }
    assert(waitpid(p,&status,0)==p && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
static void preserved(int fd, int dir, const struct stat *before, const char *bytes)
{
    struct stat after; char buf[64]; int r;
    assert(fstat(fd,&after)==0 && after.st_ino==before->st_ino && after.st_dev==before->st_dev);
    assert(after.st_size==(off_t)strlen(bytes));
    r=openat(dir,"out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC); assert(r>=0);
    fd=r; r=pread(fd,buf,sizeof(buf),0);
    assert(r==(int)strlen(bytes) && !memcmp(buf,bytes,(size_t)r)); assert(close(fd)==0);
}
int main(void)
{
    const char *tmp=getenv("TMPDIR"); char path[4096];
    struct gin_root root=GIN_ROOT_INITIALIZER;
    struct gin_checkpoint *cp=NULL;
    struct gin_checkpoint_progress progress={999,17};
    struct gin_checkpoint_write_result wr;
    struct gin_history_scope s={"https://example.invalid","12345678-1234-1234-1234-123456789abc",
        "NetA","#Room","#&","OwnA",3000000001LL,1,GIN_RENDER_CHANNEL,GIN_RENDER_MODE_PLAIN};
    struct stat a,l,st; int out,dir,old,legacy,net; unsigned char meta[8192]; ssize_t n;
    alarm(10); if (!tmp || !*tmp) tmp=".build";
    assert(snprintf(path,sizeof(path),"%s/gin-account-cp-XXXXXX",tmp)>0 && mkdtemp(path));
    assert(gin_root_acquire(path,&root)==0);
    assert(gin_root_init_named(&root,"NetA","#Room","#&",gin_account_name_prepare)==0);
    out=gin_root_output_open_named(&root,"NetA","#Room","#&",gin_account_name_prepare);
    dir=gin_root_window_open_named(&root,"NetA","#Room","#&",gin_account_name_prepare);
    assert(out>=0 && dir>=0 && fstat(out,&a)==0);
    assert(write(out,"prefix\n",7)==7);
    /* Real old constructor fails here: legitimate account #room, no room. */
    assert(OPEN(&root,&s,out,&cp,&progress)==GIN_CHECKPOINT_UNKNOWN);
    assert(cp && progress.anchor==0 && progress.unknown_before==1); held(root.lockfd);
    assert(write(out,"next\n",5)==5);
    wr=gin_checkpoint_commit(cp,41); assert(wr.code==GIN_CHECKPOINT_OK && wr.published && !wr.cleanup_failed);
    assert(gin_checkpoint_close(&cp)==0);
    assert(gin_root_init(&root,"NetA","#Room","#&")==0);
    old=gin_root_output_open(&root,"NetA","#Room","#&");
    legacy=gin_root_window_open(&root,"NetA","#Room","#&");
    assert(old>=0 && legacy>=0 && fstat(old,&l)==0 && write(old,"legacy\n",7)==7);
    assert(fstatat(legacy,GIN_CHECKPOINT_FILENAME,&st,AT_SYMLINK_NOFOLLOW)==-1 && errno==ENOENT);
    s.socket_epoch=9; s.own_nick="Different"; s.target="#room";
    assert(OPEN(&root,&s,out,&cp,&progress)==GIN_CHECKPOINT_OK && progress.anchor==41);
    assert(gin_checkpoint_close(&cp)==0);
    progress.anchor=999; progress.unknown_before=17; s.network_id++;
    assert(OPEN(&root,&s,out,&cp,&progress)==GIN_CHECKPOINT_SCOPE && !cp);
    assert(progress.anchor==999 && progress.unknown_before==17); s.network_id--;
    assert(OPEN(&root,&s,old,&cp,&progress)==GIN_CHECKPOINT_UNSAFE && !cp);
    assert(progress.anchor==999 && progress.unknown_before==17);
#ifndef LEGACY_CHECKPOINT_BRIDGE
    assert(gin_checkpoint_open_named(&root,&s,out,&cp,&progress,NULL)==GIN_CHECKPOINT_INVALID && !cp);
    assert(progress.anchor==999 && progress.unknown_before==17);
#endif
    assert(gin_checkpoint_open(&root,&s,old,&cp,&progress)==GIN_CHECKPOINT_UNKNOWN);
    assert(gin_checkpoint_close(&cp)==0);
    /* Move selected metadata aside: named never loads from legacy. */
    assert(renameat(dir,GIN_CHECKPOINT_FILENAME,legacy,GIN_CHECKPOINT_FILENAME)==0);
    assert(OPEN(&root,&s,out,&cp,&progress)==GIN_CHECKPOINT_UNKNOWN);
    assert(gin_checkpoint_close(&cp)==0);
    assert(renameat(legacy,GIN_CHECKPOINT_FILENAME,dir,GIN_CHECKPOINT_FILENAME)==0);
    net=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); assert(net>=0);
    n=read(net,meta,sizeof(meta)); assert(n>56 && !memcmp(meta,"GINCP\0\0\1",8)); assert(close(net)==0);
    assert(write(out,"extra\n",6)==6);
    progress.anchor=999; progress.unknown_before=17;
    assert(OPEN(&root,&s,out,&cp,&progress)==GIN_CHECKPOINT_AMBIGUOUS && !cp);
    assert(progress.anchor==999 && progress.unknown_before==17);
    preserved(out,dir,&a,"prefix\nnext\nextra\n"); preserved(old,legacy,&l,"legacy\n"); held(root.lockfd);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(unlinkat(dir,"in",0)==0 && unlinkat(dir,"out",0)==0);
    assert(unlinkat(legacy,"in",0)==0 && unlinkat(legacy,"out",0)==0);
    assert(close(dir)==0 && close(legacy)==0 && close(out)==0 && close(old)==0);
    net=openat(root.rootfd,"%4Eet%41",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); assert(net>=0);
    assert(unlinkat(net,"#room",AT_REMOVEDIR)==0 && unlinkat(net,"room",AT_REMOVEDIR)==0 && close(net)==0);
    assert(unlinkat(root.rootfd,"%4Eet%41",AT_REMOVEDIR)==0); held(root.lockfd);
    assert(gin_root_release(&root)==0);
    assert(snprintf(path+strlen(path),sizeof(path)-strlen(path),"/.gin.lock")>0 && unlink(path)==0);
    *strrchr(path,'/')='\0'; assert(rmdir(path)==0);
    puts("account checkpoint storage, namespace, inode and lock guards passed"); return 0;
}
