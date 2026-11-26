#ifdef NDEBUG
#undef NDEBUG
#endif
#include "checkpoint.h"
#include "fs_private.h"
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
#ifdef LEGACY_NETWORK_BRIDGE
#define OPEN gin_checkpoint_open
#else
#define OPEN gin_checkpoint_open_network
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
static void shut(struct gin_checkpoint **cp, int lock)
{ assert(gin_checkpoint_close(cp)==0 && !*cp); held(lock); }
static void reject(const struct gin_root *r, const struct gin_history_scope *s,
                   int fd, enum gin_checkpoint_code expected)
{
    struct gin_checkpoint *cp=NULL;
    struct gin_checkpoint_progress p={999,17};
    assert(OPEN(r,s,fd,&cp,&p)==expected && !cp);
    assert(p.anchor==999 && p.unknown_before==17); held(r->lockfd);
}
int main(void)
{
    const char *tmp=getenv("TMPDIR"); char path[4096], component[256];
    struct gin_root r=GIN_ROOT_INITIALIZER;
    struct gin_history_scope s={"https://example.invalid","12345678-1234-1234-1234-123456789abc",
        "NetA","$server","#&","OwnA",3000000001LL,1,GIN_RENDER_STATUS,GIN_RENDER_MODE_PLAIN};
    struct gin_checkpoint *cp=NULL;
    struct gin_checkpoint_progress p={999,17};
    struct gin_checkpoint_write_result w;
    struct gin_name legacy;
    struct stat before,after,st; unsigned char bytes[8192];
    int out,dir,other,win,wd,fd; ssize_t n;
    alarm(15); if (!tmp || !*tmp) tmp=".build";
    assert(snprintf(path,sizeof(path),"%s/gin-network-cp-XXXXXX",tmp)>0 && mkdtemp(path));
    assert(gin_root_acquire(path,&r)==0);
    assert(gin_root_network_init(&r,s.network_slug)==0);
    assert(gin_network_encode(s.network_slug,component,sizeof(component))==0);
    dir=openat(r.rootfd,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    out=gin_root_network_output_open(&r,s.network_slug);
    assert(dir>=0 && out>=0 && fstat(out,&before)==0);
    assert((before.st_mode&07777)==0600 && before.st_nlink==1);
    assert(fstat(dir,&st)==0 && (st.st_mode&07777)==0700);
    assert(write(out,"prefix\n",7)==7); held(r.lockfd);
    puts("positive network/private-output/held-lock prerequisites passed"); fflush(stdout);
    /* The unmodified old constructor traverses a window, not this network. */
    assert(OPEN(&r,&s,out,&cp,&p)==GIN_CHECKPOINT_UNKNOWN);
    assert(cp && p.anchor==0 && p.unknown_before==1);
    assert(write(out,"next\n",5)==5);
    w=gin_checkpoint_commit(cp,41);
    assert(w.code==GIN_CHECKPOINT_OK && w.published && !w.cleanup_failed);
    shut(&cp,r.lockfd);
    s.socket_epoch=99; s.own_nick="Changed";
    assert(OPEN(&r,&s,out,&cp,&p)==GIN_CHECKPOINT_OK && p.anchor==41);
    shut(&cp,r.lockfd);
    fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC); assert(fd>=0);
    assert(fstat(fd,&st)==0 && (st.st_mode&07777)==0600 && st.st_nlink==1);
    n=read(fd,bytes,sizeof(bytes)); assert(n>56 && !memcmp(bytes,"GINCP\0\0\1",8)); assert(close(fd)==0);
    assert(gin_name_prepare(s.network_slug,s.target,s.chantypes,&legacy)==0);
    assert(fstatat(dir,legacy.window_component,&st,AT_SYMLINK_NOFOLLOW)==-1 && errno==ENOENT);
    /* A real, valid old STATUS checkpoint is never a network fallback. */
    assert(gin_root_init(&r,s.network_slug,s.target,s.chantypes)==0);
    { int old=gin_root_output_open(&r,s.network_slug,s.target,s.chantypes);
      int olddir=gin_root_window_open(&r,s.network_slug,s.target,s.chantypes);
      assert(old>=0 && olddir>=0);
      assert(gin_checkpoint_open(&r,&s,old,&cp,&p)==GIN_CHECKPOINT_UNKNOWN);
      assert(write(old,"decoy\n",6)==6);
      w=gin_checkpoint_commit(cp,777); assert(w.code==GIN_CHECKPOINT_OK && w.published);
      shut(&cp,r.lockfd);
      assert(renameat(dir,GIN_CHECKPOINT_FILENAME,dir,"network-saved")==0);
      assert(OPEN(&r,&s,out,&cp,&p)==GIN_CHECKPOINT_UNKNOWN && p.anchor==0);
      shut(&cp,r.lockfd);
      assert(gin_checkpoint_open(&r,&s,old,&cp,&p)==GIN_CHECKPOINT_OK && p.anchor==777);
      shut(&cp,r.lockfd);
      assert(renameat(dir,"network-saved",dir,GIN_CHECKPOINT_FILENAME)==0);
      assert(unlinkat(olddir,GIN_CHECKPOINT_FILENAME,0)==0);
      assert(unlinkat(olddir,"in",0)==0 && unlinkat(olddir,"out",0)==0);
      assert(close(olddir)==0 && close(old)==0);
      assert(unlinkat(dir,legacy.window_component,AT_REMOVEDIR)==0);
    }
    { int owned=gin_root_network_open(&r,s.network_slug); struct stat ownedst;
      assert(owned>=0 && fstat(owned,&ownedst)==0 && S_ISDIR(ownedst.st_mode));
      assert((fcntl(owned,F_GETFD)&FD_CLOEXEC)!=0); assert(close(owned)==0);
      errno=0; assert(fcntl(owned,F_GETFD)==-1 && errno==EBADF); held(r.lockfd);
      assert(gin_root_network_open(NULL,s.network_slug)==-1);
      assert(gin_root_network_open(&r,NULL)==-1);
      assert(gin_root_network_open(&r,"missing")==-1 && errno==ENOENT);
      assert(symlinkat(component,r.rootfd,"netlink")==0);
      assert(gin_root_network_open(&r,"netlink")==-1);
      assert(unlinkat(r.rootfd,"netlink",0)==0);
      assert(fchmod(dir,0755)==0); assert(gin_root_network_open(&r,s.network_slug)==-1);
      assert(fchmod(dir,0700)==0); held(r.lockfd);
    }
    s.network_id++; reject(&r,&s,out,GIN_CHECKPOINT_SCOPE); s.network_id--;
    s.origin="http://example.invalid"; reject(&r,&s,out,GIN_CHECKPOINT_INVALID); s.origin="https://example.invalid";
    s.role=GIN_RENDER_QUERY; reject(&r,&s,out,GIN_CHECKPOINT_INVALID); s.role=GIN_RENDER_STATUS;
    s.target="other"; reject(&r,&s,out,GIN_CHECKPOINT_INVALID); s.target="$server";
    s.network_slug="missing"; reject(&r,&s,out,GIN_CHECKPOINT_IO); s.network_slug="NetA";
    assert(gin_root_network_init(&r,"Other")==0);
    other=gin_root_network_output_open(&r,"Other"); assert(other>=0);
    reject(&r,&s,other,GIN_CHECKPOINT_UNSAFE);
    s.network_slug="Other"; reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE); s.network_slug="NetA";
    /* Ordinary windows retain their original storage and lease. */
    assert(gin_root_init(&r,"NetA","#Room","#&")==0);
    win=gin_root_output_open(&r,"NetA","#Room","#&");
    wd=gin_root_window_open(&r,"NetA","#Room","#&"); assert(win>=0 && wd>=0);
    s.role=GIN_RENDER_CHANNEL; s.target="#Room";
    assert(gin_checkpoint_open(&r,&s,win,&cp,&p)==GIN_CHECKPOINT_UNKNOWN); shut(&cp,r.lockfd);
    s.role=GIN_RENDER_STATUS; s.target="$server";
    assert(renameat(dir,GIN_CHECKPOINT_FILENAME,dir,"saved")==0);
    fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_WRONLY|O_CREAT|O_EXCL,0600); assert(fd>=0);
    assert(write(fd,"bad",3)==3 && close(fd)==0);
    reject(&r,&s,out,GIN_CHECKPOINT_MALFORMED);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(symlinkat("saved",dir,GIN_CHECKPOINT_FILENAME)==0);
    reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE); assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(linkat(dir,"saved",dir,GIN_CHECKPOINT_FILENAME,0)==0);
    reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE); assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(renameat(dir,"saved",dir,GIN_CHECKPOINT_FILENAME)==0);
    assert(fchmodat(dir,GIN_CHECKPOINT_FILENAME,0644,0)==0);
    reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE); assert(fchmodat(dir,GIN_CHECKPOINT_FILENAME,0600,0)==0);
    assert(fchmod(out,0644)==0); reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE); assert(fchmod(out,0600)==0);
    assert(linkat(dir,"out",dir,"alias",0)==0); reject(&r,&s,out,GIN_CHECKPOINT_UNSAFE);
    assert(unlinkat(dir,"alias",0)==0);
    assert(write(out,"extra\n",6)==6); reject(&r,&s,out,GIN_CHECKPOINT_AMBIGUOUS);
    assert(fstat(out,&after)==0 && before.st_ino==after.st_ino && before.st_dev==after.st_dev && after.st_size==18);
    fd=openat(dir,"out",O_RDONLY|O_NOFOLLOW|O_CLOEXEC); assert(fd>=0);
    assert(read(fd,bytes,sizeof(bytes))==18 && !memcmp(bytes,"prefix\nnext\nextra\n",18)); assert(close(fd)==0);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(unlinkat(wd,"in",0)==0 && unlinkat(wd,"out",0)==0);
    assert(close(wd)==0 && close(win)==0 && unlinkat(dir,"room",AT_REMOVEDIR)==0);
    assert(unlinkat(dir,"in",0)==0 && unlinkat(dir,"out",0)==0);
    assert(close(dir)==0 && close(out)==0 && close(other)==0);
    assert(unlinkat(r.rootfd,component,AT_REMOVEDIR)==0);
    assert(gin_network_encode("Other",component,sizeof(component))==0);
    dir=openat(r.rootfd,component,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC); assert(dir>=0);
    assert(unlinkat(dir,"in",0)==0 && unlinkat(dir,"out",0)==0 && close(dir)==0);
    assert(unlinkat(r.rootfd,component,AT_REMOVEDIR)==0); held(r.lockfd);
    assert(gin_root_release(&r)==0);
    assert(snprintf(path+strlen(path),sizeof(path)-strlen(path),"/.gin.lock")>0 && unlink(path)==0);
    *strrchr(path,'/')='\0'; assert(rmdir(path)==0);
    puts("network STATUS checkpoint guards passed"); return 0;
}
