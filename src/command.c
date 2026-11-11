#include "gin_command.h"
#include "gin_naming.h"
#include <errno.h>
#include <string.h>

static int chantypes_ok(const char *chantypes)
{
    return chantypes != NULL && strnlen(chantypes, 5u) <= 4u &&
           gin_chantypes_validate(chantypes) == 0;
}

static int utf8_ok(const unsigned char *s, size_t length)
{
    size_t i = 0;
    while (i < length) {
        unsigned char c = s[i];
        unsigned char low, high;
        size_t count, k;
        if (c < 0x80u) {
            if (c < 0x20u || c == 0x7fu)
                return 0;
            i++;
            continue;
        }
        if (c >= 0xc2u && c <= 0xdfu) {
            count = 2; low = 0x80u; high = 0xbfu;
        } else if (c == 0xe0u) {
            count = 3; low = 0xa0u; high = 0xbfu;
        } else if ((c >= 0xe1u && c <= 0xecu) || c == 0xeeu || c == 0xefu) {
            count = 3; low = 0x80u; high = 0xbfu;
        } else if (c == 0xedu) {
            count = 3; low = 0x80u; high = 0x9fu;
        } else if (c == 0xf0u) {
            count = 4; low = 0x90u; high = 0xbfu;
        } else if (c >= 0xf1u && c <= 0xf3u) {
            count = 4; low = 0x80u; high = 0xbfu;
        } else if (c == 0xf4u) {
            count = 4; low = 0x80u; high = 0x8fu;
        } else {
            return 0;
        }
        if (length - i < count)
            return 0;
        if (s[i + 1] < low || s[i + 1] > high)
            return 0;
        for (k = 2; k < count; k++)
            if (s[i + k] < 0x80u || s[i + k] > 0xbfu)
                return 0;
        i += count;
    }
    return 1;
}

static enum gin_command_code classify(const char *arg, const char *chantypes,
                                      enum gin_window_kind want)
{
    char component[256];
    enum gin_window_kind kind;
    errno = 0;
    if (gin_window_encode(arg, chantypes, component, sizeof(component),
                          &kind) != 0)
        return errno == ENAMETOOLONG ? GIN_COMMAND_LIMIT : GIN_COMMAND_INVALID;
    return kind == want ? GIN_COMMAND_OK : GIN_COMMAND_INVALID;
}

static enum gin_command_code check_channel(const unsigned char *arg,
                                           size_t length,
                                           const char *chantypes)
{
    char buf[51];
    size_t i;
    if (length > GIN_COMMAND_CHANNEL_MAX)
        return GIN_COMMAND_LIMIT;
    for (i = 0; i < length; i++)
        if (arg[i] == ',')
            return GIN_COMMAND_INVALID;
    memcpy(buf, arg, length);
    buf[length] = '\0';
    if (length == 1u && (buf[0] == '#' || buf[0] == '&'))
        return strchr(chantypes, buf[0]) != NULL ? GIN_COMMAND_OK
                                                : GIN_COMMAND_INVALID;
    if (length == 1u && (buf[0] == '+' || buf[0] == '!'))
        return GIN_COMMAND_INVALID;
    return classify(buf, chantypes, GIN_WINDOW_CHANNEL);
}

static enum gin_command_code check_nick(const unsigned char *arg, size_t length,
                                        const char *chantypes)
{
    char buf[129];
    size_t i;
    if (length > 128u)
        return GIN_COMMAND_LIMIT;
    if (arg[0] == ':' || arg[0] == '$')
        return GIN_COMMAND_INVALID;
    for (i = 0; i < length; i++) {
        unsigned char c = arg[i];
        if (c == ' ' || c == ',' || c == '!' || c == '@' || c == '.' ||
            c == '*' || c == '?' || c == '#' || c == '&' || c == '+')
            return GIN_COMMAND_INVALID;
    }
    memcpy(buf, arg, length);
    buf[length] = '\0';
    return classify(buf, chantypes, GIN_WINDOW_NONCHANNEL);
}

static int verb_is(const unsigned char *s, size_t length, const char *verb)
{
    size_t vlen = strlen(verb);
    return length == vlen && memcmp(s, verb, vlen) == 0;
}

enum gin_command_code gin_command_parse(const unsigned char *record,
    size_t length, const char *chantypes, struct gin_command *out)
{
    struct gin_command result;
    const unsigned char *data;
    size_t data_length, i, verb_length, arg_length;
    const unsigned char *arg;
    enum gin_command_kind kind;
    enum gin_command_code code;
    if (record == NULL || out == NULL || !chantypes_ok(chantypes))
        return GIN_COMMAND_INVALID;
    if (length > GIN_COMMAND_RECORD_MAX)
        return GIN_COMMAND_LIMIT;
    if (length == 0u)
        return GIN_COMMAND_EMPTY;
    if (!utf8_ok(record, length))
        return GIN_COMMAND_INVALID;
    if (record[0] != '/' || (length >= 2u && record[1] == '/')) {
        data = record;
        data_length = length;
        kind = GIN_COMMAND_SEND;
        if (record[0] == '/') {
            data = record + 1;
            data_length = length - 1;
        }
        memset(&result, 0, sizeof(result));
        result.kind = kind;
        result.length = data_length;
        memcpy(result.data, data, data_length);
        result.data[data_length] = 0;
        *out = result;
        return GIN_COMMAND_OK;
    }
    i = 1;
    while (i < length && record[i] != ' ')
        i++;
    verb_length = i - 1;
    if (verb_is(record + 1, verb_length, "join"))
        kind = GIN_COMMAND_JOIN;
    else if (verb_is(record + 1, verb_length, "part"))
        kind = GIN_COMMAND_PART;
    else if (verb_is(record + 1, verb_length, "query"))
        kind = GIN_COMMAND_QUERY;
    else
        return GIN_COMMAND_UNSUPPORTED;
    if (i >= length)
        return GIN_COMMAND_INVALID;
    arg = record + i + 1;
    arg_length = length - (i + 1);
    if (arg_length == 0u)
        return GIN_COMMAND_INVALID;
    for (i = 0; i < arg_length; i++)
        if (arg[i] == ' ')
            return GIN_COMMAND_INVALID;
    if (kind == GIN_COMMAND_QUERY)
        code = check_nick(arg, arg_length, chantypes);
    else
        code = check_channel(arg, arg_length, chantypes);
    if (code != GIN_COMMAND_OK)
        return code;
    memset(&result, 0, sizeof(result));
    result.kind = kind;
    result.length = arg_length;
    memcpy(result.data, arg, arg_length);
    result.data[arg_length] = 0;
    *out = result;
    return GIN_COMMAND_OK;
}
