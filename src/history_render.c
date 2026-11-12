#include "gin_history.h"
#include <string.h>
enum gin_render_code gin_history_render(const struct gin_history_page *page,
    size_t index, struct gin_render_record **out)
{
    const struct gin_history_scope *scope = gin_history_page_scope(page);
    const struct gin_history_row *row = gin_history_page_row(page, index);
    if (!scope || !row || !out || *out) return GIN_RENDER_INVALID;
    return gin_render_plain_message(row->kind,
        (const unsigned char *)row->sender, strlen(row->sender),
        (const unsigned char *)row->body, row->body ? strlen(row->body) : 0,
        row->server_time, scope->encryption, out);
}
