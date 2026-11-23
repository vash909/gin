#ifndef GIN_ACCOUNT_NAMING_H
#define GIN_ACCOUNT_NAMING_H
#include "gin_naming.h"
/* Private opt-in account codec. All pointers nonnull, NUL-terminated inputs,
 * no input/output aliasing; capacities include NUL. Caller owns outputs.
 * Failure leaves every output unchanged. No filesystem or identity mapping. */
int gin_account_name_prepare(const char *, const char *, const char *, struct gin_name *);
int gin_account_window_decode(const char *, const char *, char *, size_t, enum gin_window_kind *);
#endif
