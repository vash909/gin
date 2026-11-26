#include "account_fs.h"
#include "account_naming.h"
#include "fs_private.h"
int gin_account_root_init(const struct gin_root *r, const char *n, const char *t, const char *c)
{
    return gin_root_init_named(r, n, t, c, gin_account_name_prepare);
}
int gin_account_root_input_open(const struct gin_root *r, const char *n, const char *t, const char *c)
{
    return gin_root_input_open_named(r, n, t, c, gin_account_name_prepare);
}
int gin_account_root_output_open(const struct gin_root *r, const char *n, const char *t, const char *c)
{
    return gin_root_output_open_named(r, n, t, c, gin_account_name_prepare);
}
int gin_account_root_window_open(const struct gin_root *r, const char *n, const char *t, const char *c)
{
    return gin_root_window_open_named(r, n, t, c, gin_account_name_prepare);
}
int gin_account_network_init(const struct gin_root *r, const char *n)
{
    return gin_root_network_init(r, n);
}
int gin_account_network_input_open(const struct gin_root *r, const char *n)
{
    return gin_root_network_input_open(r, n);
}
int gin_account_network_output_open(const struct gin_root *r, const char *n)
{
    return gin_root_network_output_open(r, n);
}
