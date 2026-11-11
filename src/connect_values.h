#ifndef GIN_CONNECT_VALUES_H
#define GIN_CONNECT_VALUES_H
#include <stddef.h>
#include "gin_credential.h"
#include "gin_phoenix.h"

/* Private allocation-free helpers, not transport or authentication proof.
 * Inputs are bounded NUL-terminated C strings; all inputs/outputs disjoint.
 * Output capacity includes NUL. On ANY failure output and *written unchanged.
 * NULL output/written => INVALID. No I/O, logging, allocation or global state. */
enum gin_ws_value_code { GIN_WS_VALUE_OK, GIN_WS_VALUE_INVALID, GIN_WS_VALUE_LIMIT };
#define GIN_WS_PROTOCOL_CAP (sizeof("phoenix,base64url.bearer.phx.") + ((GIN_CREDENTIAL_TOKEN_MAX + 2u) / 3u) * 4u)
#define GIN_WS_TOPIC_CAP (GIN_PHOENIX_TOPIC_MAX + 1u)

/* Validate existing RFC6750 token grammar (nonempty <=TOKEN_MAX; alphabet
 * A-Z a-z 0-9 - . _ ~ + /, optional '=' suffix, not leading '=').
 * Offer phoenix,base64url.bearer.phx.<STANDARD unpadded Base64(raw bytes)>.
 * Historical prefix is NOT an alphabet specification: pinned Phoenix uses
 * JavaScript btoa(token).replace(/=/g, "") and Base.decode64!(padding:false).
 * Raw slash/plus/tilde are valid; no Bearer prefix, URL-safe substitution or '='
 * encoded padding. Local raw length or capacity overflow => LIMIT. */
enum gin_ws_value_code gin_ws_protocols(const char *token, char *out,
    size_t capacity, size_t *written);

/* Subject is authenticated label, not local nick/path; preserve its case.
 * network==NULL,target==NULL => root; network!=NULL,target==NULL => network;
 * both !=NULL => window. Target without network INVALID.
 * Subject/network: nonempty <=255 UTF-8 bytes, no slash/C0/C1/DEL.
 * Target: nonempty <=256 UTF-8 bytes, no C0/C1/DEL; slash is valid in this last
 * topic segment (upstream split(parts:3)); no filesystem escaping/validation.
 * Only ASCII A-Z fold in target. Preserve other bytes, including []\\^ and
 * non-ASCII; never Unicode/RFC1459 fold, percent-encode or infer a numeric ID.
 * All malformed UTF-8 (overlong/surrogate/out-of-range) INVALID. Overlength or
 * final topic/capacity bound => LIMIT. Space is not a C0 control. */
enum gin_ws_value_code gin_ws_topic(const char *subject, const char *network,
    const char *target, char *out, size_t capacity, size_t *written);
#endif
