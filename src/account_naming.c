#include "account_naming.h"
#include <errno.h>
#include <string.h>

int
gin_account_name_prepare(const char *slug, const char *target,
                         const char *chantypes, struct gin_name *out)
{
    struct gin_name prepared;
    size_t length;
    if (gin_name_prepare(slug, target, chantypes, &prepared) == -1)
        return -1;
    if (prepared.kind == GIN_WINDOW_CHANNEL && prepared.canonical_target[0] == '#') {
        length = strlen(prepared.window_component);
        if (length >= 255) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memmove(prepared.window_component + 1, prepared.window_component, length + 1);
        prepared.window_component[0] = '#';
    }
    *out = prepared;
    return 0;
}

int
gin_account_window_decode(const char *component, const char *chantypes,
                          char *out, size_t capacity, enum gin_window_kind *kind)
{
    char target[257];
    struct gin_name prepared;
    enum gin_window_kind type;
    size_t length = strlen(component);
    const char *native = component;
    if (length > 255) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (length == 0 || (*component != '#' && *component != '+' && *component != '@')) {
        errno = EINVAL;
        return -1;
    }
    if (*component == '#')
        ++native;
    if (gin_window_decode(native, chantypes, target, sizeof(target), &type) == -1 ||
        gin_account_name_prepare("account", target, chantypes, &prepared) == -1)
        return -1;
    if (strcmp(component, prepared.window_component) != 0) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(prepared.canonical_target);
    if (length >= capacity) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(out, prepared.canonical_target, length + 1);
    *kind = type;
    return 0;
}
