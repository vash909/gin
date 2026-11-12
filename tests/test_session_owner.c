#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_session.h"
#include "gin_fs.h"
#include "gin_credential.h"
#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
static void scratch_directory(char *path, size_t capacity, const char *name)
{
    const char *tmp = getenv("TMPDIR");
    int n = snprintf(path, capacity, "%s/%s-XXXXXX", tmp && *tmp ? tmp : ".build", name);
    assert(n > 0 && (size_t)n < capacity);
    assert(mkdtemp(path) != NULL);
}
static int descriptor_count(void)
{
    int count = 0, i;
    for (i = 0; i < 256; ++i) if (fcntl(i, F_GETFD) >= 0) ++count;
    return count;
}
static void prior_handler(int number) { (void)number; }
#ifdef OWNER_WHITEBOX
#include "../src/session.c"
static void pipe_capabilities(void)
{
    char base[4096], path[4096], byte;
    int fds[3], i, writing, before = descriptor_count();
    scratch_directory(base, sizeof(base), "gin-session-capabilities");
    assert(snprintf(path, sizeof(path), "%s/fifo", base) > 0);
    assert(mkfifo(path, 0600) == 0);
    fds[0] = open(path, O_RDONLY | O_NONBLOCK);
    assert(fds[0] >= 0);
    fds[1] = open(path, O_WRONLY | O_NONBLOCK);
    assert(fds[1] >= 0);
    /* Real O_RDWR FIFO exercises access capability, not native pipe EOF or
     * peer-close semantics. The ordinary pipe EOF test below remains native. */
    fds[2] = open(path, O_RDWR | O_NONBLOCK);
    assert(fds[2] >= 0);
    for (i = 0; i < 3; ++i) {
        int flags = fcntl(fds[i], F_GETFL), fdflags = fcntl(fds[i], F_GETFD);
        assert((flags & O_ACCMODE) == (i == 0 ? O_RDONLY : i == 1 ? O_WRONLY : O_RDWR));
        for (writing = 0; writing < 2; ++writing) {
            int borrowed = borrow_pipe(fds[i], writing);
            if ((i == 0 && writing) || (i == 1 && !writing)) {
                assert(borrowed < 0);
            } else {
                assert(borrowed >= 3 && borrowed != fds[i]);
                assert(fcntl(borrowed, F_GETFD) & FD_CLOEXEC);
                if (writing) {
                    struct owner o;
                    char record[6];
                    owner_init(&o); o.status = borrowed;
                    assert(publish_ready(&o) == GIN_SESSION_OK);
                    assert(read(fds[0], record, sizeof(record)) == 6);
                    assert(!memcmp(record, "READY\n", 6));
                } else {
                    assert(write(fds[1], "x", 1) == 1);
                    assert(read(borrowed, &byte, 1) == 1 && byte == 'x');
                }
                assert(close(borrowed) == 0);
            }
            assert(fcntl(fds[i], F_GETFL) == flags);
            assert(fcntl(fds[i], F_GETFD) == fdflags);
        }
        assert(fcntl(fds[i], F_SETFL, flags & ~O_NONBLOCK) == 0);
        assert(borrow_pipe(fds[i], 0) < 0 && borrow_pipe(fds[i], 1) < 0);
        assert(fcntl(fds[i], F_GETFL) == (flags & ~O_NONBLOCK));
        assert(fcntl(fds[i], F_GETFD) == fdflags);
        assert(fcntl(fds[i], F_SETFL, flags) == 0);
    }
    for (i = 0; i < 3; ++i) assert(close(fds[i]) == 0);
    assert(unlink(path) == 0);
    i = open(path, O_CREAT | O_EXCL | O_RDWR | O_NONBLOCK, 0600);
    assert(i >= 0);
    assert(borrow_pipe(i, 0) < 0 && borrow_pipe(i, 1) < 0);
    assert(fcntl(i, F_GETFL) >= 0 && close(i) == 0);
    assert(unlink(path) == 0 && rmdir(base) == 0);
    assert(descriptor_count() == before);
}
static void whitebox(void)
{
    pipe_capabilities();
    static const char *const payloads[] = {
        "{\"kind\":\"query_windows_list\",\"windows\":{}}",
        "{\"windows\":{},\"k\\u0069nd\":\"query_windows_\\u006cist\"}",
        "{\"nested\":{\"kind\":\"query_windows_list\"}}",
        "{\"kind\":false}", "{\"kind\":\"session_state\"}",
        "{\"other\":\"kind\",\"windows\":{}}"
    };
    size_t i;
    struct owner o;
    enum gin_session_code code;
    int fds[2], flags;
    for (i=0;i<sizeof(payloads)/sizeof(payloads[0]);++i)
        assert(query_payload((const unsigned char *)payloads[i],strlen(payloads[i])) == (i<2));
    {
        struct metadata m;
        unsigned char *large=calloc(1,600000);
        assert(large);
        memset(&m,0,sizeof(m));
        for (i=0;i<9;++i) metadata_copy(&m,(const unsigned char *)"x",1,1);
        assert(m.count==8 && m.count_overflow && !m.bytes_overflow);
        metadata_clear(&m);
        memset(&m,0,sizeof(m));
        metadata_copy(&m,large,600000,1); metadata_copy(&m,large,600000,1);
        assert(m.count==1 && !m.count_overflow && m.bytes_overflow);
        metadata_clear(&m); free(large);
    }
    assert(pipe(fds)==0);
    flags=fcntl(fds[1],F_GETFL); assert(flags>=0);
    assert(fcntl(fds[1],F_SETFL,flags|O_NONBLOCK)==0);
    owner_init(&o); o.status=borrow_pipe(fds[1],1); assert(o.status>=0);
    assert(fcntl(fds[1],F_GETFL)==(flags|O_NONBLOCK));
    assert(publish_ready(&o)==GIN_SESSION_OK);
    { char record[6]; assert(read(fds[0],record,6)==6); assert(!memcmp(record,"READY\n",6)); }
    { char buffer[4096]={0}; while(write(fds[1],buffer,sizeof(buffer))>0) {} }
    assert(publish_ready(&o)==GIN_SESSION_STATUS);
    assert(close(o.status)==0); o.status=-1;
    assert(close(fds[1])==0); assert(close(fds[0])==0);
    {
        char base[4096];
        char lockpath[4096], alias[4096];
        struct gin_root held=GIN_ROOT_INITIALIZER;
        int status;
        pid_t pid;
        scratch_directory(base, sizeof(base), "gin-session-lock");
        assert(gin_root_acquire(base,&held)==0);
        assert(snprintf(lockpath,sizeof(lockpath),"%s/.gin.lock",base)>0);
        assert(snprintf(alias,sizeof(alias),"%s/ca-alias",base)>0);
        assert(link(lockpath,alias)==0);
        assert(!ca_safe(alias,&held));
        pid=fork(); assert(pid>=0);
        if (!pid) {
            struct flock lock;
            int fd=open(lockpath,O_RDWR);
            memset(&lock,0,sizeof(lock)); lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
            int result=fd>=0 && fcntl(fd,F_SETLK,&lock)<0 && (errno==EACCES || errno==EAGAIN)?0:1;
            if (fd>=0) (void)close(fd);
            /* Close inherited fixture descriptors without using a live root
             * handle across fork; the parent's process lock stays independent. */
            (void)close(held.lockfd);
            (void)close(held.rootfd);
            exit(result);
        }
        assert(waitpid(pid,&status,0)==pid && WIFEXITED(status) && WEXITSTATUS(status)==0);
        assert(gin_root_release(&held)==0);
        assert(unlink(alias)==0);
        assert(gin_root_acquire(base,&held)==0); assert(gin_root_release(&held)==0);
    }
    owner_init(&o); code=owner_begin(&o); assert(code==GIN_SESSION_OK);
    { struct owner second; owner_init(&second); assert(owner_begin(&second)==GIN_SESSION_BUSY); }
    errno=EDOM; stop_signal(SIGTERM); assert(errno==EDOM && session_stopping);
    assert(owner_end(&o,GIN_SESSION_OK)==GIN_SESSION_OK);
}
#endif
int main(void)
{
#ifndef OWNER_IMPORT_ONLY
    assert(gin_session_run(NULL, -1, 0) == GIN_SESSION_INVALID);
#endif
    assert(gin_session_import(NULL, NULL, NULL, NULL) == GIN_SESSION_INVALID);
    assert(gin_session_error(GIN_SESSION_OK) != NULL);
#ifndef OWNER_IMPORT_ONLY
    {
        char base[4096];
        char root[4096];
        struct gin_session_settings s;
        struct stat st;
        scratch_directory(base, sizeof(base), "gin-session-owner");
        assert(snprintf(root, sizeof(root), "%s/root", base) > 0);
        memset(&s, 0, sizeof(s));
        strcpy(s.local.root, root);
        strcpy(s.local.network, "net"); strcpy(s.local.target, "#one");
        strcpy(s.local.chantypes, "#");
        strcpy(s.origin, "https://127.0.0.1:1"); strcpy(s.mode, "plain");
        s.connect_ms=100; s.total_ms=200; s.join_ms=200; s.startup_ms=1000;
        /* A valid lexical configuration reaches real held-root store loading. */
        assert(gin_session_run(&s, -1, 0) == GIN_SESSION_CREDENTIAL);
        assert(stat(root, &st) == 0 && S_ISDIR(st.st_mode));
        assert(gin_session_run(&s, 0, 0) == GIN_SESSION_STATUS);
        {
            int saved_stdin=dup(0), fds[2], flags;
            assert(saved_stdin>=0 && pipe(fds)==0);
            flags=fcntl(fds[0],F_GETFL); assert(flags>=0);
            assert(fcntl(fds[0],F_SETFL,flags|O_NONBLOCK)==0);
            assert(dup2(fds[0],0)==0); assert(close(fds[0])==0);
            assert(write(fds[1],"x",1)==1);
            assert(gin_session_run(&s,-1,1)==GIN_SESSION_INPUT);
            /* INPUT must mean consumed data, not descriptor validation failure. */
            { char byte; assert(read(0,&byte,1)<0 && (errno==EAGAIN || errno==EWOULDBLOCK)); }
            assert(close(fds[1])==0);
            assert(gin_session_run(&s,-1,1)==GIN_SESSION_OK);
            assert(fcntl(0,F_GETFL)==(flags|O_NONBLOCK));
            assert(dup2(saved_stdin,0)==0); assert(close(saved_stdin)==0);
        }

        {
            struct gin_root held = GIN_ROOT_INITIALIZER;
            struct gin_credential value;
            memset(&value, 0, sizeof(value));
            strcpy(value.origin, s.origin); strcpy(value.token, "synthetic~");
            strcpy(value.subject_id, "12345678-1234-1234-1234-123456789abc");
            strcpy(value.subject_name, "Alice");
            assert(gin_root_acquire(root, &held) == 0);
            assert(gin_credential_save(&held, &value) == GIN_CREDENTIAL_OK);
            gin_credential_clear(&value); assert(gin_root_release(&held) == 0);
        }
        assert(gin_session_run(&s, -1, 0) == GIN_SESSION_TOPOLOGY);

    }
#endif
    {
        char base[4096];
        char root[4096], file[4096];
        int fd;
        struct stat st;
        scratch_directory(base, sizeof(base), "gin-session-import");
        assert(snprintf(root, sizeof(root), "%s/root", base) > 0);
        assert(snprintf(file, sizeof(file), "%s/bearer", base) > 0);
        assert(gin_session_import(root, "https://127.0.0.1:1", NULL, file) == GIN_SESSION_IO);
        fd = open(file, O_WRONLY|O_CREAT|O_EXCL, 0600); assert(fd >= 0);
        assert(write(fd, "bad\n", 4) == 4); assert(close(fd) == 0);
        assert(gin_session_import(root, "https://127.0.0.1:1", NULL, file) == GIN_SESSION_CREDENTIAL);
        assert(stat(root, &st) < 0);
        assert(chmod(file,0640)==0);
        assert(gin_session_import(root,"https://127.0.0.1:1",NULL,file)==GIN_SESSION_IO);
        assert(chmod(file,0600)==0);
        fd = open(file, O_WRONLY|O_TRUNC); assert(fd >= 0);
        assert(write(fd, "synthetic~", 10) == 10); assert(close(fd) == 0);
        assert(gin_session_import(root, "https://127.0.0.1:1", NULL, file) == GIN_SESSION_PROTOCOL);
        assert(stat(root, &st) == 0);
        {
            sigset_t block, prior, pending;
            int number;
            sigemptyset(&block); sigaddset(&block,SIGPIPE);
            assert(sigprocmask(SIG_BLOCK,&block,&prior)==0);
            assert(raise(SIGPIPE)==0);
            assert(gin_session_import(root,"https://127.0.0.1:1",NULL,file)==GIN_SESSION_BUSY);
            assert(sigpending(&pending)==0 && sigismember(&pending,SIGPIPE)==1);
            assert(sigwait(&block,&number)==0 && number==SIGPIPE);
            assert(sigprocmask(SIG_SETMASK,&prior,NULL)==0);
        }

        {
            struct sigaction old[3], custom, after;
            sigset_t before_mask, after_mask;
            struct rlimit saved, low;
            int signals[3] = {SIGINT, SIGTERM, SIGPIPE}, i, n = descriptor_count();
            memset(&custom, 0, sizeof(custom)); custom.sa_handler = prior_handler;
            custom.sa_flags = SA_RESTART; sigemptyset(&custom.sa_mask);
            sigaddset(&custom.sa_mask, SIGUSR1);
            assert(sigprocmask(SIG_SETMASK, NULL, &before_mask) == 0);
            for (i=0; i<3; ++i) assert(sigaction(signals[i], &custom, &old[i]) == 0);
            assert(getrlimit(RLIMIT_NOFILE, &saved) == 0);
            for (i=3; i<12; ++i) {
                enum gin_session_code result;
                low=saved; low.rlim_cur=(rlim_t)i;
                assert(setrlimit(RLIMIT_NOFILE, &low) == 0);
                result=gin_session_import(root, "https://127.0.0.1:1", NULL, file);
                assert(result == GIN_SESSION_IO || result == GIN_SESSION_PROTOCOL);
                assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
                assert(descriptor_count() == n);
            }
            for (i=0; i<3; ++i) {
                assert(sigaction(signals[i], NULL, &after) == 0);
                assert(after.sa_handler == custom.sa_handler && (after.sa_flags & SA_RESTART));
                assert(sigismember(&after.sa_mask, SIGUSR1) == 1);
                assert(sigaction(signals[i], &old[i], NULL) == 0);
            }
            assert(sigprocmask(SIG_SETMASK, NULL, &after_mask) == 0);
            for (i=1; i<32; ++i)
                assert(sigismember(&after_mask,i) == sigismember(&before_mask,i));
        }

        { struct gin_root held = GIN_ROOT_INITIALIZER;
          assert(gin_root_acquire(root, &held) == 0);
          assert(gin_root_release(&held) == 0); }

    }
#ifdef OWNER_WHITEBOX
    whitebox();
#endif
    puts("Session owner tests passed.");
    return 0;
}
