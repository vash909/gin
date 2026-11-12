
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
existing_endpoint(int parent, const char *name, int fifo, int flags)
{
    struct stat before, opened, after;
    int fd, saved;

    if (fstatat(parent, name, &before, AT_SYMLINK_NOFOLLOW) == -1 ||
        check_endpoint(&before, fifo) == -1)
        return -1;
    fd = openat(parent, name, flags);
    if (fd == -1)
        return -1;
    if (fstat(fd, &opened) == -1 || check_endpoint(&opened, fifo) == -1 ||
        fstatat(parent, name, &after, AT_SYMLINK_NOFOLLOW) == -1 ||
        check_endpoint(&after, fifo) == -1)
        goto fail;
    if (before.st_dev != opened.st_dev || before.st_ino != opened.st_ino ||
        after.st_dev != opened.st_dev || after.st_ino != opened.st_ino) {
        errno = EPERM;
        goto fail;
    }
    return fd;
fail:
    saved = errno;
    (void)close(fd);
    errno = saved;
    return -1;
}

static int
output_file(int parent)
{
    struct stat opened;
    int fd, saved;
    const int flags = O_WRONLY | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;

    fd = openat(parent, "out", flags | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno == EEXIST)
            return existing_endpoint(parent, "out", 0, flags);
        return -1;
    }
    if (fstat(fd, &opened) == -1 || check_endpoint(&opened, 0) == -1) {
        saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

/* Never replace, truncate or remove this inode: its persistence is intentional. */
static int
root_lock(int rootfd)
{
    struct stat before, after, path;
    struct flock lock;
    int fd, saved;
    const int flags = O_RDWR | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC;

    fd = openat(rootfd, ".gin.lock", flags | O_CREAT | O_EXCL, 0600);
    if (fd == -1) {
        if (errno != EEXIST ||
            fstatat(rootfd, ".gin.lock", &before, AT_SYMLINK_NOFOLLOW) == -1 ||
            check_endpoint(&before, 0) == -1)
            return -1;
        fd = openat(rootfd, ".gin.lock", flags);
        if (fd == -1)
            return -1;
        if (fstat(fd, &after) == -1 || check_endpoint(&after, 0) == -1)
            goto fail;
        if (before.st_dev != after.st_dev || before.st_ino != after.st_ino) {
            errno = EPERM;
            goto fail;
        }
    }
    if (fstat(fd, &after) == -1 || check_endpoint(&after, 0) == -1 ||
        fstatat(rootfd, ".gin.lock", &path, AT_SYMLINK_NOFOLLOW) == -1 ||
        check_endpoint(&path, 0) == -1)
        goto fail;
    if (path.st_dev != after.st_dev || path.st_ino != after.st_ino) {
        errno = EPERM;
        goto fail;
    }
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    if (fcntl(fd, F_SETLK, &lock) == -1)
        goto fail;
    /* Reject replacement or metadata changes across acquisition as well. */
    if (fstat(fd, &after) == -1 || check_endpoint(&after, 0) == -1 ||
        fstatat(rootfd, ".gin.lock", &path, AT_SYMLINK_NOFOLLOW) == -1 ||
        check_endpoint(&path, 0) == -1)
        goto fail;
    if (path.st_dev != after.st_dev || path.st_ino != after.st_ino) {
        errno = EPERM;
        goto fail;
    }
    return fd;
fail:
    saved = errno;
    (void)close(fd);
    errno = saved;
    return -1;
}

int
gin_root_release(struct gin_root *root)
{
    int result = 0, saved = 0;

    if (root->lockfd != -1 && close(root->lockfd) == -1) {
        result = -1;
        saved = errno;
    }
    if (root->rootfd != -1 && close(root->rootfd) == -1 && result == 0) {
        result = -1;
        saved = errno;
    }
    root->rootfd = root->lockfd = -1;
    if (result == -1)
        errno = saved;
    return result;
}

int
gin_root_acquire(const char *path, struct gin_root *root)
{
    struct gin_root opened = GIN_ROOT_INITIALIZER;
    mode_t oldmask;
    int saved, result = -1;

    if (root->rootfd != -1 || root->lockfd != -1) {
        errno = EINVAL;
        return -1;
    }
    /* Reuse the lexical root policy without probing or mutating children. */
    if (gin_validate(path, "net", "#window", "#") == -1)
        return -1;
    oldmask = umask(077);
    opened.rootfd = root_dir(path);
    if (opened.rootfd != -1 && check_dir(opened.rootfd) == 0) {
        opened.lockfd = root_lock(opened.rootfd);
        if (opened.lockfd != -1) {
            *root = opened;
            result = 0;
        }
    }
    saved = errno;
    if (result == -1)
        (void)gin_root_release(&opened);
    (void)umask(oldmask);
    errno = saved;
    return result;
}

int
gin_root_init(const struct gin_root *root, const char *network,
              const char *target, const char *chantypes)
{
    int netfd = -1, winfd = -1, outfd = -1;
    int result = -1, saved;
    mode_t oldmask;
    struct stat input;
    struct gin_name name;

    if (root->rootfd == -1 || root->lockfd == -1) {
        errno = EINVAL;
        return -1;
    }
    if (gin_name_prepare(network, target, chantypes, &name) == -1)
        return -1;
    oldmask = umask(077);
    netfd = private_dir(root->rootfd, name.network_component);
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
    (void)umask(oldmask);
    errno = saved;
    return result;
}

/* Read-only traversal: unlike private_dir(), never creates a missing entry. */
static int
input_dir(int parent, const char *component)
{
    int fd = openat(parent, component,
                    O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd != -1 && check_dir(fd) == -1) {
        int saved = errno;
        (void)close(fd);
        errno = saved;
        return -1;
    }
    return fd;
}

int
gin_root_input_open(const struct gin_root *root, const char *network,
                    const char *target, const char *chantypes)
{
    struct gin_name name;
    int netfd = -1, winfd = -1, fd = -1, result = -1, saved;

    if (root->rootfd == -1 || root->lockfd == -1) {
        errno = EINVAL;
        return -1;
    }
    if (gin_name_prepare(network, target, chantypes, &name) == -1 ||
        check_dir(root->rootfd) == -1)
        return -1;
    netfd = input_dir(root->rootfd, name.network_component);
    if (netfd == -1)
        goto done;
    winfd = input_dir(netfd, name.window_component);
    if (winfd == -1)
        goto done;
    fd = existing_endpoint(winfd, "in", 1,
                           O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (fd == -1)
        goto done;
    result = fd;
    fd = -1;
done:
    saved = errno;
    if (fd != -1)
        (void)close(fd);
    if (winfd != -1)
        (void)close(winfd);
    if (netfd != -1)
        (void)close(netfd);
    errno = saved;
    return result;
}

int
gin_root_output_open(const struct gin_root *root, const char *network,
                     const char *target, const char *chantypes)
{
    struct gin_name name;
    int netfd = -1, winfd = -1, fd = -1, saved;

    if (root == NULL || network == NULL || target == NULL || chantypes == NULL ||
        root->rootfd == -1 || root->lockfd == -1) {
        errno = EINVAL;
        return -1;
    }
    if (gin_name_prepare(network, target, chantypes, &name) == -1 ||
        check_dir(root->rootfd) == -1)
        return -1;
    netfd = input_dir(root->rootfd, name.network_component);
    if (netfd == -1)
        goto done;
    winfd = input_dir(netfd, name.window_component);
    if (winfd == -1)
        goto done;
    fd = existing_endpoint(winfd, "out", 0,
        O_WRONLY | O_APPEND | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
done:
    saved = errno;
    if (winfd != -1)
        (void)close(winfd);
    if (netfd != -1)
        (void)close(netfd);
    errno = saved;
    return fd;
}

int
gin_init(const char *path, const char *network, const char *target,
         const char *chantypes)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    int result, saved;

    /* Invalid effective settings must not even create the root or lock. */
    if (gin_validate(path, network, target, chantypes) == -1 ||
        gin_root_acquire(path, &root) == -1)
        return -1;
    result = gin_root_init(&root, network, target, chantypes);
    saved = errno;
    /* All projection descriptors were closed before releasing the writer. */
    if (gin_root_release(&root) == -1 && result == 0)
        return -1;
    errno = saved;
    return result;
}
