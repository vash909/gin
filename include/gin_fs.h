#ifndef GIN_FS_H
#define GIN_FS_H

/* Lexical validation only: no filesystem access. */
int gin_validate(const char *root, const char *network, const char *target);
int gin_init(const char *root, const char *network, const char *target);

#endif
