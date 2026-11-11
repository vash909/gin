#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_control.h"
#include "gin_command.h"
#include "gin_fs.h"
#include "gin_input.h"
#include <assert.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <errno.h>
int main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_credential c = {0};
    struct gin_send_settings s;
    struct gin_send_route route = {"Net", "#ROOM", "#&+!", GIN_SEND_ROLE_CHANNEL, GIN_SEND_MODE_PLAIN};
    struct gin_input input = GIN_INPUT_INITIALIZER;
    struct gin_record record;
    unsigned count = 0, loops = 0, expected;
    int fd;
    assert(argc == 6);
    assert(strlen(argv[1]) < sizeof(c.origin)); strcpy(c.origin, argv[1]);
    strcpy(c.subject_name, "Alice"); strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    strcpy(c.token, "aa~");
    s.auth.origin = argv[1]; s.auth.ca_file = argv[2];
    s.auth.connect_ms = 5000; s.auth.total_ms = 10000;
    s.protocol.client_protocol = 4; s.protocol.required_server_protocol = 1;
    if (!strcmp(argv[4], "origin")) c.origin[8] = c.origin[8] == '1' ? '2' : '1';
    if (!strcmp(argv[4], "mode")) route.mode = GIN_SEND_MODE_ENCRYPTED;
    if (!strcmp(argv[4], "unknown-mode")) route.mode = GIN_SEND_MODE_UNKNOWN;
    if (!strcmp(argv[4], "unknown")) route.role = GIN_SEND_ROLE_UNKNOWN;
    if (!strcmp(argv[4], "query")) route.role = GIN_SEND_ROLE_QUERY;
    if (!strcmp(argv[4], "deadline")) s.auth.total_ms = GIN_HTTP_TIMEOUT_MAX + 1;
    if (!strcmp(argv[4], "policy")) s.protocol.client_protocol = 0;
    if (!strcmp(argv[4], "timeout")) s.auth.total_ms = 5000;
    assert(gin_root_acquire(argv[3], &root) == 0);
    assert(gin_root_init(&root, "Net", "#ROOM", "#") == 0);
    fd = gin_root_input_open(&root, "Net", "#ROOM", "#"); assert(fd >= 0);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    expected = (unsigned)strtoul(argv[5], NULL, 10);
    if (!strcmp(argv[4], "bad-lock")) assert(fchmod(root.lockfd, 0640) == 0);
    if (!strcmp(argv[4], "bad-root")) assert(fchmod(root.rootfd, 0750) == 0);
    puts("ready"); assert(fflush(stdout) == 0);
    while (count < expected && ++loops < 3000) {
        enum gin_input_result ir = gin_input_next(&input, fd, &record);
        if (ir == GIN_INPUT_LINE) {
            struct gin_command command;
            enum gin_command_code code = gin_command_parse(record.data, record.length, route.chantypes, &command);
            if (!strcmp(argv[4], "native")) {
                command.kind = GIN_COMMAND_JOIN; command.length = 5;
                memcpy(command.data, "#ROOM", 6); code = GIN_COMMAND_OK;
            }
            if (code == GIN_COMMAND_OK && command.kind == GIN_COMMAND_SEND &&
                !strcmp(argv[4], "literal")) {
                struct gin_send_result r = gin_send_plain(&root, &s, &c, &route,
                    (uint64_t)count + 101, command.data, command.length);
                printf("send %d %d %llu %ld\n", r.code, r.outcome,
                    (unsigned long long)r.request_id, r.http_status);
            } else if (code != GIN_COMMAND_OK || (command.kind != GIN_COMMAND_JOIN && command.kind != GIN_COMMAND_PART)) {
                puts("rejected");
            } else {
                uint64_t id = !strcmp(argv[4], "id") ? 0 : (uint64_t)count + 101;
                struct gin_control_result r;
                struct gin_send_route control_route = route;
                control_route.wire_target = (const char *)command.data;
                r = (command.kind == GIN_COMMAND_JOIN ? gin_control_join : gin_control_part)(
                    !strcmp(argv[4], "root") ? NULL : &root, &s, &c, &control_route, id);
                assert(r.operation_id == id);
                printf("%d %d %llu %ld %d %d %d\n", r.code, r.outcome,
                    (unsigned long long)r.operation_id, r.http_status, r.protocol, r.auth, r.attempted);
            }
            count++;
        } else if (ir == GIN_INPUT_REJECTED || ir == GIN_INPUT_EMPTY) {
            puts("rejected"); count++;
        } else {
            struct pollfd p = {fd, POLLIN, 0};
            assert(ir != GIN_INPUT_ERROR);
            if (ir == GIN_INPUT_EOF) (void)poll(NULL, 0, 10);
            else (void)poll(&p, 1, 10);
        }
        assert(fflush(stdout) == 0);
    }
    assert(count == expected);
    {
        pid_t child = fork(); int status;
        assert(child >= 0);
        if (!child) {
            struct flock lock;
            int blocked;
            memset(&lock, 0, sizeof(lock)); lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
            blocked = fcntl(root.lockfd, F_SETLK, &lock) == -1 &&
                      (errno == EACCES || errno == EAGAIN);
            /* Parent flushed stdout before fork. Clean inherited library globals
             * and run TLS atexit handlers; _exit would create child-only reachable
             * allocations under the unchanged strict Valgrind leak policy. */
            gin_http_global_cleanup();
            exit(blocked ? 0 : 1);
        }
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }
    gin_credential_clear(&c); gin_http_global_cleanup();
    assert(close(fd) == 0); assert(gin_root_release(&root) == 0);
    return 0;
}
