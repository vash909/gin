#include "gin_fs.h"
#include "gin_config.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
    puts("Usage: gin --help | --version\n"
         "       gin init -d ROOT -n NETWORK -c TARGET\n"
         "       gin init  [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET]\n"
         "       gin check [-f FILE] [-d ROOT] [-n NETWORK] [-c TARGET]\n\n"
         "Independent headless file/FIFO frontend for Grappa REST + Phoenix.\n"
         "init initializes local files; check validates settings, read-only.\n"
         "Neither command connects or speaks IRC; check is not a filesystem probe.\n\n"
         "CLI settings override FILE; each flag may occur at most once.\n"
         "All effective root/network/target settings are required. No discovery.\n"
         "FILE: exact keys root/network/target in key=value lines, spaces/tabs\n"
         "trimmed, whole-line # comments, no quoting/interpolation or credentials.\n"
         "FILE must be an effective-user-owned, single-link regular file, exact\n"
         "0600, no final symlink; trust its parents. Relative paths use cwd.\n"
         "Limits (bytes): file 16384, line 8192 excluding LF/CRLF, root 4095,\n"
         "network 255, raw target 256 and encoded filename 255. No truncation.\n\n"
         "init creates ROOT/NETWORK/WINDOW/{in,out}: directories 0700,\n"
         "in FIFO 0600, out regular log 0600 (opened O_APPEND, never truncated).\n"
         "Existing entries must have safe type, exact mode and effective owner.\n"
         "ROOT parents must exist; symlinks and parent traversal are rejected.\n"
         "NETWORK uses ASCII letters, digits, '_' or '-'; spelling is preserved.\n"
         "TARGET folds ASCII A-Z only; #grappa maps to grappa, nick grappa\n"
         "to @grappa. Other bytes outside a-z, 0-9, '_', '-', '.' use %HH.\n"
         "Empty names, dot names, slash, ASCII whitespace/control are rejected.\n"
         "See gin(1) for naming rules and the local security boundary.\n\n"
         "Exit: 0 success (not connectivity), 1 I/O/unsafe file/filesystem failure,\n"
         "2 malformed CLI/configuration/effective settings.\n"
         "Networking, FIFO event loop, Phoenix wire codec and end-to-end\n"
         "encryption are not implemented. run remains unsupported (roadmap 6).");
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
                     strcmp(argv[1], "check") == 0)) {
        const char *root = NULL, *network = NULL, *target = NULL;
        const char *file = NULL;
        struct gin_settings settings = { {0}, {0}, {0} };
        int option, result;

        optind = 2;
        opterr = 0;
        while ((option = getopt(argc, argv, "f:d:n:c:")) != -1) {
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
        if (gin_validate(root, network, target) == -1)
            goto invalid;
        if (strcmp(argv[1], "check") == 0)
            return 0;
        if (gin_init(root, network, target) == -1) {
            perror("gin: init");
            return 1;
        }
        return 0;
    }
invalid:
    fputs("gin: invalid arguments; use --help\n", stderr);
    return 2;
}
