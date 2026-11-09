#include "gin_naming.h"
/* Keep the test assertions active even for release-oriented caller flags. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char out[257], back[257], input[256];
    size_t i;
    assert(gin_network_encode("Azzurra", out, sizeof(out)) == 0);
    assert(strcmp(out, "%41zzurra") == 0);
    assert(gin_network_decode(out, back, sizeof(back)) == 0);
    assert(strcmp(back, "Azzurra") == 0);
    for (i = 1; i <= 255; ++i) {
        memset(input, 'a', i); input[i] = '\0';
        assert(gin_network_encode(input, out, sizeof(out)) == 0);
        assert(gin_network_decode(out, back, sizeof(back)) == 0);
        assert(strcmp(input, back) == 0);
        assert(gin_network_encode(input, out, i + 1) == 0);
        assert(gin_network_decode(input, back, i + 1) == 0);
        assert(gin_network_encode(input, out, i) == -1);
        assert(gin_network_decode(input, back, i) == -1);
    }
    assert(gin_network_decode("Azzurra", back, sizeof(back)) == -1);
    assert(gin_network_decode("%61", back, sizeof(back)) == -1);
    assert(gin_network_decode("%4a", back, sizeof(back)) == -1);
    assert(gin_network_decode("%", back, sizeof(back)) == -1);
    assert(gin_network_decode("%00", back, sizeof(back)) == -1);
    memset(input, 'A', 86); input[86] = '\0';
    assert(gin_network_encode(input, out, sizeof(out)) == -1);
    {
        static const char *targets[] = {"#Grappa", "Grappa", "&Foo", "+Foo", "!Foo", "#+26foo", "#cafÉ", "#café", "#é", "#é", "#[x]", "#{x}", "#a\\b", "#a^b", "#a~b", "$server"};
        static const char *names[] = {"grappa", "@grappa", "+26foo", "+2Bfoo", "+21foo", "%2B26foo", "caf%C3%89", "caf%C3%A9", "e%CC%81", "%C3%A9", "%5Bx%5D", "%7Bx%7D", "a%5Cb", "a%5Eb", "a%7Eb", "@%24server"};
        static const char *bad[] = {"", ".", "..", "#", "#.", "#..", "#a/b", "#a b", "#a\t", "#a\177", "#\300\200", "#\342\202", "#\355\240\200", "#\364\220\200\200", "#\302\200", "#\302\237", "#\200", "#\370\210\200\200\200"};
        static const char *aliases[] = {"", ".", "..", "@", "+23foo", "+26", "+2bfoo", "%67rappa", "Grappa", "@Grappa", "%00", "%2f", "%2F", "%C0%80", "%C2%80", "%", "%A", "%GG", "@%23foo", "%2bfoo"};
        enum gin_window_kind kind;
        assert(gin_chantypes_validate("#&+!") == 0);
        assert(gin_chantypes_validate("") == 0);
        assert(gin_chantypes_validate("##") == -1);
        assert(gin_chantypes_validate("#x") == -1);
        for (i = 0; i < sizeof(targets)/sizeof(targets[0]); ++i) {
            struct gin_name name;
            assert(gin_name_prepare("Net", targets[i], "#&+!", &name) == 0);
            assert(strcmp(name.input_network_slug, "Net") == 0);
            assert(strcmp(name.original_wire_target, targets[i]) == 0);
            assert(strcmp(name.network_component, "%4Eet") == 0);
            assert(strcmp(name.window_component, names[i]) == 0);
            assert(gin_window_decode(names[i], "#&+!", back, sizeof(back), &kind) == 0);
            assert(strcmp(back, name.canonical_target) == 0);
            assert(kind == name.kind);
            assert(gin_window_encode(back, "#&+!", out, sizeof(out), &kind) == 0);
            assert(strcmp(out, names[i]) == 0);
            assert(gin_window_encode(back, "#&+!", out, strlen(names[i]), &kind) == -1);
            assert(gin_window_decode(names[i], "#&+!", out, strlen(back), &kind) == -1);
        }
        for (i = 0; i < sizeof(bad)/sizeof(bad[0]); ++i)
            assert(gin_window_encode(bad[i], "#&+!", out, sizeof(out), &kind) == -1);
        for (i = 0; i < sizeof(aliases)/sizeof(aliases[0]); ++i)
            assert(gin_window_decode(aliases[i], "#&+!", out, sizeof(out), &kind) == -1);
        assert(gin_window_encode("#Foo", "", out, sizeof(out), &kind) == 0);
        assert(strcmp(out, "@%23foo") == 0 && kind == GIN_WINDOW_NONCHANNEL);
        assert(gin_window_decode("foo", "", out, sizeof(out), &kind) == -1);
        assert(gin_window_decode("+26foo", "#", out, sizeof(out), &kind) == -1);
        for (i = 33; i <= 126; ++i) {
            char target[] = "#xX";
            if (i == '/') continue;
            target[2] = (char)i;
            assert(gin_window_encode(target, "#&+!", out, sizeof(out), &kind) == 0);
            assert(gin_window_decode(out, "#&+!", back, sizeof(back), &kind) == 0);
            assert(back[2] == (char)(i >= 'A' && i <= 'Z' ? i + 32 : i));
        }
    }
    {
        enum gin_window_kind kind;
        char target[258], encoded[256], canonical[257];
        const char *samples[] = {"é", "é", "😀", "\302\240", "\364\217\277\277"};
        size_t j, n;
        for (j = 0; j < sizeof(samples)/sizeof(samples[0]); ++j) {
            size_t bytes = strlen(samples[j]), expansion = 0, k;
            size_t limit;
            for (k = 0; k < bytes; ++k)
                expansion += (unsigned char)samples[j][k] < 128 ? 1 : 3;
            limit = 255 / expansion;
            target[0] = '#';
            for (n = 0; n < limit; ++n)
                memcpy(target + 1 + n * bytes, samples[j], bytes);
            target[1 + limit * bytes] = '\0';
            assert(gin_window_encode(target, "#", encoded, sizeof(encoded), &kind) == 0);
            assert(gin_window_decode(encoded, "#", canonical, sizeof(canonical), &kind) == 0);
            assert(strcmp(canonical, target) == 0);
            memcpy(target + 1 + limit * bytes, samples[j], bytes + 1);
            assert(gin_window_encode(target, "#", encoded, sizeof(encoded), &kind) == -1);
        }
        memset(target, 'x', 256); target[0] = '#'; target[256] = '\0';
        assert(gin_window_encode(target, "#", encoded, sizeof(encoded), &kind) == 0);
        assert(strlen(encoded) == 255);
        assert(gin_window_decode(encoded, "#", canonical, 256, &kind) == -1);
        assert(gin_window_decode(encoded, "#", canonical, 257, &kind) == 0);
        target[256] = 'x'; target[257] = '\0';
        assert(gin_window_encode(target, "#", encoded, sizeof(encoded), &kind) == -1);
        for (j = 0; j < 64; ++j) {
            static char folded_names[64][32];
            char slug[7];
            size_t k;
            for (n = 0; n < 6; ++n)
                slug[n] = (char)((j & ((size_t)1 << n)) ? 'A' : 'a');
            slug[6] = '\0';
            assert(gin_network_encode(slug, encoded, sizeof(encoded)) == 0);
            assert(gin_network_decode(encoded, canonical, sizeof(canonical)) == 0);
            assert(strcmp(slug, canonical) == 0);
            for (k = 0; encoded[k] != '\0'; ++k)
                folded_names[j][k] = encoded[k] >= 'A' && encoded[k] <= 'Z' ?
                    (char)(encoded[k] + 32) : encoded[k];
            folded_names[j][k] = '\0';
            for (k = 0; k < j; ++k)
                assert(strcmp(folded_names[j], folded_names[k]) != 0);
        }
        assert(gin_network_encode("x", encoded, 0) == -1);
        assert(gin_network_decode("x", canonical, 0) == -1);
        assert(gin_window_encode("#x", "#", encoded, 0, &kind) == -1);
        assert(gin_window_decode("x", "#", canonical, 0, &kind) == -1);
    }
    puts("Naming codec tests passed.");
    return 0;
}
