#ifndef GIN_ACCOUNT_FS_H
#define GIN_ACCOUNT_FS_H
#include "gin_fs.h"
/* Private opt-in primitives, NOT account identity or root adoption. Runtime
 * must gate origin/UUID/layout binding first. Nonnull strings. Borrow one held
 * root; close returned owned fds before release. No migration or repair. */
int gin_account_root_init(const struct gin_root *, const char *, const char *, const char *);
int gin_account_root_input_open(const struct gin_root *, const char *, const char *, const char *);
int gin_account_root_output_open(const struct gin_root *, const char *, const char *, const char *);
int gin_account_root_window_open(const struct gin_root *, const char *, const char *, const char *);
int gin_account_network_init(const struct gin_root *, const char *);
int gin_account_network_input_open(const struct gin_root *, const char *);
int gin_account_network_output_open(const struct gin_root *, const char *);
#endif
