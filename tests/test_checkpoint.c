#ifdef NDEBUG
#undef NDEBUG
#endif
#include "checkpoint.h"
#include "fs_private.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef GIN_CHECKPOINT_FAULT
static int fault_mode;
static int checkpoint_test_fsync(int fd)
{
    struct stat st;
    if (fstat(fd,&st)==0 && ((fault_mode==1 && S_ISREG(st.st_mode)) ||
        (fault_mode==2 && S_ISDIR(st.st_mode)))) { errno=EIO;return -1; }
    return fsync(fd);
}
#define fsync checkpoint_test_fsync
#include "../src/checkpoint.c"
#undef fsync
#endif

static void append_record(int fd, const char *body)
{
    struct gin_render_record *record=NULL;
    size_t appended=0;
    assert(gin_render_plain_message("privmsg",(const unsigned char *)"Nick",4,
        (const unsigned char *)body,strlen(body),0,GIN_RENDER_MODE_PLAIN,&record)==GIN_RENDER_OK);
    assert(gin_render_append(fd,record,&appended)==GIN_RENDER_OK && appended>0);
    gin_render_record_free(record);
}
static void lock_still_held(const char *binary, const char *root)
{
    pid_t pid=fork(); int status;
    assert(pid>=0);
    if (!pid) { char *const args[]={(char *)binary,"probe",(char *)root,NULL}; execv(binary,args); _exit(127); }
    assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
}
int main(int argc, char **argv)
{
    const char *tmp=getenv("TMPDIR");
    char path[4096], lockpath[4096];
    if (!tmp || !*tmp) tmp=".build";
    struct gin_root root=GIN_ROOT_INITIALIZER;
    struct gin_checkpoint *cp=NULL;
    struct gin_checkpoint_progress progress={999,0};
    struct gin_checkpoint_write_result wrote;
    struct gin_history_scope scope={"https://example.invalid","12345678-1234-1234-1234-123456789abc",
        "NetA","#Room","#&","OwnA",3000000001LL,1,GIN_RENDER_CHANNEL,GIN_RENDER_MODE_PLAIN};
    struct stat original,after; int output,dir,fd;
    if (argc==3 && !strcmp(argv[1],"probe")) {
        struct flock lock;
        assert(snprintf(lockpath,sizeof(lockpath),"%s/.gin.lock",argv[2])>0);
        fd=open(lockpath,O_RDWR|O_NOFOLLOW|O_CLOEXEC); assert(fd>=0);
        memset(&lock,0,sizeof(lock));lock.l_type=F_WRLCK;lock.l_whence=SEEK_SET;
        if (fcntl(fd,F_SETLK,&lock)==-1 && (errno==EACCES || errno==EAGAIN)) { close(fd); return 0; }
        close(fd); return 1;
    }
#ifdef GIN_CHECKPOINT_FAULT
    assert(argc==2 && (!strcmp(argv[1],"pre") || !strcmp(argv[1],"post")));
#else
    assert(argc==1);
#endif
    assert(tmp && snprintf(path,sizeof(path),"%s/gin-checkpoint-XXXXXX",tmp)>0);
    assert(mkdtemp(path));
    assert(gin_root_acquire(path,&root)==0 && gin_root_init(&root,"NetA","#Room","#&")==0);
    output=gin_root_output_open(&root,"NetA","#Room","#&"); assert(output>=0);
    dir=gin_root_window_open(&root,"NetA","#Room","#&"); assert(dir>=0);
    assert(fstat(output,&original)==0);
    assert(gin_checkpoint_open(&root,&scope,root.lockfd,&cp,&progress)==GIN_CHECKPOINT_UNSAFE && !cp);
    lock_still_held(argv[0],path);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNKNOWN);
    assert(cp && progress.anchor==0 && progress.unknown_before==1);
    wrote=gin_checkpoint_commit(cp,7); assert(wrote.code==GIN_CHECKPOINT_INVALID && !wrote.published);
    append_record(output,"unproven live maximum");
    wrote=gin_checkpoint_commit(cp,0);assert(wrote.code==GIN_CHECKPOINT_OK && wrote.published);
    assert(gin_checkpoint_close(&cp)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_OK &&
        progress.anchor==0 && progress.unknown_before==1);
    append_record(output,"first");
    wrote=gin_checkpoint_commit(cp,7); assert(wrote.code==GIN_CHECKPOINT_OK && wrote.published && !wrote.cleanup_failed);
    assert(gin_checkpoint_close(&cp)==0 && cp==NULL);
    scope.socket_epoch=2;scope.own_nick="RenamedSelf";scope.target="#room";
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_OK);
    assert(progress.anchor==7 && progress.unknown_before==1);
    assert(gin_checkpoint_close(&cp)==0);
