#include "render_values.h"

static int64_t gin_floor_div(int64_t a, int64_t b)
{
    int64_t q = a / b;
    int64_t r = a % b;
    if (r != 0 && ((r < 0) != (b < 0))) {
        q -= 1;
    }
    return q;
}

static void gin_write_2(char *out, size_t pos, unsigned int value)
{
    out[pos] = (char)('0' + (int)(value / 10u));
    out[pos + 1u] = (char)('0' + (int)(value % 10u));
}

static void gin_write_3(char *out, size_t pos, unsigned int value)
{
    out[pos] = (char)('0' + (int)(value / 100u));
    out[pos + 1u] = (char)('0' + (int)((value / 10u) % 10u));
    out[pos + 2u] = (char)('0' + (int)(value % 10u));
}

static void gin_write_4(char *out, size_t pos, unsigned int value)
{
    out[pos] = (char)('0' + (int)(value / 1000u));
    out[pos + 1u] = (char)('0' + (int)((value / 100u) % 10u));
    out[pos + 2u] = (char)('0' + (int)((value / 10u) % 10u));
    out[pos + 3u] = (char)('0' + (int)(value % 10u));
}

enum gin_render_value_code gin_render_time(int64_t epoch_ms,
    char output[GIN_RENDER_TIME_CAP])
{
    char tmp[GIN_RENDER_TIME_CAP];
    int64_t seconds;
    int64_t millis;
    int64_t days;
    int64_t sod;
    int64_t z;
    int64_t era;
    int64_t doe;
    int64_t yoe;
    int64_t year;
    int64_t doy;
    int64_t mp;
    int64_t day;
    int64_t month;
    unsigned int hour;
    unsigned int minute;
    unsigned int second;
    size_t i;
    if (output == NULL) {
        return GIN_RENDER_VALUE_INVALID;
    }
    if (epoch_ms < GIN_RENDER_TIME_MIN_MS ||
        epoch_ms > GIN_RENDER_TIME_MAX_MS) {
        return GIN_RENDER_VALUE_RANGE;
    }
    seconds = gin_floor_div(epoch_ms, INT64_C(1000));
    millis = epoch_ms - seconds * INT64_C(1000);
    days = gin_floor_div(seconds, INT64_C(86400));
    sod = seconds - days * INT64_C(86400);
    hour = (unsigned int)(sod / INT64_C(3600));
    minute = (unsigned int)((sod % INT64_C(3600)) / INT64_C(60));
    second = (unsigned int)(sod % INT64_C(60));
    z = days + INT64_C(719468);
    era = gin_floor_div(z, INT64_C(146097));
    doe = z - era * INT64_C(146097);
    yoe = (doe - doe / INT64_C(1460) + doe / INT64_C(36524) -
        doe / INT64_C(146096)) / INT64_C(365);
    year = yoe + era * INT64_C(400);
    doy = doe - (INT64_C(365) * yoe + yoe / INT64_C(4) - yoe / INT64_C(100));
    mp = (INT64_C(5) * doy + INT64_C(2)) / INT64_C(153);
    day = doy - (INT64_C(153) * mp + INT64_C(2)) / INT64_C(5) + INT64_C(1);
    month = mp + (mp < INT64_C(10) ? INT64_C(3) : -INT64_C(9));
    if (month <= INT64_C(2)) {
        year += INT64_C(1);
    }
    gin_write_4(tmp, 0u, (unsigned int)year);
    tmp[4] = '-';
    gin_write_2(tmp, 5u, (unsigned int)month);
    tmp[7] = '-';
    gin_write_2(tmp, 8u, (unsigned int)day);
    tmp[10] = 'T';
    gin_write_2(tmp, 11u, hour);
    tmp[13] = ':';
    gin_write_2(tmp, 14u, minute);
    tmp[16] = ':';
    gin_write_2(tmp, 17u, second);
    tmp[19] = '.';
    gin_write_3(tmp, 20u, (unsigned int)millis);
    tmp[23] = 'Z';
    tmp[24] = '\0';
    for (i = 0u; i < (size_t)GIN_RENDER_TIME_CAP; i++) {
        output[i] = tmp[i];
    }
    return GIN_RENDER_VALUE_OK;
}
