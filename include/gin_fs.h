#ifndef GIN_FS_H
#define GIN_FS_H

/* Lexical validation only: no filesystem access. */
int gin_validate(const char *root, const char *network, const char *target,
             const char *chantypes);
/* Single-process ownership; initialize once, never copy a live handle.
 * POSIX locks belong to the process, not the handle: closing ANY descriptor
 * for the lock inode releases that process's locks. Do not independently open
 * it (including via config), acquire this root twice, or use across fork.
 * Close all child descriptors before release. See docs/LOCKING.md. */
struct gin_root {
    int rootfd;
    int lockfd;
};
#define GIN_ROOT_INITIALIZER { -1, -1 }

/* Non-NULL inputs; 0 on success, -1 with errno on failure. acquire validates
 * the root lexically, creates/reuses it and retains its lock and descriptor.
 * init validates names before child mutation and leaves the lock held even
 * on failure. release consumes the handle, does not retry interrupted close,
 * and never unlinks the persistent lock file. */
int gin_root_acquire(const char *path, struct gin_root *root);
int gin_root_init(const struct gin_root *root, const char *network,
                  const char *target, const char *chantypes);
int gin_root_release(struct gin_root *root);

int gin_init(const char *root, const char *network, const char *target,
             const char *chantypes);

#endif
