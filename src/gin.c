#include "gin_fs.h"
#include "gin_config.h"
#include "gin_runtime.h"

#include <errno.h>
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
         "       gin run   [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET] [-T CHANTYPES]\n\n"
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
         "Networking, sending, Phoenix and encryption are not implemented.");
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
