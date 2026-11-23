#include "account_cli.h"
#include "account_discovery.h"
#include "account_owner.h"
#include "interactive_login.h"
#include "gin_session.h"
#include <signal.h>
#include <stdio.h>
#include <string.h>

static volatile sig_atomic_t account_stop;
static void account_signal(int number) { (void)number; account_stop = 1; }
static int account_tick(void *context, unsigned *wait_ms)
{
    (void)context;
    if (*wait_ms > 50u) *wait_ms = 50u;
    return account_stop != 0;
}

int gin_account_cli(int argc, char **argv)
{
    const int signals[] = {SIGINT,SIGTERM,SIGHUP,SIGQUIT,SIGTSTP,SIGTTIN,SIGTTOU,SIGPIPE};
    struct sigaction saved[8], action;
    sigset_t original, blocked, running;
    size_t installed = 0, i, length;
    const char *root_path = "./irc", *ca = NULL;
    int root_seen = 0, ca_seen = 0, result = 1, initialized = 0, quarantined = 0;
    int diagnostic_failed = 0;
    char origin[GIN_SESSION_ORIGIN_MAX + 1u];
    struct gin_credential credential = {0};
    struct gin_connect_settings settings = {0};
    struct gin_topology *topology = NULL;
    struct gin_account_plan *plan = NULL;
    struct gin_root root = GIN_ROOT_INITIALIZER;
    struct gin_http_control stop = {account_tick, &result, 0};
    struct gin_interactive_result login;
    struct gin_account_discovery_result discovery;
    struct gin_account_owner_input input = {0};
    struct gin_account_owner_result owner;
    if (argc < 3) goto invalid;
    length = strlen(argv[1]);
    if (!length || argv[1][0] == '-' || length > GIN_SESSION_ORIGIN_MAX) goto invalid;
    if (strncmp(argv[1], "https://", 8) == 0) memcpy(origin, argv[1], length + 1);
    else {
        if (strstr(argv[1], "://") || length > sizeof(origin) - 9u) goto invalid;
        memcpy(origin, "https://", 8); memcpy(origin + 8, argv[1], length + 1);
    }
    if (!gin_http_origin_valid(origin)) goto invalid;
    length = strlen(argv[2]);
    if (!length || length > GIN_AUTH_IDENTIFIER_MAX || argv[2][0] == '-') goto invalid;
    for (i = 0; i < length; i++) {
        unsigned char c = (unsigned char)argv[2][i];
        if (c <= 32u || c == 127u) goto invalid;
    }
    for (i = 3; i < (size_t)argc; i += 2) {
        if (i + 1 >= (size_t)argc || !argv[i + 1][0]) goto invalid;
        if (!strcmp(argv[i], "-d") && !root_seen) { root_path = argv[i + 1]; root_seen = 1; }
        else if (!strcmp(argv[i], "-a") && !ca_seen) { ca = argv[i + 1]; ca_seen = 1; }
        else goto invalid;
    }
    if (gin_validate(root_path, "gin", "account", "#&+!") != 0 ||
        (ca && strlen(ca) > GIN_SESSION_PATH_MAX)) goto invalid;
    account_stop = 0;
    if (sigprocmask(SIG_SETMASK, NULL, &original) != 0) goto no_policy;
    blocked = original;
    for (i = 0; i < 8; i++) if (sigaddset(&blocked, signals[i]) != 0) goto no_policy;
    if (sigprocmask(SIG_SETMASK, &blocked, NULL) != 0) goto no_policy;
    memset(&action, 0, sizeof(action)); sigemptyset(&action.sa_mask);
    for (i = 0; i < 8; i++) {
        action.sa_handler = i == 7 ? SIG_IGN : account_signal;
        if (sigaction(signals[i], &action, &saved[i]) != 0) goto cleanup;
        installed++;
    }
    running = original;
    for (i = 0; i < 8; i++) sigdelset(&running, signals[i]);
    if (sigprocmask(SIG_SETMASK, &running, NULL) != 0) goto cleanup;
    settings.http.origin = origin; settings.http.ca_file = ca;
    settings.http.connect_ms = GIN_SESSION_CONNECT_MS;
    settings.http.total_ms = GIN_SESSION_TOTAL_MS;
    settings.policy.client_protocol = GIN_SESSION_CLIENT_PROTOCOL;
    settings.policy.required_server_protocol = GIN_SESSION_SERVER_MIN;
    settings.join_ms = GIN_SESSION_JOIN_MS;
    if (gin_http_global_init() != GIN_HTTP_OK) goto cleanup;
    initialized = 1;
    login = gin_interactive_login(&settings.http, &settings.policy, argv[2], &stop, &credential);
    if (login.reason != GIN_INTERACTIVE_OK) {
        if (login.tty != TP_RESTORE &&
            (account_stop || (login.tty == TP_INTERRUPTED && login.interrupted))) result = 0;
        goto cleanup;
    }
    if (account_stop) { result = 0; goto cleanup; }
    discovery = gin_account_discover(&settings, &credential, "#&+!", &stop, 60000u, &topology);
    quarantined = discovery.quarantined;
    if (discovery.code != GIN_DISCOVERY_OK) {
        if (account_stop && !discovery.cleanup_failed && !quarantined) result = 0;
        goto cleanup;
    }
    if (account_stop) { result = 0; goto cleanup; }
    if (gin_account_plan_new(topology, "#&+!", credential.subject_name, &plan) != GIN_ACCOUNT_PLAN_OK) goto cleanup;
    /* Stdio can block: finish the notice before acquiring the sole root lease.
     * Never emit diagnostics from an owner phase callback under that lease. */
    if (fputs("gin: plaintext receive tracer; channel and known-open query FIFO sending enabled; encrypted content unsupported\n", stderr) == EOF) {
        diagnostic_failed = 1;
        goto cleanup;
    }
    if (account_stop) { result = 0; goto cleanup; }
    if (gin_root_acquire(root_path, &root) != 0) goto cleanup;
    /* Explicit frontend plaintext policy, never inferred from inventory/nick.
     * Existing decoders reject encrypted content. No encryption implementation. */

    input.root = &root; input.plan = plan; input.topology = topology;
    input.settings = &settings; input.credential = &credential;
    input.chantypes = "#&+!"; input.encryption = GIN_RENDER_MODE_PLAIN;
    input.stop = &stop;
    owner = gin_account_owner_run_fifo(&input).owner;
    quarantined = owner.quarantined;
    result = owner.code == GIN_ACCOUNT_OWNER_OK ? 0 : 1;
cleanup:
    if (root.rootfd >= 0 && gin_root_release(&root) != 0) result = 1;
    gin_account_plan_free(plan); gin_topology_free(topology);
    gin_credential_clear(&credential);
    if (initialized && !quarantined) gin_http_global_cleanup();
    if (sigprocmask(SIG_SETMASK, &blocked, NULL) != 0) result = 1;
    while (installed) {
        installed--;
        if (sigaction(signals[installed], &saved[installed], NULL) != 0) result = 1;
    }
    if (sigprocmask(SIG_SETMASK, &original, NULL) != 0) result = 1;
no_policy:
    if (result && !diagnostic_failed) fputs("gin: account receive tracer failed\n", stderr);
    return result;
invalid:
    fputs("gin: invalid account arguments; use --help\n", stderr);
    return 2;
}
