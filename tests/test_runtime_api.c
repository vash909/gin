/* Same-process lifecycle, signal restoration and descriptor cleanup checks. */
#include "gin_runtime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <unistd.h>

static int calls;
static int stop_ready(enum gin_runtime_event event, const struct gin_record *record, void *context)
{
    (void)context;
    assert(event == GIN_RUNTIME_READY && record->length == 0);
    ++calls;
    return 1;
}
static int fail_ready(enum gin_runtime_event event, const struct gin_record *record, void *context)
{
    (void)event; (void)record; (void)context;
    errno = EACCES;
    return -1;
}
static void prior_handler(int number) { (void)number; }

static int descriptors(void)
{
    int fd, count = 0;
    for (fd = 0; fd < 256; ++fd)
        if (fcntl(fd, F_GETFD) != -1)
            ++count;
    return count;
}

int main(int argc, char **argv)
{
    struct sigaction action, before_int, before_term, after;
    struct rlimit limit, restricted;
    sigset_t before_mask, after_mask;
    int initial, i;
    assert(argc == 2);
    alarm(15);
    action.sa_handler = prior_handler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGUSR1);
    action.sa_flags = SA_RESTART;
    assert(sigaction(SIGINT, &action, &before_int) == 0);
    assert(sigaction(SIGTERM, &action, &before_term) == 0);
    assert(sigprocmask(SIG_SETMASK, NULL, &before_mask) == 0);
    initial = descriptors();
    for (i = 0; i < 10; ++i) {
        assert(gin_runtime_run(argv[1], "net", "#one", "#", stop_ready, NULL) == 0);
        assert(gin_runtime_run(argv[1], "net", "#one", "#", fail_ready, NULL) == -1);
        assert(errno == EACCES);
        assert(descriptors() == initial);
    }
    assert(calls == 10);
    assert(gin_runtime_run(argv[1], "bad/name", "#one", "#", stop_ready, NULL) == -1);
    assert(descriptors() == initial);
    assert(getrlimit(RLIMIT_NOFILE, &limit) == 0);
    restricted = limit;
    /* Keep only the already-open standard descriptors: pipe must fail. */
    restricted.rlim_cur = 3;
    assert(setrlimit(RLIMIT_NOFILE, &restricted) == 0);
    assert(gin_runtime_run(argv[1], "net", "#one", "#", stop_ready, NULL) == -1);
    assert(errno == EMFILE);
    assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
    assert(descriptors() == initial);
    /* Exercise failures after wake allocation, during held-root acquisition
     * and child initialization. Each attempt restores its descriptor budget
     * before checking leaks or retrying; no process exit can hide a leak. */
    for (i = 5; i <= 12; ++i) {
        int result;
        restricted.rlim_cur = (rlim_t)i;
        assert(setrlimit(RLIMIT_NOFILE, &restricted) == 0);
        result = gin_runtime_run(argv[1], "net", "#one", "#", stop_ready, NULL);
        assert(result == 0 || (result == -1 && errno == EMFILE));
        assert(setrlimit(RLIMIT_NOFILE, &limit) == 0);
        assert(descriptors() == initial);
    }
    assert(sigaction(SIGINT, NULL, &after) == 0);
    assert(after.sa_handler == prior_handler && (after.sa_flags & SA_RESTART));
    assert(sigismember(&after.sa_mask, SIGUSR1) == 1);
    assert(sigaction(SIGTERM, NULL, &after) == 0);
    assert(after.sa_handler == prior_handler && (after.sa_flags & SA_RESTART));
    assert(sigprocmask(SIG_SETMASK, NULL, &after_mask) == 0);
    for (i = 1; i < 32; ++i)
        assert(sigismember(&before_mask, i) == sigismember(&after_mask, i));
    assert(sigaction(SIGINT, &before_int, NULL) == 0);
    assert(sigaction(SIGTERM, &before_term, NULL) == 0);
    puts("Runtime same-process API tests passed.");
    return 0;
}
