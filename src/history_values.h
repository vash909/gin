#ifndef GIN_HISTORY_VALUES_H
#define GIN_HISTORY_VALUES_H
#include "gin_history.h"
/* Internal immutable page representation. Constructors own ALL scope strings
 * and every non-NULL row string; freeing any input cannot invalidate a copy.
 * rows[] is one allocated array. owned_bytes includes object/array/string NULs.
 * A merged page is ID ASC with no request provenance; live page exactly1 row.
 * Code outside history_page.c must use public accessors, never mutate a page. */
struct gin_history_page {
    struct gin_history_scope scope;
    struct gin_history_request request;
    enum gin_history_source source;
    size_t count;
    size_t owned_bytes;
    struct gin_history_row *rows;
};
enum gin_history_code gin_history_scope_check(const struct gin_history_scope *scope);
enum gin_history_code gin_history_request_check(const struct gin_history_request *request);
int gin_history_scope_equal(const struct gin_history_scope *a,
    const struct gin_history_scope *b); /* ALL fields including epoch/mode/own nick. */
int gin_history_row_equal(const struct gin_history_row *a,
    const struct gin_history_row *b); /* All consumed fields incl null/presence. */
/* Internal owned constructor over already validated row views. No JSON/OOM
 * fidelity claim applies to arbitrary forged private callers. Revalidates route,
 * text lengths/UTF8/encryption/null semantics, checks all arithmetic and budgets;
 * copy atomic freshNULL output. source REST requires request; others requireNULL.
 * MERGED permits <=4096 rows, strict ID ASC. REST order/anchor/page limit exact.
 * LIVE exactly1 positive-ID row. Used by parser and recovery merge worker. */
enum gin_history_code gin_history_copy_rows(const struct gin_history_scope *scope,
    const struct gin_history_row *const *rows, size_t count,
    enum gin_history_source source, const struct gin_history_request *request,
    struct gin_history_page **out);
#endif
