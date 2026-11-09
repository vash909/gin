/* Only gin.c substitutes fputs; the real libc write remains blocking. */
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#ifdef NDEBUG
#undef NDEBUG
#include <assert.h>
#endif
int fixture_fputs(const char *text, FILE *stream)
{
    assert(write(STDOUT_FILENO, "output\n", 7) == 7);
    assert(raise(SIGTERM) == 0);
    return fputs(text, stream);
}
