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
/* Checked one-window preparation under an existing network. The root lease
 * must be live in this process; ancestors and same-UID peers are trusted and
 * cooperating. Namespace checks detect observed replacement, not an atomic
 * transaction against a hostile concurrent same-UID writer.
 * Initialize out to {-1,-1}; failure leaves it unchanged. No rollback/repair:
 * mutation_attempted includes failed creation attempts and partial paths remain.
 * error is the primary errno (or first cleanup-only errno); cleanup_failed is
 * independent and sticky. Close each proven-safe owned fd once, never retry.
 * An endpoint whose initial fstat cannot prove it distinct from the held lock
 * is deliberately retained, possibly until process exit. cleanup_failed means
 * the owner must stop, not retry. No quarantine/recovery API is provided.
 * Diagnostic tests may close retained fds ONLY AFTER releasing the root.
 * O_DIRECTORY opens cannot denote the regular held lock (kernel type guard).
 * Success publishes both fds only after all temporary directory closes succeed.
 * Close published descriptors before releasing the borrowed root. */
struct gin_fs_pair { int input; int output; };
struct gin_fs_pair_result { int error; int mutation_attempted; int cleanup_failed; };
struct gin_fs_pair_result gin_root_pair_prepare_named(const struct gin_root *,
    const char *, const char *, const char *, gin_fs_name_prepare_fn,
    struct gin_fs_pair *);
#endif
