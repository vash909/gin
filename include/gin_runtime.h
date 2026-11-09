#ifndef GIN_RUNTIME_H
#define GIN_RUNTIME_H

#include "gin_input.h"

enum gin_runtime_event {
    GIN_RUNTIME_READY,
    GIN_RUNTIME_LINE,
    GIN_RUNTIME_EMPTY,
    GIN_RUNTIME_REJECTED
};
/* Synchronous bounded NONBLOCKING callback, no recursive runtime invocation,
 * descriptor changes, signal disposition/mask changes, fork or threads.
 * Record storage is borrowed for this call only; length is nonzero only for
 * LINE. Return 0 to continue, 1 for orderly stop, -1 with errno for failure.
 * READY means local reader/handlers exist, NOT transport or acceptance.
 * Singleton, single-threaded process API: caller must not independently open
 * or close the root lock inode. Owns root, FIFO and wake pipe until return;
 * all child fds close before root release. Restores previous SIGINT/SIGTERM
 * actions and calling mask. Pending signals at teardown follow restored actions.
 * All strings and callback non-NULL. Validates before mutation. Returns 0 for
 * stop/signal, -1 with errno for startup, I/O, callback or cleanup error.
 * No transport, logs, daemon, queues or threads. See docs/RUNTIME.md. */
typedef int (*gin_runtime_callback)(enum gin_runtime_event event,
                                  const struct gin_record *record, void *context);
int gin_runtime_run(const char *path, const char *network, const char *target,
                    const char *chantypes, gin_runtime_callback callback,
                    void *context);

#endif
