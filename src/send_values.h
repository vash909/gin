#ifndef GIN_SEND_VALUES_H
#define GIN_SEND_VALUES_H

#include "gin_send.h"

/* Pure bounded helper; no I/O, clocks, environment, global allocators or hooks.
 * Strict UTF-8 scalar body, 1..4096 bytes; reject C0 (including CTCP/NUL/CR/LF)
 * and DEL. Preserve valid other UTF-8 and leading slash literally.
 * Require a known CHANNEL/QUERY role matching gin_name_prepare classification;
 * UNKNOWN/ENCRYPTED mode returns UNSUPPORTED, never plaintext fallback. Refuse
 * canonical $server and query targets beginning with #,&,+,! (not DM evidence).
 * Use gin_name_prepare for shared lexical policy, but construct the REST path
 * from exact original slug/target: /networks/SLUG/channels/TARGET/messages for
 * BOTH channel and query destinations. There is no /queries/ send endpoint.
 * RFC3986 unreserved A-Z a-z 0-9 -._~ stay
 * literal; percent-encode every other BYTE as uppercase %HH once per segment.
 * JSON has exactly one string key body, built with Jansson, not visible escapes.
 * path/json must point to fresh NULL slots; all pointers valid and nonaliasing.
 * On success caller owns malloc-compatible path/JSON, length excludes NUL.
 * Path is NUL-terminated, JSON may be; do not rely on a JSON terminator.
 * All outputs unchanged on failure. Allocation/encoding must obey public caps.
 * Explicit allocation failures map to NOMEM; no production global allocator
 * modifications and no promise to identify every internal Jansson OOM. */
enum gin_send_code gin_send_payload_build(const struct gin_send_route *route,
    const unsigned char *body, size_t length, char **path,
    unsigned char **json, size_t *json_length);

/* Pure bounded complete-response decoder. Whole-document UTF-8, duplicate-key
 * rejection (including escaped equivalent keys), depth<=32, bytes<=65536; no
 * embedded NUL, trailing nonwhitespace, or unchecked ignored nested fields.
 * Only 201 and 202 are documented success shapes. Require a JSON object.
 * 201 consumes positive signed-int64 id and exact string network==network_slug,
 * publishing ACCEPTED_PERSISTED and LAST fragment ID. Do not require unused
 * row fields, compare body text, or pretend that ID covers all fragments.
 * 202 consumes exactly boolean ok:true, publishing ACCEPTED_NO_PERSIST and 0.
 * Additive bounded fields are allowed. Unknown 2xx/status, invalid shapes and
 * empty body return MALFORMED. Invalid pointers return INVALID; byte/depth caps
 * return LIMIT. Outputs unchanged on ALL failures. Non-NULL network/id/outcome
 * required; body may be NULL only at length zero. network_slug must be a
 * validated ASCII slug (1..255 letters/digits/underscore/hyphen), consistent
 * with the production naming boundary; invalid slug arguments return INVALID.
 * An allocation-free original-byte cross-check protects consumed id/network/ok
 * semantics if Jansson returns a scratch-corrupted DOM. This does not repair
 * Jansson or independently validate ignored metadata under arbitrary OOM.
 * No logs or raw JSON output. */
enum gin_send_code gin_send_reply_parse(long status,
    const unsigned char *body, size_t length, const char *network_slug,
    int64_t *message_id, enum gin_send_outcome *outcome);

#endif
