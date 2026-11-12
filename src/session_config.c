#include "gin_session.h"
#include "gin_fs.h"
#include "gin_http.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define SESSION_CONFIG_LIMIT 16384u

struct config_field {
    const char *key;
    size_t offset;
    size_t capacity;
    long maximum;
};

/* Offsets refer only to owned members of this exact settings value. */
static const struct config_field fields[] = {
    {"root", offsetof(struct gin_session_settings, local.root), 4096, 0},
    {"network", offsetof(struct gin_session_settings, local.network), 256, 0},
    {"target", offsetof(struct gin_session_settings, local.target), 257, 0},
    {"chantypes", offsetof(struct gin_session_settings, local.chantypes), 5, 0},
    {"origin", offsetof(struct gin_session_settings, origin), GIN_SESSION_ORIGIN_MAX + 1u, 0},
    {"mode", offsetof(struct gin_session_settings, mode), GIN_SESSION_MODE_MAX + 1u, 0},
    {"ca_file", offsetof(struct gin_session_settings, ca_file), GIN_SESSION_PATH_MAX + 1u, 0},
    {"connect_ms", offsetof(struct gin_session_settings, connect_ms), 0, GIN_SESSION_REQUEST_MAX},
    {"total_ms", offsetof(struct gin_session_settings, total_ms), 0, GIN_SESSION_REQUEST_MAX},
    {"join_ms", offsetof(struct gin_session_settings, join_ms), 0, GIN_SESSION_REQUEST_MAX},
    {"startup_ms", offsetof(struct gin_session_settings, startup_ms), 0, GIN_SESSION_STARTUP_MAX}
};

void
gin_session_settings_init(struct gin_session_settings *settings)
{
    if (settings == NULL)
        return;
    memset(settings, 0, sizeof(*settings));
    memcpy(settings->local.chantypes, "#", 2);
    settings->connect_ms = GIN_SESSION_CONNECT_MS;
    settings->total_ms = GIN_SESSION_TOTAL_MS;
    settings->join_ms = GIN_SESSION_JOIN_MS;
    settings->startup_ms = GIN_SESSION_STARTUP_MS;
}

static int
valid_text(const char *data, size_t length)
{
    size_t i, column = 0;
    for (i = 0; i < length; ++i) {
        unsigned char byte = (unsigned char)data[i];
        if (byte == '\r') {
            if (i + 1 == length || data[i + 1] != '\n')
                return 0;
        } else if (byte == '\n') {
            column = 0;
        } else if ((byte < 32 && byte != '\t') || byte == 127 || ++column > 8192u) {
            return 0;
        }
    }
    return 1;
}

static char *
trim(char *text)
{
    char *end;
    text += strspn(text, " \t");
    end = text + strlen(text);
    while (end > text && (end[-1] == ' ' || end[-1] == '\t'))
        --end;
    *end = '\0';
    return text;
}

static int
number(const char *text, long maximum, long *out)
{
    long value = 0;
    const unsigned char *p = (const unsigned char *)text;
    if (*p < '1' || *p > '9')
        return -1;
    while (*p != '\0') {
        long digit;
        if (*p < '0' || *p > '9')
            return -1;
        digit = (long)(*p++ - '0');
        if (value > (maximum - digit) / 10)
            return -1;
        value = value * 10 + digit;
    }
    *out = value;
    return 0;
}

static int
parse(char *data, struct gin_session_settings *settings)
{
    char *line = data;
    unsigned seen = 0;
    while (*line != '\0') {
        char *next = strchr(line, '\n');
        char *key, *value, *equals;
        size_t i, length;
        if (next != NULL) {
            *next++ = '\0';
            length = strlen(line);
            if (length && line[length - 1] == '\r')
                line[length - 1] = '\0';
        }
        key = trim(line);
        if (*key != '\0' && *key != '#') {
            equals = strchr(key, '=');
            if (equals == NULL)
                return 2;
            *equals = '\0';
            key = trim(key);
            value = trim(equals + 1);
            for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
                if (strcmp(key, fields[i].key) == 0)
                    break;
            if (i == sizeof(fields) / sizeof(fields[0]) || (seen & (1u << i)))
                return 2;
            seen |= 1u << i;
            if (fields[i].maximum) {
                long *destination = (long *)((unsigned char *)settings + fields[i].offset);
                if (number(value, fields[i].maximum, destination) == -1)
                    return 2;
            } else {
                char *destination = (char *)settings + fields[i].offset;
                length = strlen(value);
                if ((!length && i != 3u) || length >= fields[i].capacity)
                    return 2;
                if ((i == 4u && !gin_http_origin_valid(value)) ||
                    (i == 5u && strcmp(value, "plain") != 0))
                    return 2;
                memcpy(destination, value, length + 1);
                if (i == 3u)
                    settings->local.chantypes_present = 1;
            }
        }
        if (next == NULL)
            break;
        line = next;
    }
    return 0;
}

int
gin_session_config_read(const char *path, struct gin_session_settings *settings)
{
    struct gin_session_settings temporary;
    char data[SESSION_CONFIG_LIMIT + 2u];
    struct stat st;
    size_t used = 0;
    int fd, result = 1, saved;
    if (settings == NULL)
        return 2;
    memset(settings, 0, sizeof(*settings));
    if (path == NULL)
        return 2;
    gin_session_settings_init(&temporary);
    do {
        fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1)
        return 1;
    if (fstat(fd, &st) == -1)
        goto done;
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 ||
        (st.st_mode & 07777) != 0600) {
        errno = EPERM;
        goto done;
    }
    for (;;) {
        ssize_t count = read(fd, data + used, SESSION_CONFIG_LIMIT + 1u - used);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            goto done;
        }
        if (!count)
            break;
        used += (size_t)count;
        if (used > SESSION_CONFIG_LIMIT) {
            result = 2;
            goto done;
        }
    }
    data[used] = '\0';
    result = valid_text(data, used) ? parse(data, &temporary) : 2;
 done:
    saved = errno;
    if (close(fd) == -1 && result == 0)
        result = 1;
    errno = saved;
    if (result == 0)
        *settings = temporary;
    return result;
}

int
gin_session_settings_valid(const struct gin_session_settings *settings)
{
    size_t i;
    if (settings == NULL)
        return 0;
    for (i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        if (fields[i].capacity && !memchr((const char *)settings + fields[i].offset,
            '\0', fields[i].capacity))
            return 0;
    if (!settings->local.root[0] || !settings->local.network[0] ||
        !settings->local.target[0] || !settings->origin[0] ||
        strcmp(settings->mode, "plain") != 0 || !gin_http_origin_valid(settings->origin))
        return 0;
    if (settings->connect_ms <= 0 || settings->connect_ms > GIN_SESSION_REQUEST_MAX ||
        settings->total_ms < settings->connect_ms || settings->total_ms > GIN_SESSION_REQUEST_MAX ||
        settings->join_ms <= 0 || settings->join_ms > GIN_SESSION_REQUEST_MAX ||
        settings->startup_ms <= 0 || settings->startup_ms > GIN_SESSION_STARTUP_MAX)
        return 0;
    return gin_validate(settings->local.root, settings->local.network,
        settings->local.target, settings->local.chantypes) == 0;
}
