#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fs_private.h"
#include "account_naming.h"
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

static int active, close_fault, injected, rootfd, lockfd;
static int fault_at, calls, endpoint_fd = -1, quarantine_fd = -1, schedule;
static int trigger(void)
{
    if (active && ++calls == fault_at) { injected = 1; errno = ENOSPC; return 1; }
    return 0;
}
static int hot_openat(int dir, const char *name, int flags, ...)
{
    mode_t mode = 0; int fd;
    if (flags & O_CREAT) {
        va_list ap; va_start(ap, flags); mode = (mode_t)va_arg(ap, int); va_end(ap);
    }
    if (active) {
        assert((flags & O_NOFOLLOW) && (flags & O_NONBLOCK) && (flags & O_CLOEXEC));
        assert(strcmp(name, ".gin.lock") != 0);
        if (trigger()) return -1;
    }
    if (active && schedule == 5 && !strcmp(name, "net")) {
        assert(renameat(dir, name, dir, "saved-net") == 0);
        assert(mkdirat(dir, name, 0700) == 0);
        fd = openat(dir, name, flags, mode); assert(fd >= 0);
        assert(unlinkat(dir, name, AT_REMOVEDIR) == 0);
        assert(renameat(dir, "saved-net", dir, name) == 0);
        return fd;
    }
    if (active && schedule >= 1 && schedule <= 3 && !strcmp(name, "out")) {
        assert(renameat(dir, "out", dir, "saved") == 0);
        if (schedule == 1 || schedule == 3) {
            assert(renameat(rootfd, ".gin.lock", dir, "out") == 0);
        } else {
            int other = openat(dir, "out", O_WRONLY | O_CREAT | O_EXCL, 0600);
            assert(other >= 0 && close(other) == 0);
        }
        fd = openat(dir, name, flags, mode); assert(fd >= 0);
        if (schedule == 1 || schedule == 3)
            assert(renameat(dir, "out", rootfd, ".gin.lock") == 0);
        else assert(unlinkat(dir, "out", 0) == 0);
        assert(renameat(dir, "saved", dir, "out") == 0);
    } else fd = openat(dir, name, flags, mode);
    if (active && fd >= 0 && !(flags & O_DIRECTORY)) endpoint_fd = fd;
    return fd;
}
static int hot_fstat(int fd, struct stat *s)
{
    int unproven = active && fd == endpoint_fd &&
        (schedule == 4 || (schedule == 3 && fstat(fd, s) == 0 && S_ISREG(s->st_mode)));
    if (active && (trigger() || unproven)) {
        if (fd == endpoint_fd) quarantine_fd = fd;
        errno = ENOSPC; return -1;
    }
    return fstat(fd, s);
}
static int hot_fstatat(int dir, const char *name, struct stat *s, int flags)
{
    if (trigger()) return -1;
    return fstatat(dir, name, s, flags);
}
static int hot_mkdirat(int dir, const char *name, mode_t mode)
{
    if (trigger()) return -1;
    return mkdirat(dir, name, mode);
}
static int hot_mkfifoat(int dir, const char *name, mode_t mode)
{
    if (trigger()) return -1;
    return mkfifoat(dir, name, mode);
}
static int hot_close(int fd)
{
    struct stat st;
    int fail = active && close_fault && !injected &&
        fstat(fd, &st) == 0 && S_ISDIR(st.st_mode);
    int rc;
    assert(!active || (fd != rootfd && fd != lockfd));
    rc = close(fd);
    if (trigger()) return -1;
    if (fail) { injected = 1; errno = EIO; return -1; }
    return rc;
}
#define openat hot_openat
#define fstat hot_fstat
#define fstatat hot_fstatat
#define mkdirat hot_mkdirat
#define mkfifoat hot_mkfifoat
#define close hot_close
#include "../src/fs.c"
#undef close
#undef openat
#undef fstat
#undef fstatat
#undef mkdirat
#undef mkfifoat

