#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_session.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char base[] = "root=/not-created/gin\nnetwork=NetA\ntarget=#Room\norigin=https://127.0.0.1:443\nmode=plain\n";

static void
set_file(int fd, const char *data, size_t length)
{
    size_t offset = 0;
    assert(ftruncate(fd, 0) == 0);
    assert(lseek(fd, 0, SEEK_SET) == 0);
    while (offset < length) {
        ssize_t written = write(fd, data + offset, length - offset);
        assert(written > 0);
        offset += (size_t)written;
    }
}

static void
zeroed(const struct gin_session_settings *settings)
{
    const unsigned char *bytes = (const unsigned char *)settings;
    size_t i;
    for (i = 0; i < sizeof(*settings); ++i)
        assert(bytes[i] == 0);
}

int
main(void)
{
    struct gin_session_settings s;
    char path[4096], other[4096], data[17000];
    const char *tmp = getenv("TMPDIR");
    int fd, n;
    size_t i, used;
    static const char *const bad[] = {
        "token=never-accepted\n", "mode=plain\n", "root=/duplicate\n",
        "connect_ms=0\n", "connect_ms=01\n", "connect_ms=+1\n",
        "connect_ms=60001\n", "connect_ms=999999999999999999999999999999\n",
        "total_ms=-1\n", "join_ms=60001\n", "startup_ms=600001\n",
        "ca_file=\n", "chantypes=#\nchantypes=#\n", "unknown=value\n"
    };
    gin_session_settings_init(&s);
    assert(s.connect_ms == 1500 && s.total_ms == 3000);
    assert(s.join_ms == 3000 && s.startup_ms == 30000);
    assert(strcmp(s.local.chantypes, "#") == 0);
    assert(!gin_session_settings_valid(&s));
    n = snprintf(path, sizeof(path), "%s/gin-session-config-XXXXXX", tmp ? tmp : ".");
    assert(n > 0 && (size_t)n < sizeof(path));
    fd = mkstemp(path);
    assert(fd >= 0 && fchmod(fd, 0600) == 0);
    set_file(fd, base, strlen(base));
    assert(gin_session_config_read(path, &s) == 0);
    assert(gin_session_settings_valid(&s));
    assert(strcmp(s.local.network, "NetA") == 0 && strcmp(s.local.target, "#Room") == 0);
    assert(strcmp(s.origin, "https://127.0.0.1:443") == 0);
    assert(strcmp(s.mode, "plain") == 0 && s.ca_file[0] == '\0');
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        n = snprintf(data, sizeof(data), "%s%s", base, bad[i]);
        assert(n > 0 && (size_t)n < sizeof(data));
        set_file(fd, data, (size_t)n);
        assert(gin_session_config_read(path, &s) == 2);
        zeroed(&s);
    }
    set_file(fd, "mode=encrypted\n", strlen("mode=encrypted\n"));
    assert(gin_session_config_read(path, &s) == 2);
    zeroed(&s);
    set_file(fd, "origin=http://127.0.0.1\n", strlen("origin=http://127.0.0.1\n"));
    assert(gin_session_config_read(path, &s) == 2);
    zeroed(&s);
    n = snprintf(data, sizeof(data), "%sconnect_ms=10000\ntotal_ms=20000\njoin_ms=12000\nstartup_ms=180000\nchantypes=\n", base);
    assert(n > 0 && (size_t)n < sizeof(data));
    set_file(fd, data, (size_t)n);
    assert(gin_session_config_read(path, &s) == 0 && gin_session_settings_valid(&s));
    assert(s.connect_ms == 10000 && s.total_ms == 20000 && s.join_ms == 12000 && s.startup_ms == 180000);
    assert(s.local.chantypes_present && !s.local.chantypes[0]);
    s.total_ms = 1;
    assert(!gin_session_settings_valid(&s));
    s.total_ms = 20000;
    memset(s.origin, 'x', sizeof(s.origin));
    assert(!gin_session_settings_valid(&s));
    n = snprintf(data, sizeof(data), "%starget=override-duplicate\n", base);
    assert(n > 0);
    set_file(fd, data, (size_t)n);
    assert(gin_session_config_read(path, &s) == 2);
    /* Exact whole-file limit, valid bounded comment lines, then one byte over. */
    used = strlen(base);
    memcpy(data, base, used);
    while (used < 16384) {
        size_t line = 16384 - used;
        if (line > 4096)
            line = 4096;
        data[used] = '#';
        memset(data + used + 1, 'x', line - 1);
        data[used + line - 1] = '\n';
        used += line;
    }
    set_file(fd, data, used);
    assert(gin_session_config_read(path, &s) == 0 && gin_session_settings_valid(&s));
    data[used] = '#';
    set_file(fd, data, used + 1);
    assert(gin_session_config_read(path, &s) == 2);
    zeroed(&s);
    /* Physical line bounds apply even to ignored comments. */
    memset(data, '#', 8193);
    data[8193] = '\n';
    set_file(fd, data, 8194);
    assert(gin_session_config_read(path, &s) == 2);
    zeroed(&s);
    memcpy(data, base, strlen(base));
    data[5] = '\0';
    set_file(fd, data, strlen(base));
    assert(gin_session_config_read(path, &s) == 2);
    zeroed(&s);
    set_file(fd, base, strlen(base));
    assert(fchmod(fd, 0644) == 0);
    assert(gin_session_config_read(path, &s) == 1);
    zeroed(&s);
    assert(fchmod(fd, 0600) == 0);
    n = snprintf(other, sizeof(other), "%s-link", path);
    assert(n > 0 && (size_t)n < sizeof(other));
    assert(link(path, other) == 0);
    assert(gin_session_config_read(path, &s) == 1);
    zeroed(&s);
    assert(unlink(other) == 0);
    assert(symlink(path, other) == 0);
    assert(gin_session_config_read(other, &s) == 1);
    zeroed(&s);
    assert(unlink(other) == 0);
    assert(mkfifo(other, 0600) == 0);
    assert(gin_session_config_read(other, &s) == 1);
    zeroed(&s);
    assert(unlink(other) == 0);
    assert(close(fd) == 0 && unlink(path) == 0);
    puts("session configuration contract passed");
    return 0;
}
