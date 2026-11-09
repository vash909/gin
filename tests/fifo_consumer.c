/* Test-only consumer; stdin controls when one bounded read is attempted. */
#include "gin_fs.h"
#include "gin_input.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record;
    int fd, result;
    char command;
    size_t i;
    assert(argc == 2);
    alarm(15);
    assert(gin_root_input_open(&root, "Net", "&Foo", "#&") == -1);
    assert(errno == EINVAL);
    assert(gin_root_acquire(argv[1], &root) == 0);
    assert(gin_root_input_open(&root, "unused", "#bad/name", "#") == -1);
    fd = gin_root_input_open(&root, "Net", "&Foo", "#&");
    if (fd == -1) {
        puts("unsafe");
        assert(fflush(stdout) == 0);
        assert(read(STDIN_FILENO, &command, 1) == 1 && command == 'q');
        assert(gin_root_release(&root) == 0);
        return 0;
    }
    assert((fcntl(fd, F_GETFL) & O_NONBLOCK) != 0);
    assert((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
    assert((fcntl(fd, F_GETFD) & FD_CLOEXEC) != 0);
    assert(chdir("/") == 0);
    puts("ready");
    assert(fflush(stdout) == 0);
    while (read(STDIN_FILENO, &command, 1) == 1 && command == 'n') {
        result = gin_input_next(&input, fd, &record);
        printf("%d:", result);
        for (i = 0; i < record.length; ++i)
            printf("%02x", (unsigned int)record.data[i]);
        putchar('\n');
        assert(fflush(stdout) == 0);
    }
    assert(close(fd) == 0);
    assert(gin_root_release(&root) == 0);
    return 0;
}
