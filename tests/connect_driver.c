#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_connect.h"
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static unsigned events;
static void frame(void *u, const struct gin_phoenix_frame *f, uint64_t epoch)
{ (void)u; assert(f && epoch); events++; }
int main(int argc, char **argv)
{
    struct gin_connect_settings s;
    struct gin_credential c, saved;
    struct gin_connect *session = NULL;
    struct gin_connect_subscription plan[] = {{"Net", "#ROOM"}, {"Net", "#room"}};
    struct sigaction before, after;
    struct stat st;
    enum gin_connect_code code;
    int fd, attempt, attempts;
    ssize_t n;
    assert(argc == 5);
    memset(&c, 0, sizeof(c));
    assert(strlen(argv[1]) <= GIN_CREDENTIAL_ORIGIN_MAX);
    strcpy(c.origin, argv[1]); strcpy(c.subject_name, "Alice");
    strcpy(c.subject_id, "12345678-1234-1234-1234-123456789abc");
    fd = open(argv[3], O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    assert(fd >= 0 && fstat(fd, &st) == 0 && S_ISREG(st.st_mode));
    assert(st.st_uid == geteuid() && st.st_nlink == 1 && (st.st_mode & 07777) == 0600);
    n = read(fd, c.token, sizeof(c.token)); assert(n > 0 && n <= GIN_CREDENTIAL_TOKEN_MAX);
    c.token[n] = 0; assert(close(fd) == 0); saved = c;
    s.http.origin = c.origin; s.http.ca_file = argv[2];
    s.http.connect_ms = 1500; s.http.total_ms = 3000;
    s.policy.client_protocol = 4; s.policy.required_server_protocol = 4; s.join_ms = 800;
    assert(sigaction(SIGPIPE, NULL, &before) == 0);
    assert(gin_http_global_init() == GIN_HTTP_OK);
    assert(gin_connect_new(&s, &c, plan, 2, frame, NULL, &session) == GIN_CONNECT_OK);
    assert(gin_connect_topic_count(session) == 3);
    attempts = (!strcmp(argv[4], "twice") || !strcmp(argv[4], "fresh")) ? 2 : 1;
    for (attempt = 0; attempt < attempts; attempt++) {
        unsigned calls = 0;
        code = gin_connect_start(session);
        while (code == GIN_CONNECT_OK && gin_connect_get_state(session) == GIN_CONNECT_JOINING) {
            code = gin_connect_service(session, 100); assert(++calls < 100);
        }
        if (!strcmp(argv[4], "ready") || !strcmp(argv[4], "twice")) {
            assert(code == GIN_CONNECT_OK);
            assert(gin_connect_get_state(session) == GIN_CONNECT_READY);
        }
        if (!strcmp(argv[4], "events")) {
            unsigned limit;
            assert(gin_connect_get_state(session) == GIN_CONNECT_READY);
            for (limit = 0; events < 2 && limit < 30; limit++)
                assert(gin_connect_service(session, 100) == GIN_CONNECT_OK);
            assert(events == 2);
            assert(gin_connect_service(session, 0) == GIN_CONNECT_OK && events == 2);
        }
        if (!strcmp(argv[4], "idle")) {
            struct timespec start, end;
            clock_t cpu = clock();
            double elapsed, used;
            unsigned i;
            assert(clock_gettime(CLOCK_MONOTONIC, &start) == 0);
            for (i = 0; i < 5; i++) assert(gin_connect_service(session, 50) == GIN_CONNECT_OK);
            assert(clock_gettime(CLOCK_MONOTONIC, &end) == 0);
            elapsed = (double)(end.tv_sec - start.tv_sec) + (double)(end.tv_nsec - start.tv_nsec) / 1e9;
            used = (double)(clock() - cpu) / CLOCKS_PER_SEC;
            assert(elapsed >= .20 && elapsed < 2 && used < .15);
        }
        if (code != GIN_CONNECT_OK) {
            size_t i;
            for (i = 0; i < gin_connect_topic_count(session); i++)
                assert(gin_connect_topic_at(session, i)->state != GIN_CONNECT_TOPIC_JOINED);
        }
        printf("%s %ld %d %llu\n", gin_connect_error(code),
            gin_connect_last_result(session).http_status, gin_connect_get_state(session),
            (unsigned long long)gin_connect_last_result(session).socket_epoch);
        assert(!memcmp(&c, &saved, sizeof(c)));
        gin_connect_close(session);
        for (calls = 0; calls < gin_connect_topic_count(session); calls++) {
            assert(gin_connect_topic_at(session, calls)->state != GIN_CONNECT_TOPIC_JOINED);
            assert(gin_connect_topic_at(session, calls)->join_ref == NULL);
        }
    }
    gin_connect_free(session); gin_http_global_cleanup();
    assert(sigaction(SIGPIPE, NULL, &after) == 0 && before.sa_handler == after.sa_handler);
    return 0;
}
