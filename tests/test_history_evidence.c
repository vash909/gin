#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t iterator_steps;
static void *counted_next(json_t *o, void *it)
{ iterator_steps++; return json_object_iter_next(o, it); }
#define json_object_iter_next counted_next
#include "../src/history_evidence.c"
#undef json_object_iter_next
int main(void)
{
    json_t *seed = json_object(), *parsed;
    json_error_t error = {0};
    char name[32], *bytes;
    unsigned i;
    assert(seed);
    for (i = 0; i < 1000u; i++) {
        assert(snprintf(name, sizeof(name), "additive_%u", i) > 0);
        assert(json_object_set_new(seed, name, json_integer(i)) == 0);
    }
    bytes = json_dumps(seed, JSON_COMPACT); assert(bytes); json_decref(seed);
    parsed = json_loads(bytes, JSON_REJECT_DUPLICATES, &error); assert(parsed);
    iterator_steps = 0;
    assert(gin_history_evidence((const unsigned char *)bytes, strlen(bytes), parsed));
    /* Deterministic work bound, not a machine-dependent elapsed-time test. */
    assert(iterator_steps <= 2u * 1000u);
    json_decref(parsed); free(bytes);
    return 0;
}
