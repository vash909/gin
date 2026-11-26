#ifndef GIN_CONNECT_HOT_H
#define GIN_CONNECT_HOT_H
#include "gin_connect.h"
/* Private Native-only membership. No query mutation, metadata authority,
 * plaintext permission, output/history/input readiness or delivery guarantee.
 * Single owner, outside callbacks, READY on expected nonzero socket epoch;
 * root and exact existing network must be JOINED. No outstanding query receipt
 * or encoder ownership. Duplicate returns its actual JOINED source, even at cap.
 * New slots append without moving borrowed old views; success enters JOINING.
 * Local rejection preserves output, plan and refs. Operational clock failure
 * uses normal fail-close policy. Submission performs no network I/O. Arguments
 * and output must be disjoint from the session and from each other.
 * Not compatible with the legacy frozen account_receive facade.
 */
enum gin_connect_code gin_connect_subscribe_query_live(struct gin_connect *session,
    const char *network_slug, const char *peer, const char *chantypes,
    uint64_t expected_epoch, size_t *source_out);
#endif
