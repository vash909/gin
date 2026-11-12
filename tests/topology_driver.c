/* Native TLS fixture driver; secrets are accepted only from a private file. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_topology.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void wipe(void *p, size_t n)
{
    volatile unsigned char *v = p;
    while (n--) *v++ = 0;
}

static void field(json_t *root, const char *key, char *out, size_t cap)
{
    json_t *v = json_object_get(root, key);
    size_t n;
    assert(json_is_string(v));
    n = json_string_length(v);
    assert(n < cap && !memchr(json_string_value(v), 0, n));
    memcpy(out, json_string_value(v), n);
    out[n] = 0;
}

static void load(const char *path, struct gin_credential *credential)
{
    unsigned char data[GIN_CREDENTIAL_FILE_MAX + 1];
    struct stat s;
    size_t used = 0;
    ssize_t n;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    json_t *root;
    assert(fd >= 0 && fstat(fd, &s) == 0);
    assert(S_ISREG(s.st_mode) && (s.st_mode & 07777) == 0600 &&
        s.st_uid == geteuid() && s.st_nlink == 1);
    assert(s.st_size > 0 && s.st_size <= GIN_CREDENTIAL_FILE_MAX);
    while (used < sizeof(data)) {
        n = read(fd, data + used, sizeof(data) - used);
        if (n < 0 && errno == EINTR) continue;
        assert(n >= 0);
        if (!n) break;
        used += (size_t)n;
    }
    assert(close(fd) == 0 && used <= GIN_CREDENTIAL_FILE_MAX);
    root = json_loadb((const char *)data, used, JSON_REJECT_DUPLICATES, NULL);
    assert(json_is_object(root));
    memset(credential, 0, sizeof(*credential));
    field(root, "origin", credential->origin, sizeof(credential->origin));
    field(root, "token", credential->token, sizeof(credential->token));
    field(root, "subject_id", credential->subject_id, sizeof(credential->subject_id));
    field(root, "subject_name", credential->subject_name, sizeof(credential->subject_name));
    json_decref(root);
    wipe(data, sizeof(data));
}

int main(int argc, char **argv)
{
    struct gin_auth_settings settings;
    struct gin_protocol_policy policy, saved_policy;
    struct gin_credential credential, saved;
    struct gin_topology *out = NULL, *seed = NULL;
    enum gin_topology_code code;
    const char *chantypes = "#&+!", *mode;
    int calls, i;
    size_t j;
    assert(argc == 8);
    settings.origin = argv[1]; settings.ca_file = !strcmp(argv[2], "-") ? NULL : argv[2];
    settings.connect_ms = 300; settings.total_ms = 700;
    load(argv[3], &credential);
    policy.client_protocol = atoi(argv[4]);
    policy.required_server_protocol = atoi(argv[5]);
    mode = argv[6]; calls = atoi(argv[7]);
    assert(calls == 1 || calls == 2);
    if (!strcmp(mode, "instrumented")) { settings.connect_ms = 1500; settings.total_ms = 4000; }
    else if (!strcmp(mode, "bad-timeout")) settings.connect_ms = 0;
    else if (!strcmp(mode, "bad-total")) settings.total_ms = 60001;
    else if (!strcmp(mode, "bad-order")) settings.connect_ms = 701;
    else if (!strcmp(mode, "bad-chantypes")) chantypes = "##";
    else if (!strcmp(mode, "null-chantypes")) chantypes = NULL;
    else if (!strcmp(mode, "bad-uuid")) strcpy(credential.subject_id, "invalid");
    else if (!strcmp(mode, "bad-token")) strcpy(credential.token, "invalid token");
    else if (!strcmp(mode, "bad-credential-origin")) strcpy(credential.origin, "http://invalid");
    else if (!strcmp(mode, "wrong-origin")) strcpy(credential.origin, "https://different.invalid");
    else if (!strcmp(mode, "occupied")) {
        static const unsigned char empty[] = "{\"networks\":[],\"channels\":{},\"heads\":{}}";
        assert(gin_topology_parse_boot(empty, sizeof(empty)-1, "#", &seed) == GIN_TOPOLOGY_OK);
        out = seed;
    }
    saved = credential; saved_policy = policy;
    assert(gin_http_global_init() == GIN_HTTP_OK);
    for (i = 0; i < calls; i++) {
        code = gin_topology_fetch(!strcmp(mode, "null-settings") ? NULL : &settings,
            !strcmp(mode, "null-policy") ? NULL : &policy,
            !strcmp(mode, "null-credential") ? NULL : &credential,
            chantypes, !strcmp(mode, "null-out") ? NULL : &out);
        assert(memcmp(&credential, &saved, sizeof(saved)) == 0);
        assert(memcmp(&policy, &saved_policy, sizeof(policy)) == 0);
        if (seed) assert(code == GIN_TOPOLOGY_INVALID && out == seed);
        else if (code != GIN_TOPOLOGY_OK) assert(out == NULL);
        printf("%d %zu %zu %d\n", (int)code,
            code == GIN_TOPOLOGY_OK ? gin_topology_network_count(out) : 0,
            code == GIN_TOPOLOGY_OK ? gin_topology_window_count(out) : 0,
            code == GIN_TOPOLOGY_OK ? gin_topology_queries_known(out) : 0);
        if (code == GIN_TOPOLOGY_OK) {
            for (j = 0; j < gin_topology_network_count(out); j++) {
                const struct gin_topology_network *n = gin_topology_network_at(out, j);
                assert(n);
                const unsigned char *nick = (const unsigned char *)n->own_nick;
                assert(nick && *nick);
                printf("N %" PRId64 " %s ", n->id, n->component);
                while (*nick) printf("%02X", (unsigned)*nick++);
                putchar('\n');
            }
            for (j = 0; j < gin_topology_window_count(out); j++) {
                const struct gin_topology_window *w = gin_topology_window_at(out, j);
                assert(w && w->role == GIN_REMOTE_CHANNEL && !w->opened_at);
                printf("W %" PRId64 " %d %d %d %s/%s\n", w->network_id,
                    (int)w->role, w->joined, (int)w->source,
                    w->name.network_component, w->name.window_component);
            }
        }
        gin_topology_free(out); out = NULL; seed = NULL;
    }
    gin_http_global_cleanup();
    gin_credential_clear(&credential); gin_credential_clear(&saved);
    return 0;
}
