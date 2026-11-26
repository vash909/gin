#ifdef NDEBUG
#undef NDEBUG
#endif
#include "account_naming.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void check(const char *target, const char *component)
{
    struct gin_name n, old;
    char decoded[257];
    enum gin_window_kind kind;
    assert(gin_name_prepare("Net", target, "#&+!", &old) == 0);
    assert(gin_account_name_prepare("Net", target, "#&+!", &n) == 0);
    assert(strcmp(n.window_component, component) == 0);
    assert(strcmp(n.input_network_slug, old.input_network_slug) == 0);
    assert(strcmp(n.original_wire_target, old.original_wire_target) == 0);
    assert(strcmp(n.canonical_target, old.canonical_target) == 0);
    assert(strcmp(n.network_component, old.network_component) == 0);
    assert(n.kind == old.kind);
    assert(gin_account_window_decode(component, "#&+!", decoded, sizeof(decoded), &kind) == 0);
    assert(strcmp(decoded, n.canonical_target) == 0 && kind == n.kind);
}

int main(int argc, char **argv)
{
    struct gin_name n, before;
    char out[257] = "unchanged", target[257], component[257];
    enum gin_window_kind kind = GIN_WINDOW_NONCHANNEL;
    const char *bad[] = {"in", "out", "room", "#%69n", "#IN", "#a%2fb", "#a%2Fb", "#.", "#..", "#", "#%00", "#%GG", "@%69n", "+23in"};
    size_t i;
    (void)argv;
    assert(gin_account_name_prepare("Net", "#in", "#&+!", &n) == 0);
    assert(strcmp(n.window_component, "#in") == 0);
    if (argc > 1) { puts("prepare GREEN"); return 0; }
    check("#in", "#in"); check("#out", "#out"); check("#room", "#room");
    check("in", "@in"); check("out", "@out"); check("room", "@room");
    check("@User", "@%40user"); check("&Room", "+26room");
    check("+Room", "+2Broom"); check("!Room", "+21room");
    check("##Room", "#%23room"); check("#a.b", "#a.b");
    check("#a%b", "#a%25b"); check("#\xC3\xA9", "#%C3%A9");
    for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i) {
        strcpy(out, "unchanged"); kind = GIN_WINDOW_NONCHANNEL;
        assert(gin_account_window_decode(bad[i], "#&+!", out, sizeof(out), &kind) == -1);
        assert(strcmp(out, "unchanged") == 0 && kind == GIN_WINDOW_NONCHANNEL);
    }
    assert(gin_account_window_decode("#in", "#", out, 3, &kind) == -1);
    assert(errno == ENAMETOOLONG);
    assert(strcmp(out, "unchanged") == 0 && kind == GIN_WINDOW_NONCHANNEL);
    assert(gin_account_window_decode("#in", "#", out, 0, &kind) == -1);
    assert(strcmp(out, "unchanged") == 0 && kind == GIN_WINDOW_NONCHANNEL);
    assert(gin_account_window_decode("#in", "&", out, sizeof(out), &kind) == -1);
    assert(gin_account_window_decode("+26room", "#", out, sizeof(out), &kind) == -1);
    assert(gin_account_window_decode("#in", "##", out, sizeof(out), &kind) == -1);
    assert(gin_account_name_prepare("net", "#in", "", &n) == 0);
    assert(strcmp(n.window_component, "@%23in") == 0);
    assert(gin_account_window_decode("@%23in", "", out, sizeof(out), &kind) == 0);
    assert(strcmp(out, "#in") == 0 && kind == GIN_WINDOW_NONCHANNEL);
    memset(&before, 0x5A, sizeof(before)); n = before;
    target[0] = '#'; memset(target + 1, 'a', 254); target[255] = '\0';
    assert(gin_account_name_prepare("net", target, "#", &n) == 0);
    assert(strlen(n.window_component) == 255);
    strcpy(component, n.window_component);
    assert(gin_account_window_decode(component, "#", out, sizeof(out), &kind) == 0);
    target[255] = 'a'; target[256] = '\0'; n = before; errno = 0;
    assert(gin_account_name_prepare("net", target, "#", &n) == -1 && errno == ENAMETOOLONG);
    assert(memcmp(&n, &before, sizeof(n)) == 0);
    assert(gin_account_name_prepare("bad/slug", "#in", "#", &n) == -1);
    assert(memcmp(&n, &before, sizeof(n)) == 0);
    assert(gin_account_name_prepare("net", "#a/b", "#", &n) == -1);
    assert(gin_account_name_prepare("net", "#\xC0\xAF", "#", &n) == -1);
    assert(gin_account_name_prepare("net", "#in", "##", &n) == -1);
    assert(gin_account_name_prepare("net", "", "#", &n) == -1);
    assert(memcmp(&n, &before, sizeof(n)) == 0);
    memset(component, 'a', 256); component[0] = '#'; component[256] = '\0';
    strcpy(out, "unchanged"); kind = GIN_WINDOW_NONCHANNEL;
    assert(gin_account_window_decode(component, "#", out, sizeof(out), &kind) == -1);
    assert(errno == ENAMETOOLONG && strcmp(out, "unchanged") == 0);
    assert(kind == GIN_WINDOW_NONCHANNEL);
    /* Pairwise identity/component separation, including network endpoints. */
    {
        const char *names[] = {"#in", "#out", "#room", "in", "out", "room", "##in", "@in", "&in", "+in", "!in"};
        struct gin_name values[11];
        size_t j;
        for (i = 0; i < 11; ++i) {
            assert(gin_account_name_prepare("net", names[i], "#&+!", &values[i]) == 0);
            assert(strcmp(values[i].window_component, "in") != 0);
            assert(strcmp(values[i].window_component, "out") != 0);
            for (j = 0; j < i; ++j)
                assert(strcmp(values[i].window_component, values[j].window_component) != 0);
        }
    }
    puts("account codec GREEN"); return 0;
}
