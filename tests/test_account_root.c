#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_fs.h"
#ifndef ROOT_BRIDGE
#include "account_root.h"
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef ROOT_BRIDGE
static void lease(const struct gin_root *r, int held)
{
    pid_t p = fork();
    int status;
    assert(p >= 0);
    if (p == 0) {
        struct flock l;
        int fd;
        int rc;
        alarm(3);
        fd = openat(r->rootfd, ".gin.lock", O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        assert(fd >= 0);
        memset(&l, 0, sizeof(l)); l.l_type = F_WRLCK; l.l_whence = SEEK_SET;
        rc = fcntl(fd, F_SETLK, &l);
        _exit(held ? !(rc == -1 && (errno == EACCES || errno == EAGAIN)) : rc != 0);
    }
    assert(waitpid(p, &status, 0) == p);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
#endif
int main(void)
{
    char path[4096];
    const char *tmp = getenv("TMPDIR");
    struct gin_root r = GIN_ROOT_INITIALIZER;
    struct stat st;
    alarm(10);
    if (tmp == NULL || *tmp == 0) tmp = ".build";
    assert(snprintf(path, sizeof(path), "%s/account-root-XXXXXX", tmp) > 0);
    assert(mkdtemp(path) != NULL);
    assert(gin_root_acquire(path, &r) == 0);
#ifdef ROOT_BRIDGE
    /* Native root setup succeeds; the new account identity is the feature gap. */
    if (fstatat(r.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) != 0) {
        assert(errno == ENOENT);
        puts("RED: native held root has no account identity metadata");
        assert(gin_root_release(&r) == 0);
        { char lock[4200]; (void)snprintf(lock, sizeof(lock), "%s/.gin.lock", path);
          assert(unlink(lock) == 0); assert(rmdir(path) == 0); }
        return 1;
    }
    return 0;
#else
    {
        struct gin_credential c;
        struct gin_account_root_result result;
        unsigned char first[8193], second[8193];
        ssize_t n, m;
        ino_t inode;
        int fd;
        memset(&c, 0, sizeof(c));
        strcpy(c.origin, "https://fixture.invalid");
        strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
        strcpy(c.token, "synthetic-Bearer-Only");
        strcpy(c.subject_name, "SyntheticDisplayOnly");
        lease(&r, 1);
        result = gin_account_root_bind(&r, &c, "#&");
        assert(result.code == GIN_ACCOUNT_ROOT_OK && result.created);
        fd = openat(r.rootfd, ".gin.account", O_RDONLY | O_NOFOLLOW);
        assert(fd >= 0 && fstat(fd, &st) == 0); inode = st.st_ino;
        n = read(fd, first, sizeof(first) - 1); assert(n > 0); first[n] = 0;
        assert(close(fd) == 0);
        /* Search length-aware: binary records include zero length-prefix bytes. */
        { ssize_t i; for (i = 0; i < n; ++i) {
            assert((size_t)(n-i) < strlen(c.token) || memcmp(first+i, c.token, strlen(c.token)) != 0);
            assert((size_t)(n-i) < strlen(c.subject_name) || memcmp(first+i, c.subject_name, strlen(c.subject_name)) != 0);
        } }
        strcpy(c.token, "rotated-Token"); strcpy(c.subject_name, "RotatedDisplay");
        result = gin_account_root_bind(&r, &c, "#&");
        assert(result.code == GIN_ACCOUNT_ROOT_OK && !result.created);
        strcpy(c.origin, "https://other.invalid");
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_MISMATCH);
        strcpy(c.origin, "https://fixture.invalid"); c.subject_id[0] = 'a';
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_MISMATCH);
        c.subject_id[0] = '1';
        assert(gin_account_root_bind(&r, &c, "#").code == GIN_ACCOUNT_ROOT_MISMATCH);
        fd = openat(r.rootfd, ".gin.account", O_RDONLY | O_NOFOLLOW);
        assert(fd >= 0 && fstat(fd, &st) == 0 && st.st_ino == inode);
        m = read(fd, second, sizeof(second)); assert(m == n && memcmp(first, second, (size_t)n) == 0);
        assert(close(fd) == 0);
        /* Reopen with a fresh native lease, preserving exact identity bytes. */
        assert(gin_root_release(&r) == 0);
        assert(gin_root_acquire(path, &r) == 0);
        result = gin_account_root_bind(&r, &c, "#&");
        assert(result.code == GIN_ACCOUNT_ROOT_OK && !result.created);
        lease(&r, 1);
        fd = openat(r.rootfd, ".gin.account", O_WRONLY | O_APPEND | O_NOFOLLOW);
        assert(fd >= 0 && write(fd, "x", 1) == 1 && close(fd) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_MISMATCH);
        assert(fstatat(r.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) == 0);
        assert(st.st_ino == inode && st.st_size == n + 1);
        fd = openat(r.rootfd, ".gin.account", O_WRONLY | O_NOFOLLOW);
        assert(fd >= 0 && ftruncate(fd, 8193) == 0 && close(fd) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_MISMATCH);
        assert(fstatat(r.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) == 0);
        assert(st.st_ino == inode && st.st_size == 8193);
        fd = openat(r.rootfd, ".gin.account", O_WRONLY | O_NOFOLLOW);
        assert(fd >= 0 && ftruncate(fd, 0) == 0 && close(fd) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_MISMATCH);
        assert(fchmodat(r.rootfd, ".gin.account", 0644, 0) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_UNSAFE);
        assert(unlinkat(r.rootfd, ".gin.account", 0) == 0);
        assert(mkfifoat(r.rootfd, ".gin.account", 0600) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_UNSAFE);
        assert(unlinkat(r.rootfd, ".gin.account", 0) == 0);
        fd = openat(r.rootfd, ".gin.credentials", O_WRONLY | O_CREAT | O_EXCL, 0600);
        assert(fd >= 0 && close(fd) == 0);
        result = gin_account_root_bind(&r, &c, "#&");
        assert(result.code == GIN_ACCOUNT_ROOT_UNBOUND_NONEMPTY && !result.created);
        assert(fstatat(r.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
        assert(unlinkat(r.rootfd, ".gin.credentials", 0) == 0);
        assert(fchmod(r.rootfd, 0755) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_UNSAFE);
        assert(fchmod(r.rootfd, 0700) == 0);
        assert(fchmod(r.lockfd, 0644) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_UNSAFE);
        assert(fchmod(r.lockfd, 0600) == 0);
        assert(linkat(r.rootfd, ".gin.lock", r.rootfd, ".gin.account", 0) == 0);
        assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_UNSAFE);
        lease(&r, 1);
        assert(unlinkat(r.rootfd, ".gin.account", 0) == 0);
        lease(&r, 1);
        /* Independent kernel control: release of managed lease permits ownership. */
        { struct flock l; memset(&l, 0, sizeof(l)); l.l_type = F_UNLCK; l.l_whence = SEEK_SET;
          assert(fcntl(r.lockfd, F_SETLK, &l) == 0); lease(&r, 0); }
    }
    {
        char lock[sizeof(path) + sizeof("/.gin.lock")];
        int used = snprintf(lock, sizeof(lock), "%s/.gin.lock", path);
        assert(used > 0 && (size_t)used < sizeof(lock));
        assert(gin_root_release(&r) == 0);
        assert(unlink(lock) == 0); /* Only this fixture's released root is removed. */
    }
    assert(rmdir(path) == 0);
    puts("account root controls passed");
    return 0;
#endif
}
