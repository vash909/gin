/* Private HTTPS cancellation tracer; includes the real synthetic API driver. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "http_control.h"
#include "gin_history.h"
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *control_case;
static volatile sig_atomic_t stopped;
struct timer { uint64_t deadline; unsigned ticks; };
static uint64_t milliseconds(void)
{
    struct timespec now;
    assert(clock_gettime(CLOCK_MONOTONIC,&now)==0 && now.tv_sec>=0);
    return (uint64_t)now.tv_sec*1000u+(uint64_t)now.tv_nsec/1000000u;
}
static void stop_signal(int signum) { (void)signum;stopped=1; }
static int tick(void *opaque, unsigned *wait_ms)
{
    struct timer *timer=opaque;
    timer->ticks++;
    if (stopped || milliseconds()>=timer->deadline) return 1;
    if (*wait_ms>10u) *wait_ms=10u;
    return 0;
}
static struct gin_history_fetch_result controlled_fetch(
    const struct gin_auth_settings *settings,const struct gin_protocol_policy *policy,
    const struct gin_credential *credential,const struct gin_topology *topology,
    const struct gin_history_scope *scope,const struct gin_history_request *request,
    struct gin_history_page **out)
{
    const char *prefix=getenv("GIN_HISTORY_PREFIX");
    int instrumented=prefix && *prefix;
    int abort_expected=strcmp(control_case,"control-ok") && strcmp(control_case,"control-null");
    uint64_t budget=instrumented?3000u:250u;
    struct timer timer={milliseconds()+(!strcmp(control_case,"control-signal")?60000u:budget),0};
    struct gin_http_control control={tick,&timer,0};
    struct gin_history_fetch_result result;
    if (!strcmp(control_case,"control-aborted")) control.aborted=1;
    result=gin_history_fetch_controlled(settings,policy,credential,topology,scope,request,out,
        !strcmp(control_case,"control-null")?NULL:&control);
    if (abort_expected) {
        assert(control.aborted && result.code==GIN_HISTORY_TRANSPORT && out && !*out);
        if (!strcmp(control_case,"control-signal")) assert(stopped);
    } else {
        assert(result.code==GIN_HISTORY_OK && !control.aborted);
        if (strcmp(control_case,"control-null")) assert(timer.ticks>0);
    }
    return result;
}
#define gin_history_fetch controlled_fetch
#define main history_fixture_main
#include "history_http_driver.c"
#undef main
#undef gin_history_fetch
int main(int argc,char **argv)
{
    struct sigaction action;
    assert(argc==6);
    control_case=argv[4];
    assert(!strcmp(control_case,"control-deadline") || !strcmp(control_case,"control-signal") ||
        !strcmp(control_case,"control-ok") || !strcmp(control_case,"control-null") ||
        !strcmp(control_case,"control-aborted"));
    memset(&action,0,sizeof(action));action.sa_handler=stop_signal;
    assert(sigemptyset(&action.sa_mask)==0 && sigaction(SIGTERM,&action,NULL)==0);
    return history_fixture_main(argc,argv);
}
