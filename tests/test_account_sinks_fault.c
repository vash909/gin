/* Private-TU characterization written after the integration RED/GREEN. */
#include "account_sinks.h"
#include "account_fs.h"
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

static int fail_malloc;
static int fail_init;
static int fail_open;
static int open_calls;
static int move_zero;
static int fail_close;
static int tracked[256];
static size_t tracked_count;

static void *
guarded_malloc(size_t size)
{
    if (fail_malloc) {
        errno = ENOMEM;
        return NULL;
    }
    return malloc(size);
}
static int
guarded_close(int fd)
{
    int rc = close(fd);
    if (fail_close) {
        fail_close = 0;
        errno = EINTR;
        return -1;
    }
    return rc;
}
static int
record_open(int fd)
{
    if (fd >= 0) {
        if (move_zero) {
            int saved;
            move_zero = 0;
            saved = dup2(fd, 0);
            if (saved < 0) {
                close(fd);
                return -1;
            }
            if (fd != 0)
                close(fd);
            if (fcntl(0, F_SETFD, FD_CLOEXEC) < 0) {
                close(0);
                return -1;
            }
            fd = saved;
        }
        if (tracked_count < sizeof(tracked) / sizeof(tracked[0]))
            tracked[tracked_count++] = fd;
    }
    return fd;
}
static int
opening_fails(void)
{
    ++open_calls;
    if (fail_open && open_calls == fail_open) {
        errno = EIO;
        return 1;
    }
    return 0;
}
static int
guarded_network_init(const struct gin_root *root, const char *slug)
{
    if (fail_init) {
        errno = EIO;
        return -1;
    }
    return gin_account_network_init(root, slug);
}
static int
guarded_network_input(const struct gin_root *root, const char *slug)
{
    if (opening_fails())
        return -1;
    return record_open(gin_account_network_input_open(root, slug));
}
static int
guarded_network_output(const struct gin_root *root, const char *slug)
{
    if (opening_fails())
        return -1;
    return record_open(gin_account_network_output_open(root, slug));
}
static int
guarded_root_input(const struct gin_root *root, const char *slug,
                   const char *target, const char *chantypes)
{
    if (opening_fails())
        return -1;
    return record_open(gin_account_root_input_open(root, slug, target, chantypes));
}
static int
guarded_root_output(const struct gin_root *root, const char *slug,
                    const char *target, const char *chantypes)
{
    if (opening_fails())
        return -1;
    return record_open(gin_account_root_output_open(root, slug, target, chantypes));
}
#define malloc guarded_malloc
#define close guarded_close
#define gin_account_network_init guarded_network_init
#define gin_account_network_input_open guarded_network_input
#define gin_account_network_output_open guarded_network_output
#define gin_account_root_input_open guarded_root_input
#define gin_account_root_output_open guarded_root_output
#include "../src/account_sinks.c"
#undef malloc
#undef close
#undef gin_account_network_init
#undef gin_account_network_input_open
#undef gin_account_network_output_open
#undef gin_account_root_input_open
#undef gin_account_root_output_open

#define main frozen_integration_main
#include "test_account_sinks.c"
#undef main

static void
closed_owned(void)
{
    size_t i;
    for (i = 0; i < tracked_count; ++i)
        CHECK(fcntl(tracked[i], F_GETFD) < 0 && errno == EBADF);
    tracked_count = 0;
    open_calls = 0;
}

