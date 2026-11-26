#define _POSIX_C_SOURCE 200809L
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "account_root.h"
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

static int fail_write, fail_sync, writes;
static ssize_t injected_write(int fd, const void *bytes, size_t count)
{
    if (fail_write) {
        if (writes++ == 0) return write(fd, bytes, count > 1 ? count / 2 : count);
        errno = EIO;
        return -1;
    }
    return write(fd, bytes, count);
}
static int injected_fsync(int fd)
{
    if (fail_sync) { errno = EIO; return -1; }
    return fsync(fd);
}
#define write injected_write
#define fsync injected_fsync
#include "../src/account_root.c"
#undef write
#undef fsync

static void probe_held(int fd)
{
    pid_t child = fork(); int status;
    assert(child >= 0);
    if (child == 0) {
        struct flock lock; int rc, saved;
        alarm(2); memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
        rc = fcntl(fd, F_SETLK, &lock); saved = errno;
        _exit(rc == -1 && (saved == EAGAIN || saved == EACCES) ? 0 : 1);
    }
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static size_t read_record(const struct gin_root *root, unsigned char *bytes,
                          size_t capacity, struct stat *state)
{
    ssize_t count; int fd = openat(root->rootfd, GIN_ACCOUNT_ROOT_FILENAME,
        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    assert(fd >= 0 && fstat(fd, state) == 0);
    assert(S_ISREG(state->st_mode) && (state->st_mode & 07777) == 0600 && state->st_nlink == 1);
    count = read(fd, bytes, capacity); assert(count > 0);
    assert(close(fd) == 0);
    return (size_t)count;
}
static void run_case(int partial)
{
    const char *scratch = getenv("TMPDIR");
    char path[4096], lock_path[4200]; int count;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_credential credential;
    struct gin_account_root_result result;
    struct stat before, after;
    unsigned char first[GIN_ACCOUNT_ROOT_MAX + 1], second[GIN_ACCOUNT_ROOT_MAX + 1];
    size_t a, b;
    if (!scratch || !*scratch) scratch = ".build";
    count = snprintf(path, sizeof(path), "%s/account-root-fault-XXXXXX", scratch);
    assert(count > 0 && (size_t)count < sizeof(path) && mkdtemp(path));
    memset(&credential, 0, sizeof(credential));
    strcpy(credential.origin, "https://fixture.invalid");
    strcpy(credential.subject_id, "12345678-1234-1234-1234-123456789abc");
    strcpy(credential.token, "synthetic-fault-bearer");
    strcpy(credential.subject_name, "SyntheticFaultDisplay");
    assert(gin_root_acquire(path, &root) == 0); probe_held(root.lockfd);
    writes = 0; fail_write = partial; fail_sync = !partial;
    result = gin_account_root_bind(&root, &credential, "#&");
    fail_write = 0; fail_sync = 0;
    assert(result.code == GIN_ACCOUNT_ROOT_IO && result.created);
    a = read_record(&root, first, sizeof(first), &before); probe_held(root.lockfd);
    /* Explicit test recheck, not application login/retry behavior. A partial
     * record must remain rejected, while complete matching bytes may be read. */
    result = gin_account_root_bind(&root, &credential, "#&");
    assert(result.code == (partial ? GIN_ACCOUNT_ROOT_MISMATCH : GIN_ACCOUNT_ROOT_OK));
    assert(!result.created);
    b = read_record(&root, second, sizeof(second), &after);
    assert(before.st_dev == after.st_dev && before.st_ino == after.st_ino);
    assert(before.st_size == after.st_size && a == b && memcmp(first, second, a) == 0);
    probe_held(root.lockfd);
    assert(unlinkat(root.rootfd, GIN_ACCOUNT_ROOT_FILENAME, 0) == 0);
    count = snprintf(lock_path, sizeof(lock_path), "%s/.gin.lock", path);
    assert(count > 0 && (size_t)count < sizeof(lock_path));
    assert(gin_root_release(&root) == 0);
    assert(unlink(lock_path) == 0 && rmdir(path) == 0);
}
int main(void)
{
    alarm(10);
    run_case(1); run_case(0);
    puts("account root partial-write and fsync failure controls passed");
    return 0;
}
