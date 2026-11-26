/* Private TU: corrupt only the new routing DOM, not raw bytes or downstream
 * decoders. Run through the same real HTTPS/WSS receiver driver. Healthy mode
 * asserts independent exact bytes, destination indices and persisted IDs first. */
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
static json_t *routing_loadb(const char *raw,size_t length,size_t flags,json_error_t *error)
{
    json_t *o=json_loadb(raw,length,flags,error);
    const char *mode=getenv("GIN_DISPATCH_CASE");
    if(o && mode) {
        json_t *row=json_object_get(o,"message");
        if(!strcmp(mode,"fidelity_kind")) (void)json_object_set_new(o,"kind",json_string("metadata"));
        if(!strcmp(mode,"fidelity_peer") && row) (void)json_object_set_new(row,"dm_with",json_string("Peer"));
        if(!strcmp(mode,"fidelity_id") && row) (void)json_object_del(row,"id");
    }
    return o;
}
#define json_loadb routing_loadb
#include "../src/account_dispatch.c"
#undef json_loadb
#include "account_dispatch_driver.c"
