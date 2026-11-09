/* Pipe-controlled child used by black-box tests; not an application runtime. */
#include "gin_fs.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int
main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    char command;

    assert(argc == 2);
    /* A broken handshake cannot leave a child blocked indefinitely. */
    alarm(15);
    assert(gin_root_acquire(argv[1], &root) == 0);
    assert(root.rootfd >= 0 && root.lockfd >= 0);
    assert((fcntl(root.rootfd, F_GETFD) & FD_CLOEXEC) != 0);
    assert((fcntl(root.lockfd, F_GETFD) & FD_CLOEXEC) != 0);
    /* Window initialization must use the retained descriptor, not the cwd. */
    assert(chdir("/") == 0);
    errno = 0;
    assert(gin_root_acquire(argv[1], &root) == -1 && errno == EINVAL);
    assert(gin_root_init(&root, "unused", "#bad/name", "#") == -1);
    assert(gin_root_init(&root, "net", "#held", "#") == 0);
    assert(gin_root_init(&root, "net", "#second", "#") == 0);
    assert(puts("ready") >= 0);
    assert(fflush(stdout) == 0);
    assert(read(STDIN_FILENO, &command, 1) == 1 && command == 'r');
    assert(gin_root_release(&root) == 0);
    assert(root.rootfd == -1 && root.lockfd == -1);
    assert(gin_root_release(&root) == 0);
    assert(gin_root_init(&root, "net", "#closed", "#") == -1);
    assert(puts("released") >= 0);
    assert(fflush(stdout) == 0);
    assert(read(STDIN_FILENO, &command, 1) == 1 && command == 'q');
    return 0;
}
