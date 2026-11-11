#ifndef GIN_COMMAND_H
#define GIN_COMMAND_H
#include <stddef.h>

#define GIN_COMMAND_RECORD_MAX 4096u
#define GIN_COMMAND_CHANNEL_MAX 50u
#define GIN_COMMAND_NICK_MAX 128u

enum gin_command_kind {
    GIN_COMMAND_SEND, GIN_COMMAND_JOIN, GIN_COMMAND_PART, GIN_COMMAND_QUERY
};
enum gin_command_code {
    GIN_COMMAND_OK, GIN_COMMAND_EMPTY, GIN_COMMAND_INVALID,
    GIN_COMMAND_UNSUPPORTED, GIN_COMMAND_LIMIT
};
struct gin_command {
    enum gin_command_kind kind;
    size_t length;
    unsigned char data[GIN_COMMAND_RECORD_MAX + 1u];
};

/* Pure record frontend, no I/O or allocation. Input excludes LF, is disjoint
 * from out, and need not have a terminator. Requires explicit valid CHANTYPES.
 * Non-slash input is SEND unchanged; // removes exactly one slash for SEND.
 * Commands are exactly /join SP channel, /part SP channel, /query SP nick,
 * with one ASCII space, lowercase verb and one nonempty argument. No defaults,
 * comma lists, keys/reasons/messages, quoting, arbitrary IRC or shell commands.
 * Strict UTF-8, no ASCII C0/DEL; empty input returns EMPTY. Original wire case
 * is preserved. Command targets additionally obey pinned upstream wire-token
 * rules and the existing canonical filename-component bound. Query spelling
 * is not evidence of an existing peer conversation or plaintext permission.
 * Success owns all bytes and adds data[length]=0; every other result leaves
 * out unchanged. Caller must inspect the code before dispatching anything. */
enum gin_command_code gin_command_parse(const unsigned char *record,
    size_t length, const char *chantypes, struct gin_command *out);
#endif
