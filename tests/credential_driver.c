/* RLIMIT_FSIZE fault injection uses the XSI SIGXFSZ signal. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "gin_credential.h"
#include <stdio.h>
#include <signal.h>
#include <sys/resource.h>
#include <string.h>

static void fixture(struct gin_credential *v)
{
    memset(v, 0, sizeof(*v));
    strcpy(v->origin, "https://example.test:443");
    strcpy(v->token, "Synthetic_token-123.ABC==");
    strcpy(v->subject_id, "01234567-89ab-cdef-0123-456789abcdef");
    strcpy(v->subject_name, "Synthetic user");
}

int main(int argc, char **argv)
{
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_credential value, expected;
    char command[80];
    if (argc != 2 || gin_root_acquire(argv[1], &root) != 0)
        return 2;
    puts("ready");
    fflush(stdout);
    while (fgets(command, sizeof(command), stdin) != NULL) {
        enum gin_credential_code code;
        size_t i;
        int zero = 1, equal = 0;
        fixture(&value);
        fixture(&expected);
        if (strcmp(command, "quit\n") == 0)
            break;
        if (strcmp(command, "clear\n") == 0) {
            gin_credential_clear(&value);
            code = GIN_CREDENTIAL_OK;
        } else if (strcmp(command, "save\n") == 0) {
            code = gin_credential_save(&root, &value);
        } else if (strcmp(command, "replace\n") == 0) {
            strcpy(value.token, "Replacement_synthetic");
            code = gin_credential_save(&root, &value);
        } else if (strcmp(command, "invalid-save\n") == 0) {
            strcpy(value.token, "invalid token");
            code = gin_credential_save(&root, &value);
        } else if (strcmp(command, "write-failure\n") == 0) {
            struct rlimit saved, limited;
            void (*handler)(int);
            if (getrlimit(RLIMIT_FSIZE, &saved) != 0) return 5;
            limited = saved;
            limited.rlim_cur = 1;
            handler = signal(SIGXFSZ, SIG_IGN);
            if (handler == SIG_ERR || setrlimit(RLIMIT_FSIZE, &limited) != 0) return 5;
            code = gin_credential_save(&root, &value);
            if (setrlimit(RLIMIT_FSIZE, &saved) != 0 || signal(SIGXFSZ, handler) == SIG_ERR)
                return 5;
        } else if (strcmp(command, "released\n") == 0) {
            if (gin_root_release(&root) != 0) return 4;
            memset(&value, 0xa5, sizeof(value));
            code = gin_credential_load(&root, expected.origin, &value);
        } else if (strcmp(command, "forget\n") == 0) {
            code = gin_credential_forget(&root);
        } else if (strcmp(command, "load\n") == 0 ||
                   strcmp(command, "other\n") == 0) {
            memset(&value, 0xa5, sizeof(value));
            code = gin_credential_load(&root, command[0] == 'o' ?
                "https://other.test" : expected.origin, &value);
            equal = memcmp(&value, &expected, sizeof(value)) == 0;
        } else if (strncmp(command, "validate", 8) == 0) {
            if (command[8] == '1') strcpy(value.origin, "https://");
            if (command[8] == '2') strcpy(value.token, "abc=def");
            if (command[8] == '3') strcpy(value.subject_id, "bad");
            if (command[8] == '4') strcpy(value.subject_name, "bad\033");
            if (command[8] == '5') memset(value.token, 'a', sizeof(value.token));
            if (command[8] == '6') strcpy(value.subject_name, "\300\200");
            if (command[8] == '7') strcpy(value.origin, "https://x/path");
            if (command[8] == '8') strcpy(value.origin, "https://user@x");
            if (command[8] == '9') strcpy(value.subject_name, "\302\200");
            code = gin_credential_validate(&value);
        } else {
            return 3;
        }
        for (i = 0; i < sizeof(value); ++i)
            if (((unsigned char *)&value)[i] != 0) zero = 0;
        printf("%d %d %d\n", (int)code, zero, equal);
        fflush(stdout);
        gin_credential_clear(&value);
        gin_credential_clear(&expected);
    }
    return gin_root_release(&root) == 0 ? 0 : 4;
}
