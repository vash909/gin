#include "gin_runtime.h"
#include "gin_fs.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopping;
static volatile sig_atomic_t wake_fd = -1;
static int active;

static void
request_stop(int number)
{
    int saved = errno;
    unsigned char byte = (unsigned char)number;
    stopping = 1;
    if (wake_fd != -1) {
        ssize_t ignored = write((int)wake_fd, &byte, 1);
        (void)ignored;
    }
    errno = saved;
}

static int
prepare_fd(int fd)
{
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
        return -1;
    flags = fcntl(fd, F_GETFD);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1)
        return -1;
    return 0;
}

/* Positive ceiling milliseconds, bounded to our 100ms EOF retry interval. */
static int
remaining(const struct timespec *deadline)
{
    struct timespec now;
    long ns;
    time_t seconds;
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1)
        return -1;
    seconds = deadline->tv_sec - now.tv_sec;
    ns = deadline->tv_nsec - now.tv_nsec;
    if (seconds < 0 || (seconds == 0 && ns <= 0))
        return 0;
    return (int)(seconds * 1000 + (ns + 999999) / 1000000);
}

int
gin_runtime_run(const char *path, const char *network, const char *target,
                const char *chantypes, gin_runtime_callback callback, void *context)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record = { {0}, 0 };
    struct sigaction action, old_int, old_term;
    sigset_t blocked, old_mask;
    struct timespec deadline = {0, 0};
    int wake[2] = {-1, -1}, fd = -1, installed = 0, masked = 0;
    int result = -1, saved = 0, runnable = 1, cooldown = 0, decision;

    if (active) {
        errno = EBUSY;
        return -1;
    }
    if (gin_validate(path, network, target, chantypes) == -1)
        return -1;
    active = 1;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGINT);
    sigaddset(&blocked, SIGTERM);
    if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) == -1)
        goto done;
    masked = 1;
    if (pipe(wake) == -1 || prepare_fd(wake[0]) == -1 || prepare_fd(wake[1]) == -1)
        goto done;
    memset(&action, 0, sizeof(action));
    action.sa_handler = request_stop;
    action.sa_mask = blocked;
    stopping = 0;
    wake_fd = wake[1];
    if (sigaction(SIGINT, &action, &old_int) == -1)
        goto done;
    installed = 1;
    if (sigaction(SIGTERM, &action, &old_term) == -1)
        goto done;
    installed = 2;
    if (gin_root_acquire(path, &root) == -1 ||
        gin_root_init(&root, network, target, chantypes) == -1)
        goto done;
    fd = gin_root_input_open(&root, network, target, chantypes);
    if (fd == -1 || sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1)
        goto done;
    if (stopping) {
        result = 0;
        goto done;
    }
    decision = callback(GIN_RUNTIME_READY, &record, context);
    if (decision != 0) {
        result = decision == 1 ? 0 : -1;
        goto done;
    }
    while (!stopping) {
        enum gin_input_result event;
        enum gin_runtime_event dispatched;
        if (!runnable) {
            struct pollfd fds[2];
            int timeout = -1, polled;
            if (cooldown) {
                timeout = remaining(&deadline);
                if (timeout == -1)
                    goto done;
                if (timeout == 0) {
                    cooldown = 0;
                    runnable = 1;
                    continue;
                }
            }
            fds[0].fd = wake[0];
            fds[0].events = POLLIN;
            fds[0].revents = 0;
            /* A HUP FIFO is level-triggered. Exclude it during monotonic
             * backoff but retain its secure inode and poll the wake pipe. */
            fds[1].fd = cooldown ? -1 : fd;
            fds[1].events = POLLIN;
            fds[1].revents = 0;
            polled = poll(fds, 2, timeout);
            if (polled == -1) {
                if (errno == EINTR && stopping)
                    break;
                goto done;
            }
            if ((fds[0].revents | fds[1].revents) & (POLLERR | POLLNVAL)) {
                errno = EIO;
                goto done;
            }
            if (stopping)
                break;
            if (fds[0].revents != 0) {
                errno = EIO;
                goto done;
            }
            if (cooldown)
                continue;
            if (!(fds[1].revents & (POLLIN | POLLHUP)))
                continue;
        }
        /* One bounded parser quantum, then signal check, even for buffered
         * coalesced records. Never wait on kernel readiness for buffered work. */
        event = gin_input_next(&input, fd, &record);
        if (stopping)
            break;
        runnable = 1;
        if (event == GIN_INPUT_ERROR || event == GIN_INPUT_INTERRUPTED) {
            if (stopping)
                break;
            goto done;
        }
        if (event == GIN_INPUT_AGAIN) {
            runnable = 0;
            continue;
        }
        if (event == GIN_INPUT_EOF) {
            if (clock_gettime(CLOCK_MONOTONIC, &deadline) == -1)
                goto done;
            deadline.tv_nsec += 100000000;
            if (deadline.tv_nsec >= 1000000000) {
                ++deadline.tv_sec;
                deadline.tv_nsec -= 1000000000;
            }
            cooldown = 1;
            runnable = 0;
            continue;
        }
        if (event == GIN_INPUT_MORE)
            continue;
        dispatched = event == GIN_INPUT_LINE ? GIN_RUNTIME_LINE :
            event == GIN_INPUT_EMPTY ? GIN_RUNTIME_EMPTY : GIN_RUNTIME_REJECTED;
        decision = callback(dispatched, &record, context);
        if (decision != 0) {
            result = decision == 1 ? 0 : -1;
            goto done;
        }
    }
    result = 0;
done:
    saved = errno;
    /* Single-threaded: block before restoring actions and closing wake ends.
     * No handler can write a closed/reused fd; read end outlives write end. */
    if (masked && sigprocmask(SIG_BLOCK, &blocked, NULL) == -1) {
        if (result == 0) { result = -1; saved = errno; }
    }
    if (installed >= 2 && sigaction(SIGTERM, &old_term, NULL) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    if (installed >= 1 && sigaction(SIGINT, &old_int, NULL) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    wake_fd = -1;
    if (fd != -1 && close(fd) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    if (wake[1] != -1 && close(wake[1]) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    if (wake[0] != -1 && close(wake[0]) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    if (gin_root_release(&root) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    if (masked && sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1 && result == 0) {
        result = -1; saved = errno;
    }
    active = 0;
    errno = saved;
    return result;
}
