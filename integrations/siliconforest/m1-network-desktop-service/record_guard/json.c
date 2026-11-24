#include "record_guard.h"
#include <limits.h>
static int overlap(const void *a,size_t n,const void *b,size_t m) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(!n||!m)return 0;
    if(n-1>UINTPTR_MAX-x||m-1>UINTPTR_MAX-y)return 1;
    return x<=y+m-1&&y<=x+n-1;
}
struct parser { const uint8_t *b; size_t n,pos,used,steps; struct rg_json_workspace *w; enum rg_code error; };
static int step(struct parser *p) { if(++p->steps>RG_MAX_STEPS){p->error=RG_LIMIT;return 0;} return 1; }
static void ws(struct parser *p) {
    while(p->pos<p->n&&(p->b[p->pos]==' '||p->b[p->pos]=='\t'||p->b[p->pos]=='\r'||p->b[p->pos]=='\n')) ++p->pos;
}
static int hex(uint8_t c) { if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; if(c>='A'&&c<='F')return c-'A'+10;return -1; }
/* Decode one already validated string byte, or validate while parsing. */
static int character(const uint8_t *b,size_t end,size_t *pos,uint8_t *out) {
    if(*pos>=end)return 0;
    uint8_t c=b[(*pos)++];
    if(c<32||c>127||c=='"')return 0;
    if(c!='\\'){*out=c;return 1;}
    if(*pos>=end)return 0;
    c=b[(*pos)++];
    switch(c){
    case '"':case '\\':case '/':*out=c;return 1;
    case 'b':*out=8;return 1;case 'f':*out=12;return 1;case 'n':*out=10;return 1;
    case 'r':*out=13;return 1;case 't':*out=9;return 1;
    case 'u':{
        if(end-*pos<4)return 0; unsigned v=0;
        for(unsigned i=0;i<4;++i){int h=hex(b[(*pos)++]);if(h<0)return 0;v=v*16+(unsigned)h;}
        if(v>127)return 0;*out=(uint8_t)v;return 1;
    }
    default:return 0;
    }
}
static int string(struct parser *p,uint32_t at) {
    struct rg_token *t=&p->w->tokens[at];
    if(p->pos>=p->n||p->b[p->pos++]!='"')return 0;
    t->type=RG_STRING;t->start=(uint32_t)p->pos;
    size_t decoded=0;
    while(p->pos<p->n&&p->b[p->pos]!='"'){
        uint8_t c;if(!step(p)||!character(p->b,p->n,&p->pos,&c))return 0;++decoded;
    }
    if(p->pos>=p->n)return 0;
    t->length=(uint32_t)(p->pos-t->start);t->magnitude=decoded;++p->pos;t->next=(uint32_t)p->used;return 1;
}
static int equal_keys(struct parser *p,const struct rg_token *a,const struct rg_token *b) {
    if(!step(p))return 0;
    if(a->magnitude!=b->magnitude)return 0;
    size_t x=a->start,y=b->start;
    while(x<a->start+a->length){uint8_t u,v;if(!step(p))return 0;
        if(!character(p->b,a->start+a->length,&x,&u)||!character(p->b,b->start+b->length,&y,&v))return 0;
        if(u!=v)return 0;
    }return 1;
}
static int token(struct parser *p,uint32_t *out) {
    if(p->used==RG_MAX_TOKENS||!step(p)){p->error=RG_LIMIT;return 0;}
    *out=(uint32_t)p->used++;p->w->tokens[*out]=(struct rg_token){0};return 1;
}
static int value(struct parser *p,unsigned depth) {
    if(depth>RG_MAX_DEPTH){p->error=RG_LIMIT;return 0;}
    ws(p);if(p->pos>=p->n)return 0;uint32_t at;if(!token(p,&at))return 0;
    struct rg_token *t=&p->w->tokens[at];t->start=(uint32_t)p->pos;
    uint8_t c=p->b[p->pos];
    if(c=='"')return string(p,at);
    if(c=='{'||c=='['){
        t->type=c=='{'?RG_OBJECT:RG_ARRAY;uint8_t end=c=='{'?'}':']';++p->pos;ws(p);
        if(p->pos<p->n&&p->b[p->pos]==end){++p->pos;t->next=(uint32_t)p->used;return 1;}
        for(;;){
            if(c=='{'){
                uint32_t key;if(!token(p,&key)||!string(p,key))return 0;
                for(uint32_t j=at+1;j<key;){
                    if(equal_keys(p,&p->w->tokens[j],&p->w->tokens[key])){p->error=RG_DUPLICATE;return 0;}
                    if(p->error)return 0;j=p->w->tokens[j+1].next;
                }
                ws(p);if(p->pos>=p->n||p->b[p->pos++]!=':')return 0;
            }
            if(!value(p,depth+1))return 0;++t->children;ws(p);
            if(p->pos>=p->n)return 0;
            if(p->b[p->pos]==end){++p->pos;break;}
            if(p->b[p->pos++]!=',')return 0;ws(p);
        }
        t->next=(uint32_t)p->used;return 1;
    }
    if(c=='-'||(c>='0'&&c<='9')){
        t->type=RG_INTEGER;if(c=='-'){t->negative=1;if(++p->pos>=p->n)return 0;}
        size_t first=p->pos;
        while(p->pos<p->n&&p->b[p->pos]>='0'&&p->b[p->pos]<='9'){
            unsigned d=p->b[p->pos++]-'0';
            if(t->magnitude>(UINT64_MAX-d)/10){p->error=RG_RANGE;return 0;}
            t->magnitude=t->magnitude*10+d;
        }
        if(p->pos==first||(p->pos-first>1&&p->b[first]=='0'))return 0;
        if(t->negative&&t->magnitude>(UINT64_C(1)<<63)){p->error=RG_RANGE;return 0;}
        if(p->pos<p->n&&(p->b[p->pos]=='.'||p->b[p->pos]=='e'||p->b[p->pos]=='E'))return 0;
    }else{
        const char *word;size_t n;
        if(c=='t'){word="true";n=4;t->type=RG_BOOL;t->magnitude=1;}
        else if(c=='f'){word="false";n=5;t->type=RG_BOOL;}
        else if(c=='n'){word="null";n=4;t->type=RG_NULL;}else return 0;
        if(p->n-p->pos<n)return 0;
        for(size_t i=0;i<n;++i)if(p->b[p->pos+i]!=(uint8_t)word[i])return 0;p->pos+=n;
    }
    t->length=(uint32_t)(p->pos-t->start);t->next=(uint32_t)p->used;return 1;
}
enum rg_code rg_json_parse(const uint8_t *b,size_t n,struct rg_json_workspace *w,struct rg_document *out) {
    if(!b||!w||!out||overlap(b,n,w,sizeof(*w))||overlap(out,sizeof(*out),w,sizeof(*w))||overlap(out,sizeof(*out),b,n))return RG_ARGUMENT;
    if(!n||n>RG_MAX_BYTES)return RG_LIMIT;
    struct parser p={b,n,0,0,0,w,RG_OK};
    if(!value(&p,1))return p.error?p.error:RG_JSON;ws(&p);
    if(p.pos!=n)return RG_JSON;
    *out=(struct rg_document){b,n,w->tokens,p.used};return RG_OK;
}
static const struct rg_token *get(const struct rg_document *d,uint32_t at) {
    if(!d||!d->bytes||!d->tokens||!d->count||d->count>RG_MAX_TOKENS||!d->length||d->length>RG_MAX_BYTES||at>=d->count)return NULL;
    const struct rg_token *t=&d->tokens[at];
    if(t->start>d->length||t->length>d->length-t->start||t->next<=at||t->next>d->count)return NULL;
    return t;
}
static int key_equal(const struct rg_document *d,const struct rg_token *t,const char *key,size_t n) {
    if(t->type!=RG_STRING||t->magnitude!=n)return 0;size_t pos=t->start;
    for(size_t i=0;i<n;++i){uint8_t c;if(!character(d->bytes,t->start+t->length,&pos,&c)||c!=(uint8_t)key[i])return 0;}return 1;
}
enum rg_code rg_json_path(const struct rg_document *d,const struct rg_step *path,size_t n,uint32_t *out) {
    if(!out||n>RG_MAX_DEPTH||(n&&!path)||!get(d,0))return RG_ARGUMENT;
    if(overlap(out,sizeof(*out),d,sizeof(*d))||overlap(out,sizeof(*out),d->bytes,d->length)||
       overlap(out,sizeof(*out),d->tokens,d->count*sizeof(*d->tokens))||overlap(out,sizeof(*out),path,n*sizeof(*path)))return RG_ARGUMENT;
    uint32_t at=0;
    for(size_t i=0;i<n;++i){
        const struct rg_token *t=get(d,at);if(!t)return RG_ARGUMENT;
        uint32_t cur=at+1;int found=0;
        if(path[i].kind==RG_KEY){
            if(t->type!=RG_OBJECT)return RG_TYPE;if(!path[i].key||path[i].length>RG_MAX_BYTES)return RG_ARGUMENT;
            if(overlap(out,sizeof(*out),path[i].key,path[i].length))return RG_ARGUMENT;
            for(uint32_t j=0;j<t->children;++j){
                const struct rg_token *key=get(d,cur),*v=get(d,cur+1);if(!key||!v)return RG_ARGUMENT;
                if(key_equal(d,key,path[i].key,path[i].length)){at=cur+1;found=1;break;}cur=v->next;
            }
        }else if(path[i].kind==RG_INDEX){
            if(t->type!=RG_ARRAY)return RG_TYPE;
            if(path[i].index<t->children){for(size_t j=0;j<path[i].index;++j){const struct rg_token *v=get(d,cur);if(!v)return RG_ARGUMENT;cur=v->next;}at=cur;found=1;}
        }else return RG_ARGUMENT;
        if(!found)return RG_MISSING;
    }
    *out=at;return RG_OK;
}
static int scalar_output(const struct rg_document *d,const void *out,size_t n) {
    return out&&!overlap(out,n,d,sizeof(*d))&&!overlap(out,n,d->bytes,d->length)&&!overlap(out,n,d->tokens,d->count*sizeof(*d->tokens));
}
enum rg_code rg_json_u64(const struct rg_document *d,uint32_t at,uint64_t *out) {
    const struct rg_token *t=get(d,at);if(!t||!scalar_output(d,out,sizeof(*out)))return RG_ARGUMENT;
    if(t->type!=RG_INTEGER)return RG_TYPE;if(t->negative)return RG_RANGE;*out=t->magnitude;return RG_OK;
}
enum rg_code rg_json_i64(const struct rg_document *d,uint32_t at,int64_t *out) {
    const struct rg_token *t=get(d,at);if(!t||!scalar_output(d,out,sizeof(*out)))return RG_ARGUMENT;
    if(t->type!=RG_INTEGER)return RG_TYPE;
    if(!t->negative&&t->magnitude>INT64_MAX)return RG_RANGE;
    *out=t->negative?(t->magnitude==(UINT64_C(1)<<63)?INT64_MIN:-(int64_t)t->magnitude):(int64_t)t->magnitude;return RG_OK;
}
enum rg_code rg_json_bool(const struct rg_document *d,uint32_t at,int *out) {
    const struct rg_token *t=get(d,at);if(!t||!scalar_output(d,out,sizeof(*out)))return RG_ARGUMENT;
    if(t->type!=RG_BOOL)return RG_TYPE;*out=(int)t->magnitude;return RG_OK;
}
enum rg_code rg_json_null(const struct rg_document *d,uint32_t at) {
    const struct rg_token *t=get(d,at);if(!t)return RG_ARGUMENT;return t->type==RG_NULL?RG_OK:RG_TYPE;
}
enum rg_code rg_json_string(const struct rg_document *d,uint32_t at,char *out,size_t cap,size_t *length) {
    const struct rg_token *t=get(d,at);if(!t||!out||!length)return RG_ARGUMENT;
    if(t->type!=RG_STRING)return RG_TYPE;if(t->magnitude>=cap)return RG_LIMIT;
    if(!scalar_output(d,out,(size_t)t->magnitude+1)||!scalar_output(d,length,sizeof(*length))||overlap(out,(size_t)t->magnitude+1,length,sizeof(*length)))return RG_ARGUMENT;
    /* Verify again before output so malformed/mutated scratch cannot cause a
     * partially written output. Caller must still prohibit concurrent changes. */
    size_t pos=t->start;for(uint64_t i=0;i<t->magnitude;++i){uint8_t c;if(!character(d->bytes,t->start+t->length,&pos,&c))return RG_ARGUMENT;}
    if(pos!=t->start+t->length)return RG_ARGUMENT;
    pos=t->start;for(size_t i=0;i<t->magnitude;++i){uint8_t c=0;(void)character(d->bytes,t->start+t->length,&pos,&c);out[i]=(char)c;}
    out[t->magnitude]=0;*length=(size_t)t->magnitude;return RG_OK;
}
