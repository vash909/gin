#include "account_root.h"
#include "gin_naming.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int same(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}
static int private_file(const struct stat *s)
{
    return S_ISREG(s->st_mode) && (s->st_mode & 07777) == 0600 &&
        s->st_uid == geteuid() && s->st_nlink == 1;
}
static enum gin_account_root_code held(const struct gin_root *r, struct stat *lock)
{
    struct stat dir, path;
    if (r == NULL || r->rootfd < 0 || r->lockfd < 0)
        return GIN_ACCOUNT_ROOT_INVALID;
    if (fstat(r->rootfd, &dir) != 0 || fstat(r->lockfd, lock) != 0)
        return GIN_ACCOUNT_ROOT_IO;
    if (!S_ISDIR(dir.st_mode) || (dir.st_mode & 07777) != 0700 ||
        dir.st_uid != geteuid() || !private_file(lock)) return GIN_ACCOUNT_ROOT_UNSAFE;
    if (fstatat(r->rootfd, ".gin.lock", &path, AT_SYMLINK_NOFOLLOW) != 0)
        return GIN_ACCOUNT_ROOT_IO;
    return private_file(&path) && same(lock, &path) ?
        GIN_ACCOUNT_ROOT_OK : GIN_ACCOUNT_ROOT_UNSAFE;
}
static enum gin_account_root_code entry(const struct gin_root *r,
    const struct stat *lock, struct stat *s)
{
    if (fstatat(r->rootfd, GIN_ACCOUNT_ROOT_FILENAME, s, AT_SYMLINK_NOFOLLOW) != 0)
        return GIN_ACCOUNT_ROOT_IO;
    return !same(s, lock) && private_file(s) ?
        GIN_ACCOUNT_ROOT_OK : GIN_ACCOUNT_ROOT_UNSAFE;
}
/* Separate open file description: never consume caller's directory offset.
 * Only the directory itself is opened, never any child or lock alias. */
