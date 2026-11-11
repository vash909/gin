#ifndef GIN_RENDER_VALUES_H
#define GIN_RENDER_VALUES_H
#include <stddef.h>
#include <stdint.h>

/* Local presentation limits, not upstream wire maxima. */
#define GIN_RENDER_ESCAPE_INPUT_MAX 16384u
#define GIN_RENDER_TIME_CAP 25u
#define GIN_RENDER_TIME_MIN_MS (-INT64_C(62135596800000))
#define GIN_RENDER_TIME_MAX_MS INT64_C(253402300799999)

enum gin_render_value_code {
    GIN_RENDER_VALUE_OK, GIN_RENDER_VALUE_INVALID,
    GIN_RENDER_VALUE_LIMIT, GIN_RENDER_VALUE_RANGE
};

/* Pure, deterministic, no allocation/I/O/global state/locale. Inputs stable
 * and disjoint from outputs; outputs non-NULL. NULL input only for length 0.
 * Reject length > INPUT_MAX before reading input. Strict scalar UTF-8: reject
 * overlong, truncated, surrogate, out-of-range or stray continuation bytes.
 * Preserve other valid UTF-8. ASCII quote/backslash -> backslash escaped;
 * C0/DEL -> literal uppercase \xHH. Unicode C1 -> literal uppercase \u00HH.
 * Also \uXXXX escape U+061C, U+200E/F, U+2028/9, U+202A..E, U+2066..9.
 * No raw controls/bidi/separators; not a universal visual-spoofing guarantee.
 * capacity INCLUDES NUL, *written excludes NUL. Preflight validation and size:
 * INVALID for bad pointers/UTF-8; LIMIT for size/capacity; on ANY failure
 * output and *written remain unchanged. Empty success writes NUL, length 0.
 * The length-aware pure helper supports NUL; Phoenix rejects NUL in its JSON.
 */
enum gin_render_value_code gin_render_escape(const unsigned char *input,
    size_t length, char *output, size_t capacity, size_t *written);

/* Epoch MILLISECONDS -> YYYY-MM-DDTHH:MM:SS.mmmZ (24 bytes + NUL).
 * Accept exactly MIN_MS..MAX_MS, proleptic Gregorian UTC years 0001..9999.
 * Negative subsecond values use floor arithmetic, never truncation toward 0.
 * No clock/TZ/locale/time_t dependency, leap-second inference or allocation.
 * NULL output -> INVALID; outside range -> RANGE; ANY failure leaves output
 * unchanged. Caller supplies TIME_CAP accessible bytes; no overlapping inputs.
 */
enum gin_render_value_code gin_render_time(int64_t epoch_ms,
    char output[GIN_RENDER_TIME_CAP]);
#endif
