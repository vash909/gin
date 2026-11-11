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

/* Open an EXISTING window FIFO through one live, held root and shared naming.
 * Returns caller-owned O_RDONLY|O_NONBLOCK|O_CLOEXEC fd or -1 with errno.
 * No creation/repair/log access. Non-NULL strings; root ownership must be held
 * in this process for the entire fd lifetime. Close fd before root release.
 * Does not acquire/reopen/close the lock. See docs/INPUT.md and LOCKING.md. */
int gin_root_input_open(const struct gin_root *root, const char *network,
                         const char *target, const char *chantypes);

/* Open only an EXISTING initialized window out through one live held root.
 * Returns caller-owned O_WRONLY|O_APPEND|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC fd,
 * or -1 with errno. Validate private directories and owned0600/single-link
 * regular file, preserving before/open/after inode checks. No create/repair,
 * truncation or lock manipulation. Close before releasing the same held root.
 */
int gin_root_output_open(const struct gin_root *root, const char *network,
                          const char *target, const char *chantypes);

int gin_init(const char *root, const char *network, const char *target,
             const char *chantypes);

#endif
