#ifndef GIN_TOPOLOGY_INTERNAL_H
#define GIN_TOPOLOGY_INTERNAL_H
#include "gin_topology.h"
/* Internal completed-HTTP error classifier. 401/429 are status-based. A 403
 * is SCOPE only for a bounded, fully valid JSON error client_token_scope;
 * malformed/oversized/deep error JSON remains FORBIDDEN. Never log peer data.
 * Parser owner supplies this helper using the same bounded JSON loader. */
enum gin_topology_code gin_topology_http_status(long status,
    const unsigned char *body, size_t length);
#endif
