#include "gin_fs.h"
#include "gin_config.h"
#include "gin_runtime.h"
#include "gin_session.h"
#include "gin_http.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
banner(void)
{
    puts("       :####:   ######   ###   ##\n"
         "   ######   ######   ###   ##\n"
         " :##:  .#     ##     ###:  ##\n"
         " ##:          ##     ####  ##\n"
         " ##.          ##     ##:#: ##\n"
         " ##           ##     ## ## ##\n"
         " ##  ####     ##     ## ## ##\n"
         " ##. ####     ##     ## :#:##\n"
         " ##:   ##     ##     ##  ####\n"
         " :##:  ##     ##     ##  :###\n"
         "  #######   ######   ##   ###\n"
         "   :####.   ######   ##   ###\n"
         "\n \342\200\224. .. -. ");
}

static void
usage(void)
{
    puts("Usage: gin --help | --version\n"
         "       gin init -d ROOT -n NETWORK -c TARGET\n"
         "       gin init  [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]\n"
         "       gin check [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]\n"
         "       gin run   [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]\n"
         "       gin connect | connect-check [-f SESSION_FILE] [-d ROOT] [-n NETWORK]\n"
         "           [-c TARGET] [-T CHANTYPES] [-e HTTPS_ORIGIN] [-m plain] [-a CA_FILE]\n"
         "           [-R NONBLOCK_PIPE_FD] [-E]\n"
         "       gin credential-import -d ROOT -e HTTPS_ORIGIN -s PRIVATE_BEARER_FILE\n"
         "           [-a CA_FILE]\n\n"
         "Independent headless file/FIFO frontend for Grappa REST + Phoenix.\n"
         "init initializes local files; check validates settings, read-only.\n"
         "Neither command connects or speaks IRC; check is not a filesystem probe.\n\n"
         "CLI settings override FILE; each flag may occur at most once.\n"
         "All effective root/network/target settings are required. No discovery.\n"
         "FILE: exact keys root/network/target/chantypes in key=value lines, spaces/tabs\n"
         "trimmed, whole-line # comments, no quoting/interpolation or credentials.\n"
         "FILE must be an effective-user-owned, single-link regular file, exact\n"
         "0600, no final symlink; trust its parents. Relative paths use cwd.\n"
         "Limits (bytes): file 16384, line 8192 excluding LF/CRLF, root 4095,\n"
         "network 255, raw target 256 and encoded filename 255. No truncation.\n\n"
         "init creates ROOT/NETWORK/WINDOW/{in,out}: directories 0700,\n"
         "in FIFO 0600, out regular log 0600 (opened O_APPEND, never truncated).\n"
         "init holds ROOT/.gin.lock (regular 0600, POSIX advisory) until exit;\n"
         "a competing writer fails with exit 1. check never probes this lock.\n"
         "Never delete the persistent lock file: close/process death releases\n"
         "ownership. Local cooperative locking, not a distributed/NFS guarantee.\n"
         "Existing entries must have safe type, exact mode and effective owner.\n"
         "ROOT parents must exist; symlinks and parent traversal are rejected.\n"
         "NETWORK uses ASCII letters, digits, '_' or '-'; uppercase is escaped\n"
         "(A -> %41), preserving slug case without filesystem case collisions.\n"
         "-T CHANTYPES (file chantypes): distinct subset of #&+!, default #.\n"
         "An empty value disables channels. #grappa maps to grappa, nick Grappa\n"
         "to @grappa; configured &Foo -> +26foo, +Foo -> +2Bfoo, !Foo -> +21foo.\n"
         "TARGET folds ASCII A-Z only. Strict UTF-8, no Unicode normalization;\n"
         "other bytes outside a-z, 0-9, '_', '-', '.' use uppercase %HH.\n"
         "Empty/dot names, slash, ASCII whitespace, C0/C1 controls/DEL rejected.\n"
         "Pre-beta uppercase network layout changed: use a new root or manually\n"
         "migrate reviewed logs. No automatic rename/delete or original-case recovery.\n"
         "See gin(1) for naming rules and the local security boundary.\n\n"
         "Exit: 0 success (not connectivity), 1 I/O/unsafe file/filesystem failure,\n"
         "2 malformed CLI/configuration/effective settings.\n"
         "run holds one root lock and reads one local FIFO in the foreground.\n"
         "SIGINT/SIGTERM stop cleanly. Empty lines are ignored; invalid records\n"
         "or nonempty complete lines fail with exit 1, without echo or delivery.\n"
         "Local run has no transport. connect owns one verified HTTPS/WSS session,\n"
         "not chat reception, FIFO sending, history, reconnects or encryption.\n"
         "Connected settings add origin/mode/ca_file/connect_ms/total_ms/join_ms/\n"
         "startup_ms; this separate format is not accepted by legacy commands.\n"
         "origin and explicit mode=plain are required; ROOT selects its private\n"
         ".gin.credentials store. credential-import reads exact bearer bytes from\n"
         "an owned0600 single-link regular file; never put secrets in argv/settings.\n"
         "connect-check is lexical/read-only, no root/CA/fd/server probe.\n"
         "-R requires an already nonblocking pipe write fd >=3, reporting READY\\n\n"
         "only after correlated subscriptions and validated route/plain policy.\n"
         "Backpressure fails cleanly; without -R no readiness print. -E explicitly\n"
         "stops on stdin zero-read EOF; stdin must be a nonblocking pipe read end,\n"
         "and any data fails (not commands). Otherwise stdin is ignored.\n"
         "SIGINT/TERM stop after existing synchronous startup phases return; request\n"
         "timeouts are not an immediate cancellation or hard aggregate time bound.");
}

