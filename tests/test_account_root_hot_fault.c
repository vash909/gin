#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "account_root.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int active, opens, closes, reads, fault, stats, fdstats, metadata_fd = -1;
static mode_t fifo_mode;
static int hot_openat(int dir, const char *name, int flags, ...)
{
    assert(!active || (strcmp(name, GIN_ACCOUNT_ROOT_FILENAME) == 0 &&
        !(flags & (O_CREAT | O_TRUNC | O_WRONLY | O_RDWR)) &&
        (flags & O_NOFOLLOW) && (flags & O_NONBLOCK) && (flags & O_CLOEXEC)));
    if (active) {
        ++opens;
        if (fault == 14 || fault == 16) {
            assert(renameat(dir, name, dir, ".gin.saved") == 0);
            assert(renameat(dir, ".gin.lock", dir, name) == 0);
        }
        metadata_fd = openat(dir, name, flags);
        if (fault == 14 || fault == 16) {
            assert(metadata_fd >= 0);
            assert(renameat(dir, name, dir, ".gin.lock") == 0);
            assert(renameat(dir, ".gin.saved", dir, name) == 0);
        }
        return metadata_fd;
    } else {
        mode_t mode = 0;
        if (flags & O_CREAT) {
            va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
        }
        return openat(dir, name, flags, mode);
    }
}
static ssize_t hot_read(int fd, void *out, size_t n)
{
    if (active) {
        ++reads; assert(fd == metadata_fd && reads < GIN_ACCOUNT_ROOT_MAX + 130);
        if (fault == 1 || (fault == 2 && reads == 1)) { errno = EINTR; return -1; }
        if (fault == 3) { errno = EIO; return -1; }
        if (fault == 4 && n > 1) n = 1;
    }
    return read(fd, out, n);
}
static int hot_close(int fd)
{
    int rc;
    if (!active) return close(fd);
    assert(fd == metadata_fd); ++closes; assert(closes == 1);
    rc = close(fd);
    if (fault == 5 || fault == 6) { errno = EINTR; return -1; }
    return rc;
}
static int hot_statat(int dir, const char *name, struct stat *s, int flags)
{
    int rc = fstatat(dir, name, s, flags);
    if (active && strcmp(name, GIN_ACCOUNT_ROOT_FILENAME) == 0) {
        ++stats;
        if (rc == 0 && ((fault == 7 && stats == 2) || (fault == 8 && stats == 3))) ++s->st_ino;
    }
    return rc;
}
static int hot_stat(int fd, struct stat *s)
{
    if (active && fd == metadata_fd) {
        ++fdstats;
        if (fault == 15 || fault == 16) { errno = EIO; return -1; }
    }
    int rc = fstat(fd, s);
    if (active && fd == metadata_fd && rc == 0) {
        if (fault == 9) ++s->st_ino;
        if (fault == 10) { assert(S_ISFIFO(fifo_mode)); s->st_mode = fifo_mode; }
        if (fault == 11) {
            assert(S_ISREG(s->st_mode));
            s->st_mode = (s->st_mode & ~(mode_t)07777) | 0644;
        }
        if (fault == 12) s->st_nlink = 2;
        if (fault == 13) ++s->st_uid;
    }
    return rc;
}
#define openat hot_openat
#define read hot_read
#define close hot_close
#define fstatat hot_statat
#define fstat hot_stat
#include "../src/account_root.c"
#undef openat
#undef read
#undef close
#undef fstatat
#undef fstat

