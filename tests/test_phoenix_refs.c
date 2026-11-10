#ifdef NDEBUG
#undef NDEBUG
#endif
#include "gin_phoenix.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    struct gin_phoenix_refs refs = GIN_PHOENIX_REFS_INITIALIZER;
    char out[GIN_PHOENIX_REF_CAP];
    memset(out, 'x', sizeof(out));
    assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_OK);
    assert(!strcmp(out, "1") && refs.current == 1 && !refs.exhausted);
    assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_OK);
    assert(!strcmp(out, "2") && refs.current == 2 && !refs.exhausted);
    {
        struct gin_phoenix_refs other = GIN_PHOENIX_REFS_INITIALIZER;
        uint64_t saved = refs.current;
        char before[GIN_PHOENIX_REF_CAP];
        memcpy(before, out, sizeof(before));
        assert(gin_phoenix_refs_next(&refs, out, GIN_PHOENIX_REF_CAP-1) == GIN_PHOENIX_INVALID);
        assert(refs.current == saved && !refs.exhausted && !memcmp(out, before, sizeof(out)));
        assert(gin_phoenix_refs_next(NULL, out, sizeof(out)) == GIN_PHOENIX_INVALID);
        assert(!memcmp(out, before, sizeof(out)));
        assert(gin_phoenix_refs_next(&refs, NULL, sizeof(out)) == GIN_PHOENIX_INVALID && refs.current == saved);
        assert(gin_phoenix_refs_next(&other, out, sizeof(out)) == GIN_PHOENIX_OK);
        assert(!strcmp(out, "1") && other.current == 1 && refs.current == saved);
        gin_phoenix_refs_reset(&refs);
        assert(refs.current == 0 && !refs.exhausted);
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_OK && !strcmp(out, "1"));
        refs.current = UINT64_MAX-1; refs.exhausted = 0;
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_OK);
        assert(!strcmp(out, "18446744073709551615") && refs.current == UINT64_MAX && refs.exhausted);
        memcpy(before, out, sizeof(before));
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_LIMIT);
        assert(refs.current == UINT64_MAX && refs.exhausted && !memcmp(out, before, sizeof(out)));
        refs.exhausted = 0;
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_LIMIT);
        assert(refs.current == UINT64_MAX && !refs.exhausted && !memcmp(out, before, sizeof(out)));
        refs.current = 4; refs.exhausted = 2;
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_LIMIT);
        assert(refs.current == 4 && refs.exhausted == 2 && !memcmp(out, before, sizeof(out)));
        gin_phoenix_refs_reset(&refs); gin_phoenix_refs_reset(NULL);
        assert(gin_phoenix_refs_next(&refs, out, sizeof(out)) == GIN_PHOENIX_OK && !strcmp(out, "1"));
    }
    puts("Phoenix references monotonicity, boundaries, exhaustion and failure controls passed");
    return 0;
}