static int
local_input(enum gin_runtime_event event, const struct gin_record *record, void *context)
{
    enum gin_runtime_event *decision = context;
    (void)record;
    if (event == GIN_RUNTIME_READY || event == GIN_RUNTIME_EMPTY)
        return 0;
    *decision = event;
    errno = event == GIN_RUNTIME_LINE ? ENOTSUP : EINVAL;
    return -1;
}

static int
copy_setting(char *destination, size_t capacity, const char *value)
{
    size_t length;
    if (value == NULL)
        return 0;
    length = strlen(value);
    if (length >= capacity)
        return -1;
    memcpy(destination, value, length + 1);
    return 0;
}

static int
status_number(const char *text, int *out)
{
    const unsigned char *p = (const unsigned char *)text;
    unsigned value = 0;
    if (p == NULL || *p < '1' || *p > '9')
        return -1;
    while (*p != '\0') {
        unsigned digit;
        if (*p < '0' || *p > '9')
            return -1;
        digit = (unsigned)(*p++ - '0');
        if (value > ((unsigned)INT_MAX - digit) / 10u)
            return -1;
        value = value * 10u + digit;
    }
    if (value < 3u)
        return -1;
    *out = (int)value;
    return 0;
}

/* A separate grammar preserves the offline commands and legacy settings.
 * Only filenames and nonsecret settings enter argv; bearer bytes never do. */
static int
connected_cli(int argc, char **argv)
{
    enum argument {
        FILE_ARG, ROOT_ARG, NETWORK_ARG, TARGET_ARG, CHANTYPES_ARG,
        ORIGIN_ARG, MODE_ARG, CA_ARG, STATUS_ARG, SOURCE_ARG, EOF_ARG, ARGUMENTS
    };
    const char *values[ARGUMENTS] = {NULL};
    unsigned char seen[ARGUMENTS] = {0};
    struct gin_session_settings settings;
    enum gin_session_code code;
    int importing = strcmp(argv[1], "credential-import") == 0;
    int checking = strcmp(argv[1], "connect-check") == 0;
    int option, index, status_fd = -1, result;

    /* This process owns the new command's diagnostics policy. The library
     * independently saves/restores its caller state. Legacy paths are intact. */
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        return 1;
    optind = 2;
    opterr = 0;
    while ((option = getopt(argc, argv, "f:d:n:c:T:e:m:a:R:s:E")) != -1) {
        switch (option) {
        case 'f': index = FILE_ARG; break;
        case 'd': index = ROOT_ARG; break;
        case 'n': index = NETWORK_ARG; break;
        case 'c': index = TARGET_ARG; break;
        case 'T': index = CHANTYPES_ARG; break;
        case 'e': index = ORIGIN_ARG; break;
        case 'm': index = MODE_ARG; break;
        case 'a': index = CA_ARG; break;
        case 'R': index = STATUS_ARG; break;
        case 's': index = SOURCE_ARG; break;
        case 'E': index = EOF_ARG; break;
        default: goto invalid_connected;
        }
        if (seen[index] || (option != 'E' && *optarg == '\0' && option != 'T'))
            goto invalid_connected;
        if (importing && index != ROOT_ARG && index != ORIGIN_ARG &&
            index != CA_ARG && index != SOURCE_ARG)
            goto invalid_connected;
        if (!importing && index == SOURCE_ARG)
            goto invalid_connected;
        seen[index] = 1;
        values[index] = option == 'E' ? "1" : optarg;
    }
    if (optind != argc)
        goto invalid_connected;
    gin_session_settings_init(&settings);
    if (values[FILE_ARG] != NULL) {
        result = gin_session_config_read(values[FILE_ARG], &settings);
        if (result != 0) {
            fputs(result == 1 ? "gin: cannot safely read connected configuration\n" :
                  "gin: invalid connected configuration\n", stderr);
            return result;
        }
    }
    if (copy_setting(settings.local.root, sizeof(settings.local.root), values[ROOT_ARG]) ||
        copy_setting(settings.local.network, sizeof(settings.local.network), values[NETWORK_ARG]) ||
        copy_setting(settings.local.target, sizeof(settings.local.target), values[TARGET_ARG]) ||
        copy_setting(settings.local.chantypes, sizeof(settings.local.chantypes), values[CHANTYPES_ARG]) ||
        copy_setting(settings.origin, sizeof(settings.origin), values[ORIGIN_ARG]) ||
        copy_setting(settings.mode, sizeof(settings.mode), values[MODE_ARG]) ||
        copy_setting(settings.ca_file, sizeof(settings.ca_file), values[CA_ARG]))
        goto invalid_connected;
    if (seen[CHANTYPES_ARG])
        settings.local.chantypes_present = 1;
    if (importing) {
        if (!seen[SOURCE_ARG] || !gin_http_origin_valid(settings.origin) ||
            gin_validate(settings.local.root, "gin", "credential", "#") == -1)
            goto invalid_connected;
        code = gin_session_import(settings.local.root, settings.origin,
            settings.ca_file[0] ? settings.ca_file : NULL, values[SOURCE_ARG]);
    } else {
        if (!gin_session_settings_valid(&settings) ||
            (seen[STATUS_ARG] && status_number(values[STATUS_ARG], &status_fd) == -1))
            goto invalid_connected;
        if (checking)
            return 0;
        code = gin_session_run(&settings, status_fd, seen[EOF_ARG] != 0);
    }
    if (code == GIN_SESSION_OK)
        return 0;
    /* Owner has released all resources; a blocked diagnostic cannot hold root. */
    fputs("gin: ", stderr);
    fputs(gin_session_error(code), stderr);
    fputc('\n', stderr);
    return code == GIN_SESSION_INVALID ? 2 : 1;

invalid_connected:
    fputs("gin: invalid connected arguments; use --help\n", stderr);
    return 2;
}

