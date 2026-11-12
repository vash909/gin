/* Expose POSIX.1-2008 realpath in this test translation unit. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_fs.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void still_locked(const char *path)
{
    int status; pid_t child = fork(); assert(child >= 0);
    if (child == 0) {
        struct gin_root other = GIN_ROOT_INITIALIZER;
        int rc = gin_root_acquire(path, &other);
        _exit(rc == -1 && (errno == EACCES || errno == EAGAIN) ? 0 : 1);
    }
    assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
static int output(const struct gin_root *root)
{
    return gin_root_output_open(root, "test", "#Room", "#");
}
static void make_out(int directory)
{
    int fd = openat(directory, "out", O_WRONLY | O_CREAT | O_EXCL, 0600);
    assert(fd >= 0 && close(fd) == 0);
}
int main(void)
{
    const char *base = getenv("TMPDIR");
    char *resolved;
    char temp[4096], rootpath[4096], contents[16];
    struct gin_root root = GIN_ROOT_INITIALIZER, empty = GIN_ROOT_INITIALIZER;
    struct stat first, second;
    int network, window, fd, flags, n;
    if (base == NULL) base = ".build";
    n = snprintf(temp, sizeof(temp), "%s/gin-output.XXXXXX", base);
    assert(n > 0 && (size_t)n < sizeof(temp) && mkdtemp(temp) != NULL);
    /* Normal fixture paths may inherit a symlinked TMPDIR.
     * Resolve only our private temporary directory, not hostile test inputs. */
    resolved = realpath(temp, NULL);
    assert(resolved != NULL);
    n = snprintf(rootpath, sizeof(rootpath), "%s/root", resolved);
    free(resolved);
    assert(n > 0 && (size_t)n < sizeof(rootpath));
    assert(gin_root_acquire(rootpath, &root) == 0);
    assert(gin_root_init(&root, "test", "#Room", "#") == 0);
    network = openat(root.rootfd, "test", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(network >= 0);
    window = openat(network, "room", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    assert(window >= 0 && fstatat(window, "out", &first, AT_SYMLINK_NOFOLLOW) == 0);
    fd = output(&root); assert(fd >= 0);
    flags = fcntl(fd, F_GETFL);
    assert(flags != -1 && (flags & O_ACCMODE) == O_WRONLY && (flags & O_APPEND) && (flags & O_NONBLOCK));
    assert((fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    assert(write(fd, "first", 5) == 5 && close(fd) == 0);
    still_locked(rootpath);
    fd = output(&root); assert(fd >= 0 && write(fd, "+next", 5) == 5 && close(fd) == 0);
    fd = openat(window, "out", O_RDONLY | O_NOFOLLOW); assert(fd >= 0);
    assert(read(fd, contents, sizeof(contents)) == 10 && !memcmp(contents, "first+next", 10));
    assert(fstat(fd, &second) == 0 && first.st_dev == second.st_dev && first.st_ino == second.st_ino);
    assert(close(fd) == 0);
    assert(gin_root_output_open(&empty, "test", "#Room", "#") == -1 && errno == EINVAL);
    assert(gin_root_output_open(NULL, "test", "#Room", "#") == -1 && errno == EINVAL);
    assert(gin_root_output_open(&root, "../bad", "#Room", "#") == -1);
    assert(gin_root_output_open(&root, "missing", "#Room", "#") == -1);
    assert(fstatat(root.rootfd, "missing", &second, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    assert(fchmodat(window, "out", 0644, 0) == 0 && output(&root) == -1);
    assert(fstatat(window, "out", &second, AT_SYMLINK_NOFOLLOW) == 0 && (second.st_mode & 07777) == 0644);
    assert(fchmodat(window, "out", 0600, 0) == 0);
    assert(linkat(window, "out", window, "alias", 0) == 0 && output(&root) == -1);
    assert(unlinkat(window, "alias", 0) == 0);
    if (geteuid() == 0) {
        assert(fchownat(window, "out", (uid_t)1, (gid_t)-1, 0) == 0 && output(&root) == -1);
        assert(fchownat(window, "out", geteuid(), (gid_t)-1, 0) == 0);
    } else puts("Output foreign-owner control skipped outside isolated root lane");
    assert(renameat(window, "out", window, "saved") == 0);
    assert(output(&root) == -1 && errno == ENOENT);
    assert(fstatat(window, "out", &second, AT_SYMLINK_NOFOLLOW) == -1 && errno == ENOENT);
    assert(symlinkat("saved", window, "out") == 0 && output(&root) == -1);
    assert(unlinkat(window, "out", 0) == 0);
    assert(mkfifoat(window, "out", 0600) == 0 && output(&root) == -1);
    assert(unlinkat(window, "out", 0) == 0);
    assert(mkdirat(window, "out", 0700) == 0 && output(&root) == -1);
    assert(unlinkat(window, "out", AT_REMOVEDIR) == 0);
    make_out(window);
    assert(fchmod(window, 0755) == 0 && output(&root) == -1 && fchmod(window, 0700) == 0);
    assert(fchmod(network, 0755) == 0 && output(&root) == -1 && fchmod(network, 0700) == 0);
    fd = output(&root); assert(fd >= 0 && close(fd) == 0);
    still_locked(rootpath);
    assert(unlinkat(window, "out", 0) == 0 && unlinkat(window, "saved", 0) == 0 && unlinkat(window, "in", 0) == 0);
    assert(close(window) == 0 && unlinkat(network, "room", AT_REMOVEDIR) == 0);
    assert(close(network) == 0 && unlinkat(root.rootfd, "test", AT_REMOVEDIR) == 0);
    assert(gin_root_release(&root) == 0);
    n = snprintf(contents, sizeof(contents), "%s", ".gin.lock"); assert(n == 9);
    fd = open(rootpath, O_RDONLY | O_DIRECTORY | O_NOFOLLOW); assert(fd >= 0);
    assert(unlinkat(fd, contents, 0) == 0 && close(fd) == 0);
    assert(rmdir(rootpath) == 0 && rmdir(temp) == 0);
    puts("Held-root output reuse, append flags, hostile endpoints and lock retention passed");
    return 0;
}
