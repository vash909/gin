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
#include <fcntl.h>
#include <sys/stat.h>
static int controlled_clock(struct timespec *out);
static int reserved_status=-1, reserved_operations=-1;
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
        strcmp(mode, "backoff-input") && strcmp(mode, "startup-expire") && strcmp(mode, "clock-control")) {
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
    const char *mode=getenv("GIN_OWNER_FAULT");
    if (id==CLOCK_MONOTONIC && mode && !strcmp(mode,"clock-control"))
        return controlled_clock(out);
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
#define gin_session_run private_session_run
#define gin_session_run_with_operations private_session_run_with_operations
enum gin_session_code private_session_run_with_operations(
    const struct gin_session_settings *, int, int, int);
#include "../src/session.c"
#undef gin_session_run
#undef gin_session_run_with_operations
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

/* Seam ownership is acquired only after both endpoints and reserved identities
 * validate under owner_begin's established SIGPIPE policy. */
static int command_fd=-1, ack_fd=-1, control_state, control_failed;
static size_t partial_used;
static unsigned char partial[2];
static struct timespec virtual_now;
static int parse_control_fd(const char *name)
{
    const unsigned char *p=(const unsigned char *)getenv(name);
    int fd=0;
    if (!p || *p<'1' || *p>'9') return -1;
    for (; *p; ++p) {
        int d=(int)*p-'0';
        if (d<0 || d>9 || fd>(INT_MAX-d)/10) return -1;
        fd=fd*10+d;
    }
    return fd>=3 ? fd : -1;
}
static int same_pipe(int fd, const struct stat *st)
{
    struct stat other;
    return fd>=0 && fstat(fd,&other)==0 &&
        st->st_dev==other.st_dev && st->st_ino==other.st_ino;
}
static int validate_control(void)
{
    int a=parse_control_fd("GIN_OWNER_CLOCK_COMMAND_FD");
    int b=parse_control_fd("GIN_OWNER_CLOCK_ACK_FD");
    int fa,fb,i;
    int reserved[6];
    struct stat sa,sb;
    if (a<3 || b<3 || a==b) return -1;
    fa=fcntl(a,F_GETFL); fb=fcntl(b,F_GETFL);
    if (fa<0 || fb<0 ||
        ((fa&O_ACCMODE)!=O_RDONLY && (fa&O_ACCMODE)!=O_RDWR) ||
        ((fb&O_ACCMODE)!=O_WRONLY && (fb&O_ACCMODE)!=O_RDWR) ||
        !(fa&O_NONBLOCK) || !(fb&O_NONBLOCK) ||
        fstat(a,&sa)<0 || fstat(b,&sb)<0 || !S_ISFIFO(sa.st_mode) ||
        !S_ISFIFO(sb.st_mode) || same_pipe(b,&sa) || fpathconf(b,_PC_PIPE_BUF)<2)
        return -1;
    reserved[0]=0; reserved[1]=1; reserved[2]=2;
    reserved[3]=reserved_status; reserved[4]=reserved_operations;
    reserved[5]=(int)session_wake;
    for (i=0;i<6;++i)
        if (same_pipe(reserved[i],&sa) || same_pipe(reserved[i],&sb)) return -1;
    command_fd=a; ack_fd=b;
    return 0;
}
static int add_seconds(struct timespec *value, unsigned seconds)
{
    uintmax_t maximum;
    if (sizeof(time_t)>sizeof(uintmax_t) || value->tv_sec<0 ||
        value->tv_nsec<0 || value->tv_nsec>=1000000000) return -1;
    maximum=UINTMAX_MAX;
    if (sizeof(time_t)<sizeof(uintmax_t))
        maximum >>= (sizeof(uintmax_t)-sizeof(time_t))*CHAR_BIT;
    if ((time_t)-1<0) maximum >>= 1;
    if (maximum<seconds || (uintmax_t)value->tv_sec>maximum-seconds) return -1;
    value->tv_sec=(time_t)((uintmax_t)value->tv_sec+seconds);
    return 0;
}
static int controlled_clock(struct timespec *out)
{
    unsigned char bytes[3];
    ssize_t n,w;
    size_t total;
    struct timespec next;
    unsigned char cmd;
    if (control_failed) { errno=EIO; return -1; }
    if (command_fd<0 && validate_control()<0) goto fail;
    /* One bounded read per owner clock, with carry across EAGAIN/EINTR.
     * A three-byte probe rejects coalesced input before any state or ACK. */
    n=read(command_fd,bytes,sizeof(bytes)-partial_used);
    if (n<0 && (errno==EAGAIN || errno==EWOULDBLOCK || errno==EINTR)) {
        if (control_state) { *out=virtual_now; return 0; }
        return clock_gettime(CLOCK_MONOTONIC,out);
    }
    if (n<=0) goto fail;
    total=partial_used+(size_t)n;
    if (total>2) goto fail;
    memcpy(partial+partial_used,bytes,(size_t)n); partial_used=total;
    if (partial[0]!='A' && partial[0]!='B' && partial[0]!='E') goto fail;
    if (total==1) {
        if (control_state) { *out=virtual_now; return 0; }
        return clock_gettime(CLOCK_MONOTONIC,out);
    }
    cmd=partial[0];
    if (partial[1]!='\n' || (cmd=='B' ? control_state!='A' : control_state!=0)) goto fail;
    if (cmd=='B') next=virtual_now;
    else if (clock_gettime(CLOCK_MONOTONIC,&next)<0) goto fail;
    if (add_seconds(&next,cmd=='A'?120u:cmd=='B'?61u:181u)<0) {
        control_failed=1; errno=EOVERFLOW; return -1;
    }
    virtual_now=next; control_state=cmd; partial_used=0;
    w=write(ack_fd,partial,2);
    if (w!=2) goto fail;
    *out=virtual_now; return 0;
fail:
    control_failed=1; errno=EIO; return -1;
}
static enum gin_session_code run_private(const struct gin_session_settings *s,
    int status, int eof, int operations)
{
    enum gin_session_code code;
    int saved;
    reserved_status=status; reserved_operations=operations;
    code=private_session_run_with_operations(s,status,eof,operations);
    saved=errno;
    if (command_fd>=0) { (void)close(command_fd); command_fd=-1; }
    if (ack_fd>=0) { (void)close(ack_fd); ack_fd=-1; }
    errno=saved;
    return code;
}
enum gin_session_code gin_session_run(const struct gin_session_settings *s,
    int status, int eof)
{
    return run_private(s,status,eof,-1);
}
enum gin_session_code gin_session_run_with_operations(const struct gin_session_settings *s,
    int status, int eof, int operations)
{
    return run_private(s,status,eof,operations);
}