static void lease(int fd)
{
    pid_t p = fork(); int status;
    assert(p >= 0);
    if (p == 0) {
        struct flock l; int rc, saved;
        alarm(2); memset(&l, 0, sizeof(l)); l.l_type = F_WRLCK; l.l_whence = SEEK_SET;
        rc = fcntl(fd, F_SETLK, &l); saved = errno;
        _exit(rc == -1 && (saved == EAGAIN || saved == EACCES) ? 0 : 1);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void check(const struct gin_root *r, const char *o, const char *id,
    const char *types, enum gin_account_root_code want, int cleanup, int inject)
{
    struct gin_account_root_check_result result;
    opens = closes = reads = stats = fdstats = 0; metadata_fd = -1; fault = inject; active = 1;
    result = gin_account_root_check(r, o, id, types);
    active = 0;
    assert(result.code == want && result.cleanup_failed == !!cleanup);
    assert(opens <= 1 && closes == (metadata_fd >= 0));
    if (r != NULL && r->lockfd >= 0) lease(r->lockfd);
}
static int quarantine(int inject)
{
    const char *tmp = getenv("TMPDIR");
    char path[4096];
    struct gin_root r = GIN_ROOT_INITIALIZER;
    struct gin_credential c = { "https://fixture.invalid", "Synthetic-only",
        "12345678-1234-1234-1234-123456789ABC", "Fixture" };
    struct gin_account_root_check_result result;
    struct stat before, after, opened, lock;
    pid_t p; int status, held, retained, good, dir;
    if (tmp == NULL || !*tmp) tmp = ".build";
    assert(snprintf(path, sizeof(path), "%s/gin-quarantine-XXXXXX", tmp) > 0);
    assert(mkdtemp(path) != NULL && gin_root_acquire(path, &r) == 0);
    assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_OK);
    lease(r.lockfd);
    assert(fstatat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, &before, AT_SYMLINK_NOFOLLOW) == 0);
    opens = closes = reads = stats = fdstats = 0; metadata_fd = -1;
    fault = inject; active = 1;
    result = gin_account_root_check(&r, c.origin, c.subject_id, "#&");
    active = 0;
    assert(opens == 1 && metadata_fd >= 0);
    assert(fstatat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, &after, AT_SYMLINK_NOFOLLOW) == 0);
    assert(same(&before, &after) && before.st_size == after.st_size);
    retained = fstat(metadata_fd, &opened) == 0;
    assert(fstat(r.lockfd, &lock) == 0);
    if (retained) assert(same(&opened, &lock) == (inject != 15));
    p = fork(); assert(p >= 0);
    if (p == 0) {
        struct flock l; int rc, saved;
        alarm(2); memset(&l, 0, sizeof(l)); l.l_type = F_WRLCK; l.l_whence = SEEK_SET;
        rc = fcntl(r.lockfd, F_SETLK, &l); saved = errno;
        _exit(rc == -1 && (saved == EAGAIN || saved == EACCES) ? 0 : 1);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status));
    held = WEXITSTATUS(status) == 0;
    good = result.code == (inject == 14 ? GIN_ACCOUNT_ROOT_UNSAFE : GIN_ACCOUNT_ROOT_IO) &&
        result.cleanup_failed && closes == 0 && reads == 0 && fdstats == 1 && retained && held;
    /* Diagnostic ownership only: never close a possible alias while root is live. */
    assert(gin_root_release(&r) == 0);
    if (retained) assert(close(metadata_fd) == 0);
    dir = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); assert(dir >= 0);
    assert(unlinkat(dir, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    assert(unlinkat(dir, ".gin.lock", 0) == 0);
    assert(close(dir) == 0 && rmdir(path) == 0);
    printf("quarantine=%d code=%d cleanup=%d closes=%d fdstats=%d retained=%d held=%d cleanup complete\n",
        inject, result.code, result.cleanup_failed, closes, fdstats, retained, held);
    return good;
}
int main(void)
{
    const char *tmp = getenv("TMPDIR");
    const char *origin = "https://fixture.invalid", *id = "12345678-1234-1234-1234-123456789ABC";
    char path[4096], lockpath[4200], long_origin[1026];
    struct gin_root r = GIN_ROOT_INITIALIZER, wrong;
    struct gin_credential c;
    unsigned char bytes[GIN_ACCOUNT_ROOT_MAX + 1];
    struct stat before, after;
    int fd, saved, i, type_pipe[2]; ssize_t n;
    alarm(15);
    /* Derive the synthetic FIFO type from a real pipe, not optional S_IF* names. */
    assert(pipe(type_pipe) == 0);
    assert(fstat(type_pipe[0], &after) == 0 && S_ISFIFO(after.st_mode));
    fifo_mode = (after.st_mode & ~(mode_t)07777) | 0600;
    assert(close(type_pipe[0]) == 0 && close(type_pipe[1]) == 0);
    if (getenv("ROOT_QUARANTINE_CASE") != NULL)
        return quarantine(atoi(getenv("ROOT_QUARANTINE_CASE"))) ? 0 : 1;
    assert(quarantine(14));
    assert(quarantine(15));
    assert(quarantine(16));
    if (tmp == NULL || !*tmp) tmp = ".build";
    assert(snprintf(path, sizeof(path), "%s/gin-hot-root-XXXXXX", tmp) > 0);
    assert(mkdtemp(path) != NULL && gin_root_acquire(path, &r) == 0);
    memset(&c, 0, sizeof(c)); strcpy(c.origin, origin); strcpy(c.subject_id, id);
    strcpy(c.token, "Synthetic-only-fixture"); strcpy(c.subject_name, "NotTheUUID");
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_IO, 0, 0);
    assert(fstatat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, &after, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    assert(gin_account_root_bind(&r, &c, "#&").code == GIN_ACCOUNT_ROOT_OK);
    assert(fstatat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, &before, AT_SYMLINK_NOFOLLOW) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_OK, 0, 0);
    check(&r, "https://wrong.invalid", id, "#&", GIN_ACCOUNT_ROOT_MISMATCH, 0, 0);
    check(&r, origin, "12345678-1234-1234-1234-123456789abc", "#&", GIN_ACCOUNT_ROOT_MISMATCH, 0, 0);
    check(&r, origin, id, "#", GIN_ACCOUNT_ROOT_MISMATCH, 0, 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_OK, 0, 2);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_OK, 0, 4);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_IO, 0, 1);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_IO, 0, 3);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_OK, 1, 5);
    check(&r, "https://wrong.invalid", id, "#&", GIN_ACCOUNT_ROOT_MISMATCH, 1, 6);
    for (i = 7; i <= 13; ++i) check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_UNSAFE, 0, i);
    check(NULL, origin, id, "#&", GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    check(&r, NULL, id, "#&", GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    check(&r, origin, NULL, "#&", GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    check(&r, origin, id, NULL, GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    check(&r, origin, "invalid", "#&", GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    memset(long_origin, 'x', sizeof(long_origin)); memcpy(long_origin, "https://", 8); long_origin[1025] = 0;
    check(&r, long_origin, id, "#&", GIN_ACCOUNT_ROOT_INVALID, 0, 0);
    saved = dup(0); assert(close(0) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_OK, 0, 0);
    assert(metadata_fd == 0 && fcntl(0, F_GETFD) == -1);
    if (saved >= 0) { assert(dup2(saved, 0) == 0); assert(close(saved) == 0); }
    fd = openat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, O_RDONLY); assert(fd >= 0);
    n = read(fd, bytes, sizeof(bytes)); assert(n > 0 && close(fd) == 0);
    assert(fstatat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, &after, AT_SYMLINK_NOFOLLOW) == 0);
    assert(before.st_ino == after.st_ino && before.st_size == after.st_size);
    fd = openat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, O_WRONLY | O_APPEND); assert(fd >= 0);
    assert(write(fd, "x", 1) == 1 && close(fd) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_MISMATCH, 0, 0);
    fd = openat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, O_WRONLY); assert(fd >= 0);
    assert(ftruncate(fd, GIN_ACCOUNT_ROOT_MAX + 1) == 0 && close(fd) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_MISMATCH, 0, 0);
    assert(unlinkat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    assert(symlinkat(".gin.lock", r.rootfd, GIN_ACCOUNT_ROOT_FILENAME) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_UNSAFE, 0, 0);
    assert(opens == 0 && unlinkat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    assert(linkat(r.rootfd, ".gin.lock", r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_UNSAFE, 0, 0);
    assert(opens == 0 && unlinkat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    assert(mkfifoat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0600) == 0);
    check(&r, origin, id, "#&", GIN_ACCOUNT_ROOT_UNSAFE, 0, 0);
    assert(opens == 0 && unlinkat(r.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    wrong = r; wrong.rootfd = r.lockfd;
    check(&wrong, origin, id, "#&", GIN_ACCOUNT_ROOT_UNSAFE, 0, 0);
    assert(snprintf(lockpath, sizeof(lockpath), "%s/.gin.lock", path) > 0);
    assert(gin_root_release(&r) == 0 && unlink(lockpath) == 0 && rmdir(path) == 0);
    puts("existing-only root, native identity, guard, read, close, fd0 and lease controls passed");
    return 0;
}