int
main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage();
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("gin 0.1.0-dev");
        return 0;
    }
    if (argc > 1 && (strcmp(argv[1], "connect") == 0 ||
                     strcmp(argv[1], "connect-check") == 0 ||
                     strcmp(argv[1], "credential-import") == 0))
        return connected_cli(argc, argv);
    if (argc > 1 && (strcmp(argv[1], "init") == 0 ||
                     strcmp(argv[1], "check") == 0 || strcmp(argv[1], "run") == 0)) {
        const char *root = NULL, *network = NULL, *target = NULL;
        const char *file = NULL, *chantypes = NULL;
        struct gin_settings settings = { {0}, {0}, {0}, {0}, 0 };
        int option, result;

        optind = 2;
        opterr = 0;
        while ((option = getopt(argc, argv, "f:d:n:c:T:")) != -1) {
            switch (option) {
            case 'f':
                if (file != NULL || *optarg == '\0')
                    goto invalid;
                file = optarg;
                break;
            case 'd':
                if (root != NULL)
                    goto invalid;
                root = optarg;
                break;
            case 'n':
                if (network != NULL)
                    goto invalid;
                network = optarg;
                break;
            case 'c':
                if (target != NULL)
                    goto invalid;
                target = optarg;
                break;
            case 'T':
                if (chantypes != NULL)
                    goto invalid;
                chantypes = optarg;
                break;
            default: goto invalid;
            }
        }
        if (optind != argc)
            goto invalid;
        if (file != NULL) {
            result = gin_config_read(file, &settings);
            if (result != 0) {
                fputs(result == 1 ? "gin: cannot safely read configuration\n" :
                      "gin: invalid configuration\n", stderr);
                return result;
            }
        }
        if (root == NULL)
            root = settings.root;
        if (network == NULL)
            network = settings.network;
        if (target == NULL)
            target = settings.target;
        if (chantypes == NULL)
            chantypes = settings.chantypes_present ? settings.chantypes : "#";
        if (gin_validate(root, network, target, chantypes) == -1)
            goto invalid;
        if (strcmp(argv[1], "check") == 0)
            return 0;
        if (strcmp(argv[1], "run") == 0) {
            enum gin_runtime_event decision = GIN_RUNTIME_READY;
            /* Stdio may block: announce intent before owning runtime resources,
             * and report failures only after runtime cleanup. Never claim ready. */
            if (fputs("gin: starting local input; transport unavailable; sending unsupported\n", stderr) == EOF ||
                fflush(stderr) == EOF)
                return 1;
            if (gin_runtime_run(root, network, target, chantypes, local_input, &decision) == -1) {
                fputs(decision == GIN_RUNTIME_LINE ? "gin: sending unsupported; input not delivered\n" :
                      decision == GIN_RUNTIME_REJECTED ? "gin: invalid input record\n" :
                      "gin: local runtime failed\n", stderr);
                return 1;
            }
            return 0;
        }
        if (gin_init(root, network, target, chantypes) == -1) {
            perror("gin: init");
            return 1;
        }
        return 0;
    }
invalid:
    if (argc == 1)
        banner();
    fputs("gin: invalid arguments; use --help\n", stderr);
    return 2;
}
