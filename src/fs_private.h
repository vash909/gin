#ifndef GIN_FS_PRIVATE_H
#define GIN_FS_PRIVATE_H
#include "gin_fs.h"
/* Existing-only traversal under the caller's live held root. No creation,
 * permission repair, lock acquisition or independent lock-inode open/close.
 * Return an owned directory descriptor; close it before releasing the root. */
int gin_root_window_open(const struct gin_root *, const char *, const char *, const char *);
#endif
