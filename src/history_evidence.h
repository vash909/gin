#ifndef GIN_HISTORY_EVIDENCE_H
#define GIN_HISTORY_EVIDENCE_H
#include <stddef.h>
#include <jansson.h>
/* Allocation-free original-byte comparison, independent of lexer scratch.
 * Guards consumed integer/string/null/routing values, not dependency safety. */
int gin_history_evidence(const unsigned char *data, size_t length, const json_t *root);
int gin_history_utf8(const char *text);
#endif
