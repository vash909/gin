/* Private actual-owner clock fault; all other translation units are normal. */
#include "gin_session.h"
#include "gin_input.h"
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
static int fault_armed;
static enum gin_input_result fault_input(struct gin_input *input, int fd,
    struct gin_record *record)
{
    enum gin_input_result result;
    const char *mode = getenv("GIN_OWNER_FAULT");
    if (mode && *mode && strcmp(mode, "clock-max") && strcmp(mode, "pending-stop")) {
        errno = EINVAL; return GIN_INPUT_ERROR;
    }
    result = gin_input_next(input, fd, record);
    if (result == GIN_INPUT_EOF && mode && strcmp(mode, "clock-max") == 0)
        fault_armed = 1;
    return result;
}
static int fault_clock(clockid_t id, struct timespec *out)
{
    if (id == CLOCK_MONOTONIC && fault_armed) {
        uintmax_t maximum = (time_t)-1 > 0 ? (uintmax_t)(time_t)-1 :
            UINTMAX_MAX >> (sizeof(uintmax_t) * CHAR_BIT - sizeof(time_t) * CHAR_BIT + 1);
        fault_armed = 0;
        out->tv_sec = (time_t)maximum; out->tv_nsec = 999999999;
        return 0;
    }
    return clock_gettime(id, out);
}
static ssize_t fault_write(int fd, const void *buffer, size_t length)
{
    ssize_t written = write(fd, buffer, length);
    const char *mode = getenv("GIN_OWNER_FAULT");
    if (written >= 0 && (size_t)written == length && length >= 4u &&
        length <= 192u && !memcmp(buffer, "OP1 ", 4u) &&
        mode && !strcmp(mode, "pending-stop")) {
        char line[193];
        memcpy(line, buffer, length); line[length] = '\0';
        if (strstr(line, " SEND PENDING 0\n")) (void)raise(SIGTERM);
    }
    return written;
}
#define write fault_write
#define gin_input_next fault_input
#define clock_gettime fault_clock
#include "../src/session.c"
#undef write
#undef clock_gettime
#undef gin_input_next
