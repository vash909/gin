#include "gin_fs.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void
usage(void)
{
    puts("Usage: gin --help | --version\n"
         "       gin init -d ROOT -n NETWORK -c TARGET\n\n"
         "Independent headless file/FIFO frontend for Grappa REST + Phoenix.\n"
         "This skeleton only initializes local files; it does not speak IRC.\n\n"
         "init creates ROOT/NETWORK/WINDOW/{in,out}: directories 0700,\n"
         "in FIFO 0600, out regular log 0600 (opened O_APPEND, never truncated).\n"
         "Existing entries must have safe type, exact mode and effective owner.\n"
         "ROOT parents must exist; symlinks and parent traversal are rejected.\n"
         "NETWORK uses ASCII letters, digits, '_' or '-'; spelling is preserved.\n"
         "TARGET folds ASCII A-Z only; #grappa maps to grappa, nick grappa\n"
         "to @grappa. Other bytes outside a-z, 0-9, '_', '-', '.' use %HH.\n"
         "Empty names, dot names, slash, ASCII whitespace/control are rejected.\n"
         "See gin(1) for naming limits and the local security boundary.\n\n"
         "Networking, configuration, FIFO event loop, Phoenix wire codec and\n"
         "end-to-end encryption are not implemented.");
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
    if (argc > 1 && strcmp(argv[1], "init") == 0) {
        const char *root = NULL, *network = NULL, *target = NULL;
        int option;

        optind = 2;
        opterr = 0;
        while ((option = getopt(argc, argv, "d:n:c:")) != -1) {
            switch (option) {
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
        if (root == NULL || network == NULL || target == NULL || optind != argc)
            goto invalid;
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
