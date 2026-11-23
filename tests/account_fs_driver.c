#include "gin_fs.h"
#include "fs_private.h"
#ifndef LEGACY_BRIDGE
#include "account_fs.h"
#endif
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/stat.h>

static int locked(int lockfd)
{
    pid_t p = fork();
    int st;
    if (p == -1) return 0;
    if (p == 0) {
        struct flock lock;
        int rc, saved;
        alarm(2);
        memset(&lock, 0, sizeof(lock));
        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        rc = fcntl(lockfd, F_SETLK, &lock);
        saved = errno;
        if (close(lockfd) == -1) _exit(2);
        _exit(rc == -1 && (saved == EAGAIN || saved == EACCES) ? 0 : 1);
    }
    return waitpid(p, &st, 0) == p && WIFEXITED(st) && WEXITSTATUS(st) == 0;
}
static int released(const char *path)
{
    pid_t p = fork();
    int st;
    if (p == -1) return 0;
    if (p == 0) {
        struct gin_root other = GIN_ROOT_INITIALIZER;
        alarm(2);
        if (gin_root_acquire(path, &other) == -1) _exit(1);
        _exit(gin_root_release(&other) == 0 ? 0 : 2);
    }
    return waitpid(p, &st, 0) == p && WIFEXITED(st) && WEXITSTATUS(st) == 0;
}
int main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    char line[1024], op[16], net[300], target[300];
    int rc, fd, flags, saved;
    if (argc != 2) return 2;
    alarm(10);
    if (gin_root_acquire(argv[1], &root) == -1) return 3;
    while (fgets(line, sizeof(line), stdin)) {
        if (sscanf(line, "%15s %299s %299s", op, net, target) != 3) return 4;
        fd = -1;
#ifdef LEGACY_BRIDGE
        /* Deliberately legacy #status window: semantic RED, not new API. */
        if (!strcmp(op, "ni")) rc = gin_root_init(&root, net, "#status", "#&+!");
        else return 5;
#else
        if (!strcmp(op, "u755") || !strcmp(op, "u770") ||
            !strcmp(op, "l755") || !strcmp(op, "l770")) {
            if (fchmod(root.rootfd, op[2] == '5' ? 0755 : 0770) == -1)
                return 11;
            rc = op[0] == 'u' ? gin_account_root_init(&root, net, target, "#&+!") :
                gin_root_init(&root, net, target, "#&+!");
            saved = errno;
            if (!locked(root.lockfd)) return 9;
            if (fchmod(root.rootfd, 0700) == -1) return 12;
            errno = saved;
        }
        else if (!strcmp(op, "ni")) rc = gin_account_network_init(&root, net);
        else if (!strcmp(op, "wi")) rc = gin_account_root_init(&root, net, target, "#&+!");
        else if (!strcmp(op, "nr")) rc = fd = gin_account_network_input_open(&root, net);
        else if (!strcmp(op, "no")) rc = fd = gin_account_network_output_open(&root, net);
        else if (!strcmp(op, "wr")) rc = fd = gin_account_root_input_open(&root, net, target, "#&+!");
        else if (!strcmp(op, "wo")) rc = fd = gin_account_root_output_open(&root, net, target, "#&+!");
        else if (!strcmp(op, "wd")) rc = fd = gin_account_root_window_open(&root, net, target, "#&+!");
        else return 5;
#endif
        saved = errno;
        flags = fd == -1 ? 0 : fcntl(fd, F_GETFL);
        if (fd != -1) {
            if (!strcmp(op, "no") || !strcmp(op, "wo")) {
                if (write(fd, "prefix\n", 7) != 7) return 6;
            }
            if (!(fcntl(fd, F_GETFD) & FD_CLOEXEC)) return 7;
            if (close(fd) == -1) return 8;
        }
        if (!locked(root.lockfd)) return 9;
        printf("%d %d %d\n", rc < 0 ? -1 : 0, saved, flags);
        fflush(stdout);
    }
    if (gin_root_release(&root) == -1) return 10;
    return released(argv[1]) ? 0 : 13;
}