#ifdef GIN_CHECKPOINT_FAULT
    {
        unsigned char previous[GIN_CHECKPOINT_BYTES_MAX],current[GIN_CHECKPOINT_BYTES_MAX];
        ssize_t before,now;
        assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_OK);
        fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);assert(fd>=0);
        before=read(fd,previous,sizeof(previous));assert(before>0 && close(fd)==0);
        append_record(output,"publication boundary");
        fault_mode=!strcmp(argv[1],"pre")?1:2;
        wrote=gin_checkpoint_commit(cp,8);
        assert(wrote.code==GIN_CHECKPOINT_IO && wrote.published==(fault_mode==2) && !wrote.cleanup_failed);
        assert(gin_checkpoint_close(&cp)==0);
        fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);assert(fd>=0);
        now=read(fd,current,sizeof(current));assert(now==before && close(fd)==0);
        if (fault_mode==1) {
            assert(!memcmp(previous,current,(size_t)before));
            assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_AMBIGUOUS && !cp);
        } else {
            assert(memcmp(previous,current,(size_t)before));
            assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_OK && progress.anchor==8);
            assert(gin_checkpoint_close(&cp)==0);
        }
        fault_mode=0;
        lock_still_held(argv[0],path);
        goto cleanup;
    }
#endif
    scope.network_id++;
    progress.anchor=999;
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_SCOPE && !cp && progress.anchor==999);
    scope.network_id--;
    append_record(output,"not checkpointed");
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_AMBIGUOUS && !cp);
    assert(fstat(output,&after)==0 && after.st_ino==original.st_ino && after.st_size>original.st_size);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(symlinkat("out",dir,GIN_CHECKPOINT_FILENAME)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNSAFE);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(linkat(dir,"out",dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNSAFE);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(linkat(root.rootfd,".gin.lock",dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNSAFE);
    lock_still_held(argv[0],path);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);assert(fd>=0);
    assert(write(fd,"bad",3)==3 && close(fd)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_MALFORMED);
    assert(fchmodat(dir,GIN_CHECKPOINT_FILENAME,0644,0)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNSAFE);
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(gin_checkpoint_open(&root,&scope,output,&cp,&progress)==GIN_CHECKPOINT_UNKNOWN);
    append_record(output,"new known output");
    fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);assert(fd>=0);
    assert(write(fd,"keep",4)==4 && close(fd)==0);
    wrote=gin_checkpoint_commit(cp,8);assert(wrote.code==GIN_CHECKPOINT_UNSAFE && !wrote.published);
    fd=openat(dir,GIN_CHECKPOINT_FILENAME,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);assert(fd>=0);
    { char keep[4];assert(read(fd,keep,4)==4 && !memcmp(keep,"keep",4)); }assert(close(fd)==0);
    assert(gin_checkpoint_close(&cp)==0);
    lock_still_held(argv[0],path);
#ifdef GIN_CHECKPOINT_FAULT
cleanup:
#endif
    assert(unlinkat(dir,GIN_CHECKPOINT_FILENAME,0)==0);
    assert(close(output)==0 && close(dir)==0 && gin_root_release(&root)==0);
    assert(snprintf(lockpath,sizeof(lockpath),"%s/%%4Eet%%41/room",path)>0);
    fd=open(lockpath,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);assert(fd>=0);
    assert(unlinkat(fd,"in",0)==0 && unlinkat(fd,"out",0)==0 && close(fd)==0);
    assert(rmdir(lockpath)==0);
    assert(snprintf(lockpath,sizeof(lockpath),"%s/%%4Eet%%41",path)>0 && rmdir(lockpath)==0);
    assert(snprintf(lockpath,sizeof(lockpath),"%s/.gin.lock",path)>0 && unlink(lockpath)==0 && rmdir(path)==0);
    puts("checkpoint storage guards, progress consistency and lock retention passed");
    return 0;
}