static enum gin_account_root_code empty_root(const struct gin_root *r)
{
    int fd = openat(r->rootfd, ".", O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    DIR *d;
    struct dirent *e;
    enum gin_account_root_code code = GIN_ACCOUNT_ROOT_OK;
    if (fd < 0) return GIN_ACCOUNT_ROOT_IO;
    d = fdopendir(fd);
    if (d == NULL) { (void)close(fd); return GIN_ACCOUNT_ROOT_IO; }
    errno = 0;
    while ((e = readdir(d)) != NULL) {
        if (strcmp(e->d_name, ".") != 0 && strcmp(e->d_name, "..") != 0 &&
            strcmp(e->d_name, ".gin.lock") != 0) {
            code = GIN_ACCOUNT_ROOT_UNBOUND_NONEMPTY;
            break;
        }
        errno = 0;
    }
    if (e == NULL && errno != 0) code = GIN_ACCOUNT_ROOT_IO;
    if (closedir(d) != 0) code = GIN_ACCOUNT_ROOT_IO;
    return code;
}
/* Version 1 account-layout record: eight fixed magic/version octets, then
 * three uint16 big-endian byte lengths followed by exact non-NUL field bytes.
 * The version identifies this first account layout as well as the encoding. */
static size_t record(unsigned char *out, const struct gin_credential *c, const char *types)
{
    static const unsigned char magic[8] = { 'G', 'I', 'N', 'A', 'C', 'C', 0, 1 };
    const char *fields[3];
    size_t used = sizeof(magic), i;
    fields[0] = c->origin; fields[1] = c->subject_id; fields[2] = types;
    memcpy(out, magic, sizeof(magic));
    for (i = 0; i < 3; ++i) {
        size_t n = strlen(fields[i]);
        if (n > 65535 || n + 2 > GIN_ACCOUNT_ROOT_MAX - used) return 0;
        out[used++] = (unsigned char)(n >> 8);
        out[used++] = (unsigned char)n;
        memcpy(out + used, fields[i], n); used += n;
    }
    return used;
}
struct gin_account_root_result gin_account_root_bind(const struct gin_root *r,
    const struct gin_credential *c, const char *types)
{
    struct gin_account_root_result result = { GIN_ACCOUNT_ROOT_INVALID, false };
    struct stat lock, before, opened, after;
    unsigned char expected[GIN_ACCOUNT_ROOT_MAX], actual[GIN_ACCOUNT_ROOT_MAX + 1];
    size_t n, used = 0;
    unsigned int attempts = 0;
    int fd = -1;
    mode_t mask;
    /* Every entry checks the borrowed root/lock; no lock pathname is opened. */
    result.code = held(r, &lock);
    if (result.code != GIN_ACCOUNT_ROOT_OK) return result;
    result.code = GIN_ACCOUNT_ROOT_INVALID;
    if (types == NULL || gin_credential_validate(c) != GIN_CREDENTIAL_OK ||
        gin_chantypes_validate(types) != 0) return result;
    n = record(expected, c, types);
    if (n == 0) return result;
    if (fstatat(r->rootfd, GIN_ACCOUNT_ROOT_FILENAME, &before, AT_SYMLINK_NOFOLLOW) == 0) {
        result.code = GIN_ACCOUNT_ROOT_UNSAFE;
        if (same(&before, &lock) || !private_file(&before)) return result;
        if (before.st_size < 0 || before.st_size > GIN_ACCOUNT_ROOT_MAX) {
            result.code = GIN_ACCOUNT_ROOT_MISMATCH; return result;
        }
        fd = openat(r->rootfd, GIN_ACCOUNT_ROOT_FILENAME,
            O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
        result.code = GIN_ACCOUNT_ROOT_IO;
        if (fd < 0) return result;
        if (fstat(fd, &opened) != 0) goto done;
        result.code = GIN_ACCOUNT_ROOT_UNSAFE;
        if (!private_file(&opened) || same(&opened, &lock) || !same(&before, &opened)) goto done;
        result.code = entry(r, &lock, &after);
        if (result.code != GIN_ACCOUNT_ROOT_OK) goto done;
        if (!same(&opened, &after)) { result.code = GIN_ACCOUNT_ROOT_UNSAFE; goto done; }
        result.code = GIN_ACCOUNT_ROOT_IO;
        while (used < sizeof(actual) && attempts++ < GIN_ACCOUNT_ROOT_MAX + 128) {
            ssize_t got = read(fd, actual + used, sizeof(actual) - used);
            if (got < 0) { if (errno == EINTR) continue; goto done; }
            if (got == 0) break;
            used += (size_t)got;
        }
        if (attempts >= GIN_ACCOUNT_ROOT_MAX + 128) goto done;
        result.code = entry(r, &lock, &after);
        if (result.code != GIN_ACCOUNT_ROOT_OK) goto done;
        if (fstat(fd, &opened) != 0) { result.code = GIN_ACCOUNT_ROOT_IO; goto done; }
        if (!same(&before, &after) || !same(&before, &opened) || !private_file(&opened)) {
            result.code = GIN_ACCOUNT_ROOT_UNSAFE; goto done;
        }
        result.code = used == n && memcmp(actual, expected, n) == 0 ?
            GIN_ACCOUNT_ROOT_OK : GIN_ACCOUNT_ROOT_MISMATCH;
        goto done;
    }
    result.code = GIN_ACCOUNT_ROOT_IO;
    if (errno != ENOENT) return result;
    result.code = empty_root(r);
    if (result.code != GIN_ACCOUNT_ROOT_OK) return result;
    result.code = held(r, &lock);
    if (result.code != GIN_ACCOUNT_ROOT_OK) return result;
    mask = umask(077);
    fd = openat(r->rootfd, GIN_ACCOUNT_ROOT_FILENAME,
        O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
    (void)umask(mask);
    result.code = GIN_ACCOUNT_ROOT_IO;
    if (fd < 0) return result;
    result.created = true;
    if (fstat(fd, &opened) != 0) goto done;
    result.code = GIN_ACCOUNT_ROOT_UNSAFE;
    if (!private_file(&opened) || same(&opened, &lock)) goto done;
    result.code = GIN_ACCOUNT_ROOT_IO;
    while (used < n && attempts++ < GIN_ACCOUNT_ROOT_MAX + 128) {
        ssize_t wrote = write(fd, expected + used, n - used);
        if (wrote < 0) { if (errno == EINTR) continue; goto done; }
        if (wrote == 0) goto done;
        used += (size_t)wrote;
    }
    if (used != n || fsync(fd) != 0) goto done;
    result.code = entry(r, &lock, &after);
    if (result.code == GIN_ACCOUNT_ROOT_OK && !same(&opened, &after))
        result.code = GIN_ACCOUNT_ROOT_UNSAFE;
done:
    if (close(fd) != 0 && result.code == GIN_ACCOUNT_ROOT_OK)
        result.code = GIN_ACCOUNT_ROOT_IO;
    return result;
}
