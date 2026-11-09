/* Darwin hides O_NOFOLLOW with _POSIX_C_SOURCE unless this is enabled. */
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

#include "gin_config.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_LIMIT 16384

/* Validate raw bytes before C-string parsing, including comments and overrides. */
static int
valid_text(const char *data, size_t length)
{
    size_t i, column = 0;

    for (i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)data[i];
        if (c == '\r') {
            if (i + 1 == length || data[i + 1] != '\n')
                return 0;
        } else if (c == '\n') {
            column = 0;
        } else {
            if ((c < 32 && c != '\t') || c == 127 || ++column > 8192)
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
parse(char *data, struct gin_settings *settings)
{
    char *line = data;

    while (*line != '\0') {
        char *next = strchr(line, '\n');
        char *key, *value, *equals, *destination;
        size_t capacity, length;

        if (next != NULL) {
            *next++ = '\0';
            length = strlen(line);
            if (length > 0 && line[length - 1] == '\r')
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
            if (strcmp(key, "root") == 0) {
                destination = settings->root;
                capacity = sizeof(settings->root);
            } else if (strcmp(key, "network") == 0) {
                destination = settings->network;
                capacity = sizeof(settings->network);
            } else if (strcmp(key, "target") == 0) {
                destination = settings->target;
                capacity = sizeof(settings->target);
            } else {
                return 2;
            }
            length = strlen(value);
            if (destination[0] != '\0' || length == 0 || length >= capacity)
                return 2;
            memcpy(destination, value, length + 1);
        }
        if (next == NULL)
            break;
        line = next;
    }
    return 0;
}

int
gin_config_read(const char *path, struct gin_settings *settings)
{
    char data[CONFIG_LIMIT + 2];
    size_t used = 0;
    struct stat st;
    int fd, result = 1, saved;

    /* Opening a FIFO must not wait for a writer; inspect the opened object. */
    do {
        fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    } while (fd == -1 && errno == EINTR);
    if (fd == -1)
        return 1;
    if (fstat(fd, &st) == -1)
        goto done;
    if (!S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink != 1 || (st.st_mode & 07777) != 0600) {
        errno = EPERM;
        goto done;
    }
    for (;;) {
        ssize_t count = read(fd, data + used, CONFIG_LIMIT + 1 - used);
        if (count == -1) {
            if (errno == EINTR)
                continue;
            goto done;
        }
        if (count == 0)
            break;
        used += (size_t)count;
        if (used > CONFIG_LIMIT) {
            result = 2;
            goto done;
        }
    }
    data[used] = '\0';
    result = 0;
done:
    saved = errno;
    /* Do not retry close: after EINTR descriptor ownership is platform-specific. */
    if (close(fd) == -1 && result == 0)
        return 1;
    errno = saved;
    if (result != 0)
        return result;
    return valid_text(data, used) ? parse(data, settings) : 2;
}
