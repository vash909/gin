#ifndef GIN_JSON_EVIDENCE_H
#define GIN_JSON_EVIDENCE_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <jansson.h>
/* Allocation-free raw/DOM evidence shared by JSON consumers. The decoder has
 * already checked syntax, UTF-8, duplicate keys and bounded depth. Preserve
 * strings, integers, boolean/null and structure under recoverable decoder OOM.
 * Reals are unconsumed metadata: only their type, not value fidelity, is checked.
 * This does not repair dependency memory safety under arbitrary allocation faults.
 */
struct gin_json_scan { const unsigned char *p, *end; };
static void gin_json_space(struct gin_json_scan *s) { while (s->p < s->end && (*s->p==' ' || *s->p=='\n' || *s->p=='\r' || *s->p=='\t')) s->p++; }
static int gin_json_take(struct gin_json_scan *s, unsigned char c) { gin_json_space(s); if(s->p==s->end || *s->p!=c) return 0; s->p++; return 1; }
static int gin_json_hex4(struct gin_json_scan *s, unsigned *v)
{
    unsigned i, x=0;
    for(i=0;i<4;i++) { unsigned c; if(s->p==s->end) return 0; c=*s->p++;
        if(c>='0' && c<='9') c-='0'; else if(c>='a' && c<='f') c=c-'a'+10; else if(c>='A' && c<='F') c=c-'A'+10; else return 0;
        x=x*16+c;
    } *v=x; return 1;
}
static int gin_json_string(struct gin_json_scan *s, const char *expected)
{
    size_t at=0, n=expected ? strlen(expected):0;
    if(!gin_json_take(s,'"')) return 0;
    while(s->p<s->end) {
        unsigned c=*s->p++; unsigned char bytes[4]; size_t k=1,i;
        if(c=='"') return !expected || at==n;
        if(c<32) return 0;
        if(c=='\\') {
            if(s->p==s->end) return 0;
            c=*s->p++;
            switch(c) {
            case '"': case '\\': case '/': break;
            case 'b': c=8; break; case 'f': c=12; break; case 'n': c=10; break; case 'r': c=13; break; case 't': c=9; break;
            case 'u': {
                unsigned low;
                if(!gin_json_hex4(s,&c)) return 0;
                if(c>=0xd800 && c<=0xdbff) {
                    if(s->end-s->p<2 || s->p[0]!='\\' || s->p[1]!='u') return 0;
                    s->p+=2; if(!gin_json_hex4(s,&low) || low<0xdc00 || low>0xdfff) return 0;
                    c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
                } else if(c>=0xdc00 && c<=0xdfff) return 0;
                if(!c) return 0;
                if(c<0x80) break;
                if(c<0x800) { bytes[0]=(unsigned char)(0xc0|(c>>6)); k=2; }
                else if(c<0x10000) { bytes[0]=(unsigned char)(0xe0|(c>>12)); bytes[1]=(unsigned char)(0x80|((c>>6)&63)); k=3; }
                else { bytes[0]=(unsigned char)(0xf0|(c>>18)); bytes[1]=(unsigned char)(0x80|((c>>12)&63)); bytes[2]=(unsigned char)(0x80|((c>>6)&63)); k=4; }
                bytes[k-1]=(unsigned char)(0x80|(c&63)); break;
            }
            default: return 0;
            }
        }
        if(k==1) bytes[0]=(unsigned char)c;
        for(i=0;i<k;i++) { if(expected && (at>=n || (unsigned char)expected[at]!=bytes[i])) return 0; at++; }
    } return 0;
}
static int gin_json_value(struct gin_json_scan *s,const json_t *v,unsigned depth)
{
    size_t count=0; unsigned char c;
    if(depth>32) return 0;
    gin_json_space(s); if(s->p==s->end || !v) return 0; c=*s->p;
    if(c=='"') return json_is_string(v) && gin_json_string(s,json_string_value(v));
    if(c=='[') {
        if(!json_is_array(v)) return 0;
        s->p++; gin_json_space(s);
        if(s->p<s->end && *s->p==']') { s->p++; return json_array_size(v)==0; }
        do { if(!gin_json_value(s,json_array_get(v,count++),depth+1)) return 0; gin_json_space(s); if(s->p<s->end && *s->p==']') {s->p++; return count==json_array_size(v);} } while(gin_json_take(s,','));
        return 0;
    }
    if(c=='{') {
        if(!json_is_object(v)) return 0;
        s->p++; gin_json_space(s);
        if(s->p<s->end && *s->p=='}') {s->p++; return json_object_size(v)==0;}
        {
            void *it=json_object_iter((json_t *)v);
            /* Jansson >=2.8 preserves parsed key insertion order. Walk once:
             * searching every DOM key for each input key is quadratic in
             * untrusted additive metadata, despite a bounded input size. */
            do {
                const json_t *child;
                if(!it || !gin_json_string(s,json_object_iter_key(it))) return 0;
                child=json_object_iter_value(it);
                if(!gin_json_take(s,':') || !gin_json_value(s,child,depth+1)) return 0;
                count++; it=json_object_iter_next((json_t *)v,it); gin_json_space(s);
                if(s->p<s->end && *s->p=='}') {s->p++; return !it && count==json_object_size(v);}
            } while(gin_json_take(s,','));
        } return 0;
    }
    if(c=='n' || c=='t' || c=='f') {
        const char *lit=c=='n'?"null":c=='t'?"true":"false"; size_t n=strlen(lit);
        if((size_t)(s->end-s->p)<n || memcmp(s->p,lit,n)) return 0;
        s->p+=n; return c=='n'?json_is_null(v):c=='t'?json_is_true(v):json_is_false(v);
    }
    {
        uint64_t n=0, cap; int neg=0, overflow=0, real=0; const unsigned char *start=s->p;
        if(*s->p=='-') { neg=1; s->p++; }
        if(s->p==s->end || *s->p<'0' || *s->p>'9') return 0;
        cap=neg ? (uint64_t)INT64_MAX+1u : (uint64_t)INT64_MAX;
        do { unsigned d=*s->p++-'0'; if(n>(cap-d)/10u) overflow=1; else n=n*10u+d; } while(s->p<s->end && *s->p>='0' && *s->p<='9');
        if(s->p-start>(neg?2:1) && start[neg]=='0') return 0;
        if(s->p<s->end && (*s->p=='.' || *s->p=='e' || *s->p=='E')) {
            real=1;
            if(*s->p=='.') {s->p++; if(s->p==s->end || *s->p<'0' || *s->p>'9') return 0; while(s->p<s->end && *s->p>='0' && *s->p<='9') s->p++;}
            if(s->p<s->end && (*s->p=='e'||*s->p=='E')) {s->p++; if(s->p<s->end && (*s->p=='+'||*s->p=='-')) s->p++; if(s->p==s->end || *s->p<'0'||*s->p>'9') return 0; while(s->p<s->end && *s->p>='0'&&*s->p<='9') s->p++;}
        }
        if(real) return json_is_real(v); /* Unconsumed real metadata: no fidelity claim. */
        if(overflow || !json_is_integer(v)) return 0;
        if(neg) return json_integer_value(v)==(n==(uint64_t)INT64_MAX+1u?INT64_MIN:-(int64_t)n);
        return json_integer_value(v)==(int64_t)n;
    }
}
static int gin_json_evidence(const unsigned char *data,size_t length,const json_t *root)
{ struct gin_json_scan s={data,data+length}; if(!gin_json_value(&s,root,0)) return 0; gin_json_space(&s); return s.p==s.end; }
#endif
