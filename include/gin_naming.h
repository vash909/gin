#ifndef GIN_NAMING_H
#define GIN_NAMING_H
#include <stddef.h>

/* NUL-terminated input; capacity includes NUL. 0 success, -1 with errno.
 * Outputs are valid only on success. Input and output must not overlap. */
int gin_network_encode(const char *slug, char *out, size_t capacity);
int gin_network_decode(const char *component, char *out, size_t capacity);
enum gin_window_kind { GIN_WINDOW_CHANNEL, GIN_WINDOW_NONCHANNEL };

/* Owned copies: never modify the caller's slug or wire target. This name-only
 * value has no numeric network ID; discovery maps that identity separately.
 * Nonchannel includes synthetic windows; it is not proof of a DM.
 * Treat a prepared name as immutable. */
struct gin_name {
    char input_network_slug[256];
    char original_wire_target[257];
    char canonical_target[257];
    char network_component[256];
    char window_component[256];
    enum gin_window_kind kind;
};

int gin_chantypes_validate(const char *chantypes);
int gin_window_encode(const char *target, const char *chantypes,
                      char *out, size_t capacity, enum gin_window_kind *kind);
/* Decode to canonical Grappa identity, not original wire letter casing. */
int gin_window_decode(const char *component, const char *chantypes,
                      char *out, size_t capacity, enum gin_window_kind *kind);
int gin_name_prepare(const char *slug, const char *target, const char *chantypes,
                     struct gin_name *name);
#endif
