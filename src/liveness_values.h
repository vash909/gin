#ifndef GIN_LIVENESS_VALUES_H
#define GIN_LIVENESS_VALUES_H
/* Pure capped exponential backoff. No clocks, I/O, allocation or global state.
 * base/cap must satisfy 1 <= base <= cap <= 60000, retry_index is 1..16.
 * Success writes min(cap, base * 2^(retry_index-1)) and returns 1.
 * Invalid arguments return 0 and leave a non-NULL output unchanged.
 * Never evaluate an overflowing multiplication or shift. */
int gin_liveness_delay(unsigned base_ms, unsigned cap_ms, unsigned retry_index,
    unsigned *delay_ms);
#endif
