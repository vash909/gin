/* Deterministic seams: only runtime.c is compiled with these substitutions. */
#include "gin_runtime.h"
#include "gin_fs.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int mode, fired, calls;
static const char *root_path;

int fixture_sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    if (mode == 1 && !fired && how == SIG_SETMASK && set != NULL) {
        fired = 1;
        assert(raise(SIGTERM) == 0); /* Pending until the real unmask. */
    }
    return sigprocmask(how, set, old);
}

enum gin_input_result fixture_next(struct gin_input *input, int fd, struct gin_record *record)
{
    enum gin_input_result result = gin_input_next(input, fd, record);
    if (mode == 2 && !fired && result == GIN_INPUT_LINE) {
        fired = 1;
        assert(raise(SIGTERM) == 0);
    }
    return result;
}

static int callback(enum gin_runtime_event event, const struct gin_record *record, void *context)
{
    char path[4096];
    int fd;
    (void)record; (void)context;
    ++calls;
    if (event == GIN_RUNTIME_READY && mode == 2) {
        assert(snprintf(path, sizeof(path), "%s/net/one/in", root_path) > 0);
        fd = open(path, O_WRONLY | O_NONBLOCK);
        assert(fd != -1);
        assert(write(fd, "line\n", 5) == 5);
        assert(close(fd) == 0);
        return 0;
    }
    errno = EACCES;
    return -1; /* A forbidden dispatch must change the result. */
}
static void prior(int number) { (void)number; }
static int descriptors(void)
{
    int fd, count = 0;
    for (fd = 0; fd < 256; ++fd)
        if (fcntl(fd, F_GETFD) != -1) ++count;
    return count;
}
int main(int argc, char **argv)
{
    struct sigaction action, after;
    sigset_t mask, after_mask;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    int initial, i;
    assert(argc == 3);
    alarm(5);
    root_path = argv[1]; mode = atoi(argv[2]);
    memset(&action, 0, sizeof(action));
    action.sa_handler = prior; action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask); sigaddset(&action.sa_mask, SIGUSR1);
    assert(sigaction(SIGINT, &action, NULL) == 0);
    assert(sigaction(SIGTERM, &action, NULL) == 0);
    assert(sigprocmask(SIG_SETMASK, NULL, &mask) == 0);
    initial = descriptors();
    assert(gin_runtime_run(root_path, "net", "#one", "#", callback, NULL) == 0);
    assert(fired && calls == (mode == 1 ? 0 : 1));
    assert(descriptors() == initial);
    for (i = 0; i < 2; ++i) {
        assert(sigaction(i ? SIGTERM : SIGINT, NULL, &after) == 0);
        assert(after.sa_handler == prior && (after.sa_flags & SA_RESTART));
        assert(sigismember(&after.sa_mask, SIGUSR1) == 1);
    }
    assert(sigprocmask(SIG_SETMASK, NULL, &after_mask) == 0);
    for (i = 1; i < 32; ++i)
        assert(sigismember(&mask, i) == sigismember(&after_mask, i));
    assert(gin_root_acquire(root_path, &root) == 0);
    assert(gin_root_release(&root) == 0);
    return 0;
}
