#include "liveness_values.h"
#include <limits.h>
#include <stddef.h>

int gin_liveness_delay(unsigned base_ms, unsigned cap_ms, unsigned retry_index, unsigned *delay_ms)
{
    unsigned cur;
    unsigned i;

    if (delay_ms == NULL) {
        return 0;
    }
    if (base_ms < 1u || base_ms > 60000u) {
        return 0;
    }
    if (cap_ms < 1u || cap_ms > 60000u) {
        return 0;
    }
    if (base_ms > cap_ms) {
        return 0;
    }
    if (retry_index < 1u || retry_index > 16u) {
        return 0;
    }

    cur = base_ms;
    for (i = 1u; i < retry_index; i++) {
        if (cur >= cap_ms) {
            cur = cap_ms;
            break;
        }
        if (cur > UINT_MAX / 2u) {
            cur = cap_ms;
            break;
        } else {
            unsigned doubled = cur * 2u;
            if (doubled >= cap_ms) {
                cur = cap_ms;
            } else {
                cur = doubled;
            }
        }
        if (cur >= cap_ms) {
            cur = cap_ms;
            /* further iterations stay capped, but continue to keep logic simple */
        }
    }
    if (cur > cap_ms) {
        cur = cap_ms;
    }
    *delay_ms = cur;
    return 1;
}
