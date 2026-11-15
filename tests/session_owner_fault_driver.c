/* Private actual-owner clock fault; all other translation units are normal. */
#include "gin_session.h"
#include "gin_input.h"
#include "gin_connect.h"
#include <time.h>
#include <stdint.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
static int fault_armed;
static int clock_eligible;
static int backoff_triggered;
static int startup_clock_state;
static struct timespec startup_clock;
static enum gin_connect_code fault_service(struct gin_connect *connection, unsigned max_wait_ms);
static int fault_poll(struct pollfd *fds, nfds_t count, int timeout);
static enum gin_input_result fault_input(struct gin_input *input, int fd,
    struct gin_record *record)
{
    enum gin_input_result result;
    const char *mode = getenv("GIN_OWNER_FAULT");
    if (mode && *mode && strcmp(mode, "clock-max") && strcmp(mode, "pending-stop") &&
        strcmp(mode, "backoff-stop") && strcmp(mode, "backoff-eof") &&
        strcmp(mode, "backoff-input") && strcmp(mode, "startup-expire")) {
        errno = EINVAL; return GIN_INPUT_ERROR;
    }
    result = gin_input_next(input, fd, record);
    if (result == GIN_INPUT_EMPTY &&
        mode && strcmp(mode, "clock-max") == 0) clock_eligible = 1;
    if (result == GIN_INPUT_EOF && clock_eligible &&
        mode && strcmp(mode, "clock-max") == 0)
        fault_armed = 1;
    return result;
}
static int fault_clock(clockid_t id, struct timespec *out)
{
    if (id == CLOCK_MONOTONIC && startup_clock_state) {
        if (startup_clock_state < 0) { errno=EOVERFLOW; return -1; }
        *out=startup_clock; return 0;
    }
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
#define gin_connect_service fault_service
#define poll fault_poll
#define write fault_write
#define gin_input_next fault_input
#define clock_gettime fault_clock
#include "../src/session.c"
#undef write
#undef clock_gettime
#undef gin_input_next
#undef poll
#undef gin_connect_service

/* Only this translation unit interposes the owner's recovery poll. The native
 * connection implementation and production executable have no such hook. */
static int fault_poll(struct pollfd *fds, nfds_t count, int timeout)
{
    const char *mode=getenv("GIN_OWNER_FAULT");
    /* session_wake is the WRITE end; the owner polls its distinct READ end.
     * This TU has one poll call site, reached only by the recovery wait. */
    if (!backoff_triggered && mode && count>0 && count<=2 && session_wake>=0 &&
        fds[0].fd>=0 && fds[0].events==POLLIN &&
        (!strcmp(mode,"backoff-stop") || !strcmp(mode,"backoff-eof") ||
         !strcmp(mode,"backoff-input"))) {
        backoff_triggered=1;
        if (!strcmp(mode,"backoff-stop")) {
            if (raise(SIGTERM)!=0) return -1;
        } else {
            const unsigned char *p=(const unsigned char *)getenv("GIN_OWNER_EOF_FD");
            struct stat st;
            int fd=0, flags;
            if (!p || *p<'1' || *p>'9') { errno=EINVAL; return -1; }
            for (;*p;++p) {
                int digit=(int)*p-'0';
                if (digit<0 || digit>9 || fd>(INT_MAX-digit)/10) { errno=EINVAL; return -1; }
                fd=fd*10+digit;
            }
            flags=fcntl(fd,F_GETFL);
            if (count!=2 || fd<3 || fd==fds[0].fd || fd==fds[1].fd || flags<0 ||
                ((flags&O_ACCMODE)!=O_WRONLY && (flags&O_ACCMODE)!=O_RDWR) ||
                fstat(fd,&st)<0 || !S_ISFIFO(st.st_mode)) { errno=EINVAL; return -1; }
            if (!strcmp(mode,"backoff-input") && write(fd,"x",1)!=1) return -1;
            if (close(fd)<0) return -1;
        }
    }
    return poll(fds,count,timeout);
}

/* This wrapper is after #undef: it calls real native service/clock. Only the
 * owner TU sees the injected time; native retry and transport clocks stay real. */
static enum gin_connect_code fault_service(struct gin_connect *connection, unsigned max_wait_ms)
{
    enum gin_connect_code code=gin_connect_service(connection,max_wait_ms);
    const char *mode=getenv("GIN_OWNER_FAULT");
    if (!startup_clock_state && code!=GIN_CONNECT_OK && mode && !strcmp(mode,"startup-expire")) {
        uintmax_t maximum=(time_t)-1>0 ? (uintmax_t)(time_t)-1 :
            UINTMAX_MAX >> (sizeof(uintmax_t)*CHAR_BIT-sizeof(time_t)*CHAR_BIT+1);
        startup_clock_state=-1;
        if (clock_gettime(CLOCK_MONOTONIC,&startup_clock)==0 && startup_clock.tv_sec>=0 &&
            startup_clock.tv_nsec>=0 && startup_clock.tv_nsec<1000000000 &&
            maximum>=181u && (uintmax_t)startup_clock.tv_sec<=maximum-181u) {
            startup_clock.tv_sec=(time_t)((uintmax_t)startup_clock.tv_sec+181u);
            startup_clock_state=1;
        }
    }
    return code;
}
