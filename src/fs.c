/* Darwin hides O_NOFOLLOW with _POSIX_C_SOURCE unless this is enabled. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include "gin_fs.h"
#include "gin_naming.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int
check_dir(int fd)
{
    struct stat st;

    if (fstat(fd, &st) == -1)
        return -1;
    if (!S_ISDIR(st.st_mode) || (st.st_mode & 07777) != 0700 ||
        st.st_uid != geteuid()) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int
private_dir(int parent, const char *name)
{
    int fd;

    if (mkdirat(parent, name, 0700) == -1 && errno != EEXIST)
        return -1;
    fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd != -1 && check_dir(fd) == -1) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/* Shared lexical contract; deliberately does not probe the filesystem. */
int
gin_validate(const char *root, const char *network, const char *target,
             const char *chantypes)
{
    const unsigned char *p = (const unsigned char *)root;
    const char *scan = root;
    struct gin_name name;

    if (*root == '\0' || strlen(root) > 4095 || strlen(target) > 256)
        goto invalid;
    for (; *p != '\0'; ++p) {
        if (*p < 32 || *p == 127)
            goto invalid;
    }
    while (*scan != '\0') {
        size_t length;
        scan += strspn(scan, "/");
        length = strcspn(scan, "/");
        if (length == 2 && scan[0] == '.' && scan[1] == '.')
            goto invalid;
        scan += length;
    }
    return gin_name_prepare(network, target, chantypes, &name);
invalid:
    errno = EINVAL;
    return -1;
}

/* Resolve each component separately: O_NOFOLLOW on a full path is insufficient. */
static int
root_dir(const char *root)
{
    char *copy, *part, *next, *state = NULL;
    int fd, saved;
    copy = strdup(root);
    if (copy == NULL)
        return -1;
    fd = open(root[0] == '/' ? "/" : ".",
              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd == -1) {
        free(copy);
        return -1;
    }
    part = strtok_r(copy, "/", &state);
    while (part != NULL) {
        int child;
        next = strtok_r(NULL, "/", &state);
        child = next == NULL ? private_dir(fd, part) :
            openat(fd, part, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        saved = errno;
        (void)close(fd);
        fd = child;
        if (fd == -1) {
            errno = saved;
            break;
        }
        part = next;
    }
    saved = errno;
    free(copy);
    errno = saved;
    return fd;
}

static int
check_endpoint(const struct stat *st, int fifo)
{
    if ((fifo ? !S_ISFIFO(st->st_mode) : !S_ISREG(st->st_mode)) ||
        (st->st_mode & 07777) != 0600 || st->st_nlink != 1 ||
        st->st_uid != geteuid()) {
        errno = EPERM;
        return -1;
    }
    return 0;
}

static int
output_file(int parent)
{
    struct stat before, after;
    int fd;
    const int flags = O_WRONLY | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;

    fd = openat(parent, "out", flags | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno != EEXIST ||
            fstatat(parent, "out", &before, AT_SYMLINK_NOFOLLOW) == -1 ||
            check_endpoint(&before, 0) == -1)
            return -1;
        fd = openat(parent, "out", flags);
        if (fd == -1)
            return -1;
        if (fstat(fd, &after) == -1 || check_endpoint(&after, 0) == -1)
            goto fail;
        if (before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
            errno = EPERM;
            goto fail;
        }
    } else if (fstat(fd, &after) == -1 || check_endpoint(&after, 0) == -1) {
        goto fail;
    }
    return fd;
fail:
    {
        int saved = errno;
        (void)close(fd);
        errno = saved;
    }
    return -1;
}

int
gin_init(const char *root, const char *network, const char *target,
         const char *chantypes)
{
    int rootfd = -1, netfd = -1, winfd = -1, outfd = -1;
    int result = -1, saved;
    mode_t oldmask;
    struct stat input;
    struct gin_name name;

    if (gin_validate(root, network, target, chantypes) == -1 ||
        gin_name_prepare(network, target, chantypes, &name) == -1)
        return -1;
    oldmask = umask(077);

    rootfd = root_dir(root);
    if (rootfd == -1 || check_dir(rootfd) == -1)
        goto done;
    netfd = private_dir(rootfd, name.network_component);
    if (netfd == -1)
        goto done;
    winfd = private_dir(netfd, name.window_component);
    if (winfd == -1)
        goto done;
    if (mkfifoat(winfd, "in", 0600) == -1 && errno != EEXIST)
        goto done;
    if (fstatat(winfd, "in", &input, AT_SYMLINK_NOFOLLOW) == -1 ||
        check_endpoint(&input, 1) == -1)
        goto done;
    outfd = output_file(winfd);
    if (outfd == -1)
        goto done;
    result = 0;

done:
    saved = errno;
    if (outfd != -1 && close(outfd) == -1 && result == 0) {
        saved = errno;
        result = -1;
    }
    if (winfd != -1)
        (void)close(winfd);
    if (netfd != -1)
        (void)close(netfd);
    if (rootfd != -1)
        (void)close(rootfd);
    (void)umask(oldmask);
    errno = saved;
    return result;
}