static void
characterize(int zero)
{
    struct gin_topology *base = NULL, *topology = NULL;
    struct gin_account_plan *plan = NULL;
    struct gin_credential credential = {
        "https://fixture.invalid", "SyntheticBearer",
        "12345678-1234-1234-1234-123456789abc", "SyntheticName"
    };
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_account_sinks *sinks = NULL;
    struct gin_account_sinks_result result;
    const char *tmp = getenv("TMPDIR");
    char path[4096], before[8192], after[8192], prefix[8];
    struct stat st;
    int fd, count;
    ssize_t length;
    alarm(15);
    if (!tmp || !*tmp)
        tmp = ".build";
    CHECK(snprintf(path, sizeof(path), "%s/fault-XXXXXX", tmp) > 0);
    CHECK(mkdtemp(path));
    CHECK(gin_topology_parse_boot((const unsigned char *)boot, strlen(boot), "#", &base) == GIN_TOPOLOGY_OK);
    CHECK(gin_topology_apply_queries(base, (const unsigned char *)queries, strlen(queries), &topology) == GIN_TOPOLOGY_OK);
    CHECK(gin_account_plan_new(topology, "#", "SyntheticName", &plan) == GIN_ACCOUNT_PLAN_OK);
    CHECK(gin_root_acquire(path, &root) == 0);
    count = fdcount();
    result = gin_account_sinks_new(&root, &credential, plan, "&", &sinks);
    CHECK(result.code == GIN_ACCOUNT_SINKS_INVALID && !result.binding_attempted && !sinks);
    CHECK(fstatat(root.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    CHECK(fstatat(root.rootfd, "one", &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    fail_malloc = 1;
    result = gin_account_sinks_new(&root, &credential, plan, "#", &sinks);
    fail_malloc = 0;
    CHECK(result.code == GIN_ACCOUNT_SINKS_NOMEM && !result.binding_attempted && !sinks);
    CHECK(fstatat(root.rootfd, ".gin.account", &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    CHECK(fdcount() == count);
    fail_init = 1;
    result = gin_account_sinks_new(&root, &credential, plan, "#", &sinks);
    fail_init = 0;
    CHECK(result.code == GIN_ACCOUNT_SINKS_IO && result.binding_attempted && result.binding.created && !sinks && errno == EIO);
    CHECK(fdcount() == count);
    CHECK(fstatat(root.rootfd, "one", &st, AT_SYMLINK_NOFOLLOW) < 0 && errno == ENOENT);
    fd = openat(root.rootfd, ".gin.account", O_RDONLY | O_NOFOLLOW);
    CHECK(fd >= 0);
    length = read(fd, before, sizeof(before));
    CHECK(length > 0 && close(fd) == 0);
    lease(path, 1);
    result = gin_account_sinks_new(&root, &credential, plan, "#", &sinks);
    CHECK(result.code == GIN_ACCOUNT_SINKS_OK && !result.binding.created && sinks);
    CHECK(write(gin_account_sinks_network_output(sinks, 0), "prefix", 6) == 6);
    CHECK(gin_account_sinks_close(sinks) == 0);
    sinks = NULL;
    closed_owned();
    fail_open = 3;
    fail_close = 1;
    move_zero = zero;
    result = gin_account_sinks_new(&root, &credential, plan, "#", &sinks);
    CHECK(result.code == GIN_ACCOUNT_SINKS_IO && !sinks && result.cleanup_failed && errno == EIO);
    CHECK(open_calls == 3 && tracked_count == 2);
    if (zero)
        CHECK(tracked[0] == 0);
    closed_owned();
    fail_open = 0;
    CHECK(fdcount() == count - (zero ? 1 : 0));
    fd = openat(root.rootfd, "one/out", O_RDONLY | O_NOFOLLOW);
    CHECK(fd >= 0 && read(fd, prefix, sizeof(prefix)) == 6 && !memcmp(prefix, "prefix", 6) && close(fd) == 0);
    fd = openat(root.rootfd, ".gin.account", O_RDONLY | O_NOFOLLOW);
    CHECK(fd >= 0 && read(fd, after, sizeof(after)) == length && !memcmp(before, after, (size_t)length) && close(fd) == 0);
    lease(path, 1);
    result = gin_account_sinks_new(&root, &credential, plan, "#", &sinks);
    CHECK(result.code == GIN_ACCOUNT_SINKS_OK && sinks);
    fail_close = 1;
    CHECK(gin_account_sinks_close(sinks) == -1 && errno == EINTR);
    sinks = NULL;
    closed_owned();
    lease(path, 1);
    CHECK(gin_root_release(&root) == 0);
    lease(path, 0);
    gin_account_plan_free(plan);
    gin_topology_free(topology);
    gin_topology_free(base);
}

int
main(int argc, char **argv)
{
    pid_t child;
    int probe = sinks_probe_dispatch(argc, argv);
    if (probe >= 0)
        return probe;
    sinks_test_executable = argv[0];
    if (argc == 2 && !strcmp(argv[1], "--sinks-fault-zero")) {
        characterize(1);
        return 0;
    }
    if (argc != 1)
        return 2;
    alarm(40);
    characterize(0);
    CHECK(fflush(NULL) == 0);
    child = fork();
    CHECK(child >= 0);
    if (!child) {
        char *args[] = {argv[0], (char *)"--sinks-fault-zero", NULL};
        alarm(15);
        execvp(sinks_test_executable, args);
        _exit(127);
    }
    sinks_wait_child(child);
    puts("account-sinks fault characterization: ok");
    return 0;
}
