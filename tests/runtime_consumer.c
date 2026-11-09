/* Test-only ongoing local consumer. Synthetic payloads only, no transport. */
#include "gin_runtime.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int consume(enum gin_runtime_event event, const struct gin_record *record, void *context)
{
    size_t i;
    (void)context;
    if (event == GIN_RUNTIME_READY) {
        assert(gin_runtime_run("unused", "net", "#one", "#", consume, NULL) == -1);
        assert(errno == EBUSY);
        puts("ready");
    } else {
        printf("%d:", (int)event);
        for (i = 0; i < record->length; ++i)
            printf("%02x", (unsigned int)record->data[i]);
        putchar('\n');
    }
    assert(fflush(stdout) == 0);
    return 0;
}

int main(int argc, char **argv)
{
    struct sigaction previous, after;
    sigset_t mask, after_mask;
    int sig;
    assert(argc == 2);
    alarm(15);
    assert(sigaction(SIGINT, NULL, &previous) == 0);
    assert(sigprocmask(SIG_SETMASK, NULL, &mask) == 0);
    assert(gin_runtime_run(argv[1], "net", "#one", "#", consume, NULL) == 0);
    assert(sigaction(SIGINT, NULL, &after) == 0);
    assert(after.sa_handler == previous.sa_handler);
    assert(sigprocmask(SIG_SETMASK, NULL, &after_mask) == 0);
    for (sig = 1; sig < 32; ++sig)
        assert(sigismember(&mask, sig) == sigismember(&after_mask, sig));
    return 0;
}
