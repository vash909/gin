/* Darwin hides O_NOFOLLOW under strict POSIX feature selection. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif
#include "gin_credential.h"
#include <errno.h>
#include <fcntl.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#if JANSSON_VERSION_HEX < 0x020e00
#error "gin requires Jansson >= 2.14"
#endif

void
gin_credential_clear(struct gin_credential *value)
{
    if (value != NULL) {
        volatile unsigned char *p = (volatile unsigned char *)value;
        size_t n = sizeof(*value);
        while (n-- != 0) *p++ = 0;
    }
}

static int
ascii_alnum(unsigned char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9');
}

static int
origin_valid(const char *s)
{
    size_t n = strnlen(s, GIN_CREDENTIAL_ORIGIN_MAX + 1), i = 8, start;
    if (n > GIN_CREDENTIAL_ORIGIN_MAX || n <= 8 || strncmp(s, "https://", 8) != 0)
        return 0;
    start = i;
    if (s[i] == '[') {
        start = ++i;
        while (i < n && s[i] != ']') {
            if (!((s[i] >= '0' && s[i] <= '9') ||
                (s[i] >= 'a' && s[i] <= 'f') ||
                (s[i] >= 'A' && s[i] <= 'F') || s[i] == ':' || s[i] == '.'))
                return 0;
            ++i;
        }
        if (i == start || i == n) return 0;
        ++i;
    } else {
        while (i < n && s[i] != ':') {
            if (!ascii_alnum((unsigned char)s[i]) && s[i] != '-' && s[i] != '.')
                return 0;
            ++i;
        }
        if (i == start) return 0;
    }
    if (i == n) return 1;
    if (s[i++] != ':' || i == n) return 0;
    for (; i < n; ++i)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

enum gin_credential_code
gin_credential_validate(const struct gin_credential *v)
{
    size_t n, i;
    int padding = 0;
    json_t *name;
    if (v == NULL || !origin_valid(v->origin)) return GIN_CREDENTIAL_INVALID;
    n = strnlen(v->token, sizeof(v->token));
    if (n == 0 || n == sizeof(v->token) || v->token[0] == '=')
        return GIN_CREDENTIAL_INVALID;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)v->token[i];
        if (c == '=') { padding = 1; continue; }
        if (padding || (!ascii_alnum(c) && strchr("-._~+/", c) == NULL))
            return GIN_CREDENTIAL_INVALID;
    }
    if (strnlen(v->subject_id, sizeof(v->subject_id)) != 36)
        return GIN_CREDENTIAL_INVALID;
    for (i = 0; i < 36; ++i) {
        char c = v->subject_id[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') return GIN_CREDENTIAL_INVALID;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
                     (c >= 'A' && c <= 'F'))) return GIN_CREDENTIAL_INVALID;
    }
    n = strnlen(v->subject_name, sizeof(v->subject_name));
    if (n == 0 || n == sizeof(v->subject_name)) return GIN_CREDENTIAL_INVALID;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)v->subject_name[i];
        if (c < 32 || c == 127 || (c == 0xc2 && i + 1 < n &&
            (unsigned char)v->subject_name[i + 1] >= 0x80 &&
            (unsigned char)v->subject_name[i + 1] <= 0x9f))
            return GIN_CREDENTIAL_INVALID;
    }
    name = json_stringn(v->subject_name, n);
    if (name == NULL) return GIN_CREDENTIAL_INVALID;
    json_decref(name);
    return GIN_CREDENTIAL_OK;
}

static int
same_inode(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino;
}

static int
private_file(const struct stat *s)
{
    return S_ISREG(s->st_mode) && (s->st_mode & 07777) == 0600 &&
        s->st_uid == geteuid() && s->st_nlink == 1;
}

static enum gin_credential_code
held_root(const struct gin_root *r, struct stat *lock)
{
    struct stat dir, path;
    if (r == NULL || r->rootfd < 0 || r->lockfd < 0)
        return GIN_CREDENTIAL_INVALID;
    if (fstat(r->rootfd, &dir) != 0 || fstat(r->lockfd, lock) != 0)
        return GIN_CREDENTIAL_IO;
    if (!S_ISDIR(dir.st_mode) || (dir.st_mode & 07777) != 0700 ||
        dir.st_uid != geteuid() || !private_file(lock))
        return GIN_CREDENTIAL_UNSAFE;
    if (fstatat(r->rootfd, ".gin.lock", &path, AT_SYMLINK_NOFOLLOW) != 0)
        return GIN_CREDENTIAL_IO;
    if (!private_file(&path) || !same_inode(lock, &path))
        return GIN_CREDENTIAL_UNSAFE;
    return GIN_CREDENTIAL_OK;
}

/* Always inspect before open: closing a lock-inode alias would drop ownership. */
static enum gin_credential_code
target(const struct gin_root *r, const struct stat *lock, struct stat *s)
{
    if (fstatat(r->rootfd, GIN_CREDENTIAL_FILENAME, s, AT_SYMLINK_NOFOLLOW) != 0)
        return errno == ENOENT ? GIN_CREDENTIAL_NOT_FOUND : GIN_CREDENTIAL_IO;
    if (same_inode(s, lock) || !private_file(s)) return GIN_CREDENTIAL_UNSAFE;
    return GIN_CREDENTIAL_OK;
}