static void lease(int fd)
{
    pid_t p = fork(); int status;
    assert(p >= 0);
    if (!p) {
        struct flock l; int rc, e;
        alarm(2); memset(&l, 0, sizeof(l));
        l.l_type = F_WRLCK; l.l_whence = SEEK_SET;
        rc = fcntl(fd, F_SETLK, &l); e = errno;
        _exit(rc == -1 && (e == EACCES || e == EAGAIN) ? 0 : 1);
    }
    assert(waitpid(p, &status, 0) == p && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static void remove_fixture(const char *path, struct gin_root *r)
{
    int n = openat(r->rootfd, "net", O_RDONLY | O_DIRECTORY);
    int w;
    assert(n >= 0);
    w = openat(n, "@peer", O_RDONLY | O_DIRECTORY); assert(w >= 0);
    assert(unlinkat(w, "in", 0) == 0 && unlinkat(w, "out", 0) == 0);
    assert(close(w) == 0 && unlinkat(n, "@peer", AT_REMOVEDIR) == 0);
    assert(unlinkat(n, "in", 0) == 0 && unlinkat(n, "out", 0) == 0);
    assert(close(n) == 0 && unlinkat(r->rootfd, "net", AT_REMOVEDIR) == 0);
    assert(gin_root_release(r) == 0);
    { char p[4200]; assert(snprintf(p, sizeof(p), "%s/.gin.lock", path) > 0);
      assert(unlink(p) == 0 && rmdir(path) == 0); }
}

#ifndef PAIR_BASELINE
static int scenario(int kind, int nth)
{
    struct gin_root r = GIN_ROOT_INITIALIZER;
    struct gin_fs_pair pair = { -1, -1 };
    struct gin_fs_pair_result result;
    struct stat old, after;
    const char *tmp = getenv("TMPDIR");
    char path[4096], bytes[16];
    int n, w, oldfd, total, saved0 = -1, held_in;
    if (!tmp || !*tmp) tmp = ".build";
    assert(snprintf(path, sizeof(path), "%s/gin-pair-matrix-XXXXXX", tmp) > 0);
    assert(mkdtemp(path) && gin_root_acquire(path, &r) == 0);
    assert(gin_root_network_init(&r, "net") == 0);
    assert(gin_root_init_named(&r, "net", "peer", "#&", gin_account_name_prepare) == 0);
    n = openat(r.rootfd, "net", O_RDONLY | O_DIRECTORY); assert(n >= 0);
    w = openat(n, "@peer", O_RDONLY | O_DIRECTORY); assert(w >= 0);
    oldfd = openat(w, "out", O_RDWR | O_APPEND); assert(oldfd >= 0);
    held_in = openat(w, "in", O_RDONLY | O_NONBLOCK); assert(held_in >= 0);
    assert(write(oldfd, "old bytes\n", 10) == 10 && fstat(oldfd, &old) == 0);
    if (kind == 1) { assert(unlinkat(w, "in", 0) == 0); }
    if (kind == 2) { assert(unlinkat(w, "out", 0) == 0); }
    if (kind == 3) {
        assert(unlinkat(w, "in", 0) == 0 && unlinkat(w, "out", 0) == 0);
        assert(close(w) == 0 && unlinkat(n, "@peer", AT_REMOVEDIR) == 0); w = -1;
    }
    if (kind == 5 || kind == 18) assert(fchmod(oldfd, 0644) == 0);
    if (kind == 6) assert(linkat(w, "out", w, "alias", 0) == 0);
    if (kind == 7 || kind == 8) {
        assert(unlinkat(w, "in", 0) == 0);
        if (kind == 7) assert(symlinkat("out", w, "in") == 0);
        else { int fd = openat(w, "in", O_WRONLY | O_CREAT | O_EXCL, 0600); assert(fd >= 0 && close(fd) == 0); }
    }
    if (kind == 9) { saved0 = dup(0); assert(close(0) == 0); }
    if (kind == 14) assert(fchmod(w, 0755) == 0);
    if (kind == 15 && geteuid() == 0) assert(fchown(oldfd, 1, (gid_t)-1) == 0);
    close_fault = kind == 13 || kind == 18; injected = calls = 0; fault_at = nth;
    schedule = kind >= 10 && kind <= 12 ? kind - 9 : (kind == 16 ? 4 : 0);
    if (kind == 19) schedule = 5;
    endpoint_fd = quarantine_fd = -1; rootfd = r.rootfd; lockfd = r.lockfd;
    lease(r.lockfd); active = 1;
    result = gin_root_pair_prepare_named(&r, kind == 4 ? "absent" : "net",
        kind == 17 ? "../escape" : "peer", "#&", gin_account_name_prepare, &pair);
    active = 0; total = calls;
    if (nth) {
        assert(injected && result.error == ENOSPC);
    } else if (kind == 4 || kind == 17) {
        assert(result.error == (kind == 4 ? ENOENT : EINVAL));
        assert(!result.mutation_attempted && !result.cleanup_failed);
        assert(fstatat(r.rootfd, "absent", &after, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    } else if (kind == 10 || kind == 12 || kind == 16) {
        assert(result.error == (kind == 10 ? EPERM : ENOSPC));
        assert(result.cleanup_failed);
        if (kind == 10) quarantine_fd = endpoint_fd;
        assert(quarantine_fd >= 0 && fcntl(quarantine_fd, F_GETFD) >= 0);
    } else if (kind == 11 || (kind >= 5 && kind <= 8) || kind == 14 || (kind == 15 && geteuid() == 0)) {
        assert(result.error == EPERM && !result.cleanup_failed);
    } else if (kind == 18 || kind == 19) {
        assert(result.error == EPERM && result.cleanup_failed == (kind == 18));
        assert(result.mutation_attempted == (kind == 18));
    } else if (kind == 13) {
        assert(result.error == EIO && result.cleanup_failed && injected);
    } else {
        int fl;
        assert(!result.error && !result.cleanup_failed && result.mutation_attempted);
        assert(pair.input >= 0 && pair.output >= 0);
        fl = fcntl(pair.input, F_GETFL);
        assert((fl & O_ACCMODE) == O_RDONLY && (fl & O_NONBLOCK));
        fl = fcntl(pair.output, F_GETFL);
        assert((fl & O_ACCMODE) == O_WRONLY && (fl & O_APPEND) && (fl & O_NONBLOCK));
        assert((fcntl(pair.input, F_GETFD) & FD_CLOEXEC) && (fcntl(pair.output, F_GETFD) & FD_CLOEXEC));
        if (kind == 9) assert(fcntl(0, F_GETFD) == -1 && errno == EBADF);
        assert(close(pair.input) == 0 && close(pair.output) == 0);
    }
    if (result.error) assert(pair.input == -1 && pair.output == -1);
    lease(r.lockfd);
    assert(fstat(oldfd, &after) == 0 && old.st_ino == after.st_ino && after.st_size == 10);
    assert(pread(oldfd, bytes, sizeof(bytes), 0) == 10 && !memcmp(bytes, "old bytes\n", 10));
    assert(fcntl(held_in, F_GETFD) >= 0);
    if (kind == 5 || kind == 18) assert(fchmod(oldfd, 0600) == 0);
    if (kind == 14) assert(fchmod(w, 0700) == 0);
    if (kind == 15 && geteuid() == 0) assert(fchown(oldfd, 0, (gid_t)-1) == 0);
    if (kind == 6) assert(unlinkat(w, "alias", 0) == 0);
    if (w >= 0) assert(close(w) == 0);
    assert(close(n) == 0 && close(oldfd) == 0 && close(held_in) == 0);
    /* Faults before publication intentionally leave any already-created paths.
     * Fixture cleanup is diagnostic, after borrowed lease release. */
    assert(gin_root_release(&r) == 0);
    if (quarantine_fd >= 0 && fcntl(quarantine_fd, F_GETFD) >= 0)
        assert(close(quarantine_fd) == 0);
    if (saved0 >= 0) { assert(dup2(saved0, 0) == 0 && close(saved0) == 0); }
    n = open(path, O_RDONLY | O_DIRECTORY); assert(n >= 0);
    w = openat(n, "net", O_RDONLY | O_DIRECTORY); assert(w >= 0);
    { int win = openat(w, "@peer", O_RDONLY | O_DIRECTORY);
      if (win >= 0) {
          if (unlinkat(win, "in", 0) == -1) assert(errno == ENOENT);
          if (unlinkat(win, "out", 0) == -1) assert(errno == ENOENT);
          assert(close(win) == 0 && unlinkat(w, "@peer", AT_REMOVEDIR) == 0);
      } else assert(errno == ENOENT);
    }
    assert(unlinkat(w, "in", 0) == 0 && unlinkat(w, "out", 0) == 0);
    assert(close(w) == 0 && unlinkat(n, "net", AT_REMOVEDIR) == 0);
    assert(unlinkat(n, ".gin.lock", 0) == 0 && close(n) == 0 && rmdir(path) == 0);
    printf("scenario=%d fault_at=%d calls=%d error=%d mutation=%d cleanup=%d; lease/old-bytes/FD/fixture controls complete\n",
        kind, nth, total, result.error, result.mutation_attempted, result.cleanup_failed);
    return total;
}
static void matrix(void)
{
    int i, total;
    close_fault = 0;
    total = scenario(0, 0);
    for (i = 1; i <= total; ++i) scenario(0, i);
    total = scenario(3, 0);
    for (i = 1; i <= total; ++i) scenario(3, i);
    for (i = 1; i <= 19; ++i) scenario(i, 0);
}
#endif

int main(void)
{
    struct gin_root r = GIN_ROOT_INITIALIZER;
    const char *tmp = getenv("TMPDIR");
    char path[4096];
    int observed;
    alarm(15);
    if (!tmp || !*tmp) tmp = ".build";
    assert(snprintf(path, sizeof(path), "%s/gin-pair-XXXXXX", tmp) > 0);
    assert(mkdtemp(path) != NULL && gin_root_acquire(path, &r) == 0);
    assert(gin_root_network_init(&r, "net") == 0); lease(r.lockfd);
    rootfd = r.rootfd; lockfd = r.lockfd;
    close_fault = 1; active = 1;
#ifdef PAIR_BASELINE
    observed = gin_root_init_named(&r, "net", "peer", "#&", gin_account_name_prepare);
#else
    {
        struct gin_fs_pair pair = { -1, -1 };
        struct gin_fs_pair_result result = gin_root_pair_prepare_named(&r,
            "net", "peer", "#&", gin_account_name_prepare, &pair);
        observed = result.error ? -1 : 0;
        assert(result.error == EIO && result.cleanup_failed && result.mutation_attempted);
        assert(pair.input == -1 && pair.output == -1);
    }
#endif
    active = 0; assert(injected); lease(r.lockfd);
    remove_fixture(path, &r);
    printf("healthy existing-network setup; injected real-close-then-EIO=%d; observed=%d; orderly cleanup and lease controls complete\n", injected, observed);
    #ifndef PAIR_BASELINE
    matrix();
    #endif
    if (observed != -1) {
        puts("MISSING CHECKED COMPOSITION CONTRACT: legacy init discards nested directory cleanup error (not a legacy API defect)");
        return 1;
    }
    return 0;
}
