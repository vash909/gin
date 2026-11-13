#include "history_evidence.h"
#include "json_evidence.h"
int gin_history_evidence(const unsigned char *data, size_t length, const json_t *root)
{ return gin_json_evidence(data, length, root); }
int gin_history_utf8(const char *text)
{
    const unsigned char *p=(const unsigned char *)text;
    if(!p) return 0;
    while(*p) {
        unsigned c=*p++, n,i,min;
        if(c<128) continue;
        if(c>=0xc2 && c<=0xdf) {n=1;min=0x80;c&=31;}
        else if(c>=0xe0 && c<=0xef) {n=2;min=0x800;c&=15;}
        else if(c>=0xf0 && c<=0xf4) {n=3;min=0x10000;c&=7;}
        else return 0;
        for(i=0;i<n;i++) {if(*p<0x80 || *p>0xbf) return 0; c=(c<<6)|(*p++&63);}
        if(c<min || c>0x10ffff || (c>=0xd800 && c<=0xdfff)) return 0;
    } return 1;
}