/* Refuse nesting before entering the recursive decoder, even in tiny inputs. */
static int
flat_json(const char *s, size_t n)
{
    size_t i;
    int depth = 0, string = 0, escape = 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == 0) return 0;
        if (string) {
            if (escape) escape = 0;
            else if (c == '\\') escape = 1;
            else if (c == '"') string = 0;
        } else if (c == '"') string = 1;
        else if (c == '[' || c == ']') return 0;
        else if (c == '{') { if (++depth > 1) return 0; }
        else if (c == '}') { if (--depth < 0) return 0; }
    }
    return !string && depth == 0;
}

static int
copy_field(json_t *object, const char *key, char *out, size_t cap)
{
    json_t *field = json_object_get(object, key);
    size_t n;
    if (!json_is_string(field)) return 0;
    n = json_string_length(field);
    if (n >= cap || memchr(json_string_value(field), 0, n) != NULL) return 0;
    memcpy(out, json_string_value(field), n);
    out[n] = 0;
    return 1;
}

enum gin_credential_code
gin_credential_load(const struct gin_root *r, const char *origin,
                    struct gin_credential *v)
{
    struct stat lock, before, opened, after;
    enum gin_credential_code code;
    char buffer[GIN_CREDENTIAL_FILE_MAX + 1];
    size_t used = 0;
    int fd = -1, attempts = 0;
    json_t *object = NULL, *version;
    gin_credential_clear(v);
    memset(buffer, 0, sizeof(buffer));
    if (v == NULL || origin == NULL || !origin_valid(origin))
        return GIN_CREDENTIAL_INVALID;
    code = held_root(r, &lock);
    if (code != GIN_CREDENTIAL_OK) goto done;
    code = target(r, &lock, &before);
    if (code != GIN_CREDENTIAL_OK) goto done;
    if (before.st_size < 0 || before.st_size > GIN_CREDENTIAL_FILE_MAX) {
        code = GIN_CREDENTIAL_INVALID; goto done;
    }
    fd = openat(r->rootfd, GIN_CREDENTIAL_FILENAME,
        O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    code = GIN_CREDENTIAL_IO;
    if (fd == -1 || fstat(fd, &opened) != 0) goto done;
    code = GIN_CREDENTIAL_UNSAFE;
    if (!private_file(&opened) || same_inode(&opened, &lock) ||
        !same_inode(&before, &opened)) goto done;
    code = target(r, &lock, &after);
    if (code != GIN_CREDENTIAL_OK) goto done;
    if (!same_inode(&opened, &after)) { code = GIN_CREDENTIAL_UNSAFE; goto done; }
    code = GIN_CREDENTIAL_IO;
    while (used < sizeof(buffer) && attempts++ < GIN_CREDENTIAL_FILE_MAX + 128) {
        ssize_t got = read(fd, buffer + used, sizeof(buffer) - used);
        if (got < 0) { if (errno == EINTR) continue; goto done; }
        if (got == 0) break;
        used += (size_t)got;
    }
    code = GIN_CREDENTIAL_INVALID;
    if (used > GIN_CREDENTIAL_FILE_MAX || attempts >= GIN_CREDENTIAL_FILE_MAX + 128 ||
        !flat_json(buffer, used)) goto done;
    object = json_loadb(buffer, used, JSON_REJECT_DUPLICATES, NULL);
    if (!json_is_object(object) || json_object_size(object) != 5) goto done;
    version = json_object_get(object, "version");
    if (!json_is_integer(version) || json_integer_value(version) != 1 ||
        !copy_field(object, "origin", v->origin, sizeof(v->origin)) ||
        !copy_field(object, "token", v->token, sizeof(v->token)) ||
        !copy_field(object, "subject_id", v->subject_id, sizeof(v->subject_id)) ||
        !copy_field(object, "subject_name", v->subject_name, sizeof(v->subject_name)))
        goto done;
    code = gin_credential_validate(v);
    if (code == GIN_CREDENTIAL_OK && strcmp(origin, v->origin) != 0)
        code = GIN_CREDENTIAL_INVALID;
done:
    if (fd != -1 && close(fd) != 0 && code == GIN_CREDENTIAL_OK)
        code = GIN_CREDENTIAL_IO;
    json_decref(object);
    {
        volatile char *p = buffer;
        size_t n = sizeof(buffer);
        while (n-- != 0) *p++ = 0;
    }
    if (code != GIN_CREDENTIAL_OK) gin_credential_clear(v);
    return code;
}

enum gin_credential_code
gin_credential_save(const struct gin_root *r, const struct gin_credential *v)
{
    struct stat lock, before, after, created;
    enum gin_credential_code code, prior;
    json_t *object = NULL;
    char *data = NULL, name[80];
    size_t n = 0, used = 0;
    int fd = -1, own = 0, i, attempts = 0;
    static unsigned long sequence;
    code = gin_credential_validate(v);
    if (code != GIN_CREDENTIAL_OK) return code;
    code = held_root(r, &lock);
    if (code != GIN_CREDENTIAL_OK) return code;
    prior = target(r, &lock, &before);
    if (prior != GIN_CREDENTIAL_OK && prior != GIN_CREDENTIAL_NOT_FOUND) return prior;
    object = json_pack("{s:i,s:s,s:s,s:s,s:s}", "version", 1,
        "origin", v->origin, "token", v->token, "subject_id", v->subject_id,
        "subject_name", v->subject_name);
    code = GIN_CREDENTIAL_IO;
    if (object == NULL) goto done;
    data = json_dumps(object, JSON_COMPACT);
    if (data == NULL) goto done;
    n = strlen(data);
    if (n > GIN_CREDENTIAL_FILE_MAX) { code = GIN_CREDENTIAL_INVALID; goto done; }
    for (i = 0; i < 64; ++i) {
        (void)snprintf(name, sizeof(name), ".gin.credentials.tmp.%ld.%lu",
                       (long)getpid(), ++sequence);
        fd = openat(r->rootfd, name,
            O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC, 0600);
        if (fd != -1) { own = 1; break; }
        if (errno != EEXIST) goto done;
    }
    if (fd == -1) goto done;
    if (fstat(fd, &created) != 0 || !private_file(&created) ||
        same_inode(&created, &lock)) goto done;
    while (used < n && attempts++ < GIN_CREDENTIAL_FILE_MAX + 128) {
        ssize_t wrote = write(fd, data + used, n - used);
        if (wrote < 0) { if (errno == EINTR) continue; goto done; }
        if (wrote == 0) goto done;
        used += (size_t)wrote;
    }
    if (used != n || fsync(fd) != 0) goto done;
    if (fstat(fd, &after) != 0 || !private_file(&after) ||
        !same_inode(&created, &after)) goto done;
    if (fstatat(r->rootfd, name, &after, AT_SYMLINK_NOFOLLOW) != 0 ||
        !private_file(&after) || !same_inode(&created, &after)) goto done;
    if (close(fd) != 0) { fd = -1; goto done; }
    fd = -1;
    code = target(r, &lock, &after);
    if (code != prior || (code == GIN_CREDENTIAL_OK && !same_inode(&before, &after))) {
        if (code == GIN_CREDENTIAL_OK || code == GIN_CREDENTIAL_NOT_FOUND)
            code = GIN_CREDENTIAL_UNSAFE;
        goto done;
    }
    code = GIN_CREDENTIAL_IO;
    if (renameat(r->rootfd, name, r->rootfd, GIN_CREDENTIAL_FILENAME) != 0) goto done;
    own = 0;
    /* A directory fsync failure is reported even though publication occurred. */
    if (fsync(r->rootfd) != 0) goto done;
    code = GIN_CREDENTIAL_OK;
done:
    if (fd != -1) (void)close(fd);
    if (own) (void)unlinkat(r->rootfd, name, 0);
    if (data != NULL) {
        volatile char *p = data;
        size_t left = n;
        while (left-- != 0) *p++ = 0;
        free(data);
    }
    json_decref(object);
    return code;
}

enum gin_credential_code
gin_credential_forget(const struct gin_root *r)
{
    struct stat lock, before, after;
    enum gin_credential_code code = held_root(r, &lock);
    if (code != GIN_CREDENTIAL_OK) return code;
    code = target(r, &lock, &before);
    if (code != GIN_CREDENTIAL_OK) return code;
    code = target(r, &lock, &after);
    if (code != GIN_CREDENTIAL_OK) return code;
    if (!same_inode(&before, &after)) return GIN_CREDENTIAL_UNSAFE;
    if (unlinkat(r->rootfd, GIN_CREDENTIAL_FILENAME, 0) != 0 || fsync(r->rootfd) != 0)
        return GIN_CREDENTIAL_IO;
    return GIN_CREDENTIAL_OK;
}
const char *gin_credential_error(enum gin_credential_code c)
{
    switch (c) {
    case GIN_CREDENTIAL_OK: return "success";
    case GIN_CREDENTIAL_INVALID: return "invalid credential data";
    case GIN_CREDENTIAL_UNSAFE: return "unsafe credential storage";
    case GIN_CREDENTIAL_IO: return "credential storage I/O failure";
    case GIN_CREDENTIAL_NOT_FOUND: return "credential storage not found";
    }
    return "unknown credential error";
}
