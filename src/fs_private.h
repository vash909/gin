#ifndef GIN_FS_PRIVATE_H
#define GIN_FS_PRIVATE_H
#include "gin_fs.h"
#include "gin_naming.h"
/* Compiled-in trusted pure lexical callbacks ONLY, never user/plugin data.
 * Callback must retain native untrusted-component guards. These primitives
 * do not validate account origin/UUID/layout binding. Borrow one held root;
 * returned owned descriptors must close before releasing that root. */
typedef int (*gin_fs_name_prepare_fn)(const char *, const char *, const char *, struct gin_name *);
int gin_root_init_named(const struct gin_root *, const char *, const char *, const char *, gin_fs_name_prepare_fn);
int gin_root_input_open_named(const struct gin_root *, const char *, const char *, const char *, gin_fs_name_prepare_fn);
int gin_root_output_open_named(const struct gin_root *, const char *, const char *, const char *, gin_fs_name_prepare_fn);
int gin_root_window_open_named(const struct gin_root *, const char *, const char *, const char *, gin_fs_name_prepare_fn);
/* Existing-only owned network directory; borrow held root, never reopen lock. */
int gin_root_network_open(const struct gin_root *, const char *);
int gin_root_network_init(const struct gin_root *, const char *);
int gin_root_network_input_open(const struct gin_root *, const char *);
int gin_root_network_output_open(const struct gin_root *, const char *);
/* Existing-only traversal under the caller's live held root. No creation,
 * permission repair, lock acquisition or independent lock-inode open/close.
 * Return an owned directory descriptor; close it before releasing the root. */
int gin_root_window_open(const struct gin_root *, const char *, const char *, const char *);
#endif
