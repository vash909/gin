#ifndef TESTS_SINKS_EXEC_PROBE_H
#define TESTS_SINKS_EXEC_PROBE_H

#include <signal.h>
#include <time.h>

/* Private exec modes keep inherited owner allocations out of child leak checks. */
static const char *sinks_test_executable;

static int
sinks_probe_dispatch(int argc, char **argv)
{
    char file[4200];
    struct flock lock;
    int fd, rc, saved_errno, length, held;
    if (argc < 2 || strcmp(argv[1], "--sinks-lease-probe"))
        return -1;
    alarm(3);
    if (argc != 4 || (strcmp(argv[3], "0") && strcmp(argv[3], "1")))
        return 1;
    held = !strcmp(argv[3], "1");
    length = snprintf(file, sizeof(file), "%s/.gin.lock", argv[2]);
    if (length < 0 || (size_t)length >= sizeof(file))
        return 1;
    fd = open(file, O_RDWR | O_NOFOLLOW);
    if (fd < 0)
        return 1;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    rc = fcntl(fd, F_SETLK, &lock);
    saved_errno = errno;
    if (close(fd))
        return 1;
    return held ? !(rc < 0 && (saved_errno == EACCES || saved_errno == EAGAIN)) : rc != 0;
}

static void
sinks_wait_child(pid_t child)
{
    struct timespec start, now, pause_time = {0, 10000000};
    int status = 0, failed = 0;
    pid_t result;
    if (clock_gettime(CLOCK_MONOTONIC, &start))
        failed = 1;
    while (!failed) {
        result = waitpid(child, &status, WNOHANG);
        if (result == child) {
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
            return;
        }
        if (result < 0 && errno != EINTR)
            failed = 1;
        if (clock_gettime(CLOCK_MONOTONIC, &now))
            failed = 1;
        else if (!failed && (now.tv_sec - start.tv_sec > 8 ||
                 (now.tv_sec - start.tv_sec == 8 && now.tv_nsec >= start.tv_nsec)))
            failed = 1;
        if (!failed && nanosleep(&pause_time, NULL) && errno != EINTR)
            failed = 1;
    }
    /* Independent parent watchdog: kill and reap before reporting failure. */
    CHECK(kill(child, SIGKILL) == 0 || errno == ESRCH);
    do { result = waitpid(child, &status, 0); } while (result < 0 && errno == EINTR);
    CHECK(result == child);
    CHECK(!failed);
}

static void
lease(const char *path, int held)
{
    pid_t child;
    CHECK(fflush(NULL) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        char *args[] = {(char *)sinks_test_executable,
                        (char *)"--sinks-lease-probe", (char *)path,
                        (char *)(held ? "1" : "0"), NULL};
        alarm(3);
        execvp(sinks_test_executable, args);
        _exit(127);
    }
    sinks_wait_child(child);
}

#endif
