#ifndef GIN_CONFIG_H
#define GIN_CONFIG_H

struct gin_settings {
    char root[4096];
    char network[256];
    char target[257];
    char chantypes[5];
    int chantypes_present;
};

/* Owns and closes its descriptor. Returns 0, I/O/unsafe 1, or malformed 2. */
int gin_config_read(const char *path, struct gin_settings *settings);

#endif
