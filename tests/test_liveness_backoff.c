/* Independent pure backoff contract; assertions stay enabled under NDEBUG. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "liveness_values.h"
#include <assert.h>
#include <limits.h>
#include <stddef.h>

int main(void)
{
    unsigned delay, index, base, cap;
    static const unsigned wanted[] = {1000,2000,4000,8000,16000,30000,30000};
    for (index = 1; index <= 7; index++) {
        delay = 777;
        assert(gin_liveness_delay(1000, 30000, index, &delay) == 1);
        assert(delay == wanted[index-1]);
    }
    for (base = 1; base <= 60000; base += 997) {
        for (cap = base; cap <= 60000; cap += 1151) {
            unsigned expected = base;
            for (index = 1; index <= 16; index++) {
                delay = 0;
                assert(gin_liveness_delay(base, cap, index, &delay) == 1);
                assert(delay == expected);
                /* Independent reference stays below 120000 on supported POSIX. */
                expected = expected * 2u < cap ? expected * 2u : cap;
            }
        }
    }
    delay = 777;
    assert(!gin_liveness_delay(0, 10, 1, &delay) && delay == 777);
    assert(!gin_liveness_delay(11, 10, 1, &delay) && delay == 777);
    assert(!gin_liveness_delay(1, 60001, 1, &delay) && delay == 777);
    assert(!gin_liveness_delay(1, 10, 0, &delay) && delay == 777);
    assert(!gin_liveness_delay(1, 10, 17, &delay) && delay == 777);
    assert(!gin_liveness_delay(UINT_MAX, UINT_MAX, UINT_MAX, &delay) && delay == 777);
    assert(!gin_liveness_delay(1, 10, 1, NULL));
    assert(gin_liveness_delay(59999, 60000, 16, &delay) && delay == 60000);
    assert(gin_liveness_delay(60000, 60000, 1, &delay) && delay == 60000);
    assert(gin_liveness_delay(1, 1, 16, &delay) && delay == 1);
    return 0;
}
