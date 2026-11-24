/* Original implementation. Wire integers decoded bytewise; no platform ABI. */
#include "egress_validate.h"
#include <string.h>

static const uint8_t question[17] = {7,'e','x','a','m','p','l','e',3,'c','o','m',0,0,1,0,1};
static uint16_t u16(const uint8_t *p) { return (uint16_t)(((uint16_t)p[0]<<8)|p[1]); }
static uint32_t u32(const uint8_t *p) {
    return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint8_t lower(uint8_t c) { return c>='A' && c<='Z' ? (uint8_t)(c+32) : c; }
static int equal_ci(const uint8_t *a, size_t n, const char *b) {
    size_t i;
    if (n != strlen(b)) return 0;
    for (i=0;i<n;i++) if (lower(a[i]) != (uint8_t)b[i]) return 0;
    return 1;
}
enum eg_status eg_dns_query(uint16_t id, uint8_t *out, size_t cap, size_t *written) {
    if (written) *written=0;
    if (!out || !written) return EG_ARGUMENT;
    if (cap<EG_DNS_QUERY_BYTES) return EG_DNS_SIZE;
    memset(out,0,EG_DNS_QUERY_BYTES);out[0]=(uint8_t)(id>>8);out[1]=(uint8_t)id;
    out[2]=1;out[5]=1;memcpy(out+12,question,sizeof question);
    *written=EG_DNS_QUERY_BYTES;return EG_OK;
}

/* Explicit frozen Python3.14.7 ipaddress predicate facts plus multicast/reserved
 * exclusions from backend policy. Numeric ranges are data, not copied code. */
int eg_ipv4_global(const uint8_t ip[4]) {
    static const uint32_t denied[][2] = {
        {0x00000000u,0x00ffffffu},{0x0a000000u,0x0affffffu},
        {0x64400000u,0x647fffffu},{0x7f000000u,0x7fffffffu},
        {0xa9fe0000u,0xa9feffffu},{0xac100000u,0xac1fffffu},
        {0xc0000000u,0xc00000ffu},{0xc0000200u,0xc00002ffu},
        {0xc0a80000u,0xc0a8ffffu},{0xc6120000u,0xc613ffffu},
        {0xc6336400u,0xc63364ffu},{0xcb007100u,0xcb0071ffu},
        {0xe0000000u,0xffffffffu}
    };
    uint32_t v;size_t i;
    if (!ip) return 0;
    v=u32(ip);
    if (v==0xc0000009u || v==0xc000000au) return 1;
    for (i=0;i<sizeof denied/sizeof denied[0];i++)
        if (v>=denied[i][0] && v<=denied[i][1]) return 0;
    return 1;
}

static enum eg_status name(const uint8_t *p,size_t n,size_t at,char out[254],size_t *end) {
    uint8_t seen[EG_DNS_MAX]={0};size_t cursor=at,used=0,written=0;unsigned step;
    for (step=0;step<64;step++) {
        unsigned length;size_t i;
        if (cursor>=n || seen[cursor]) return EG_DNS_NAME;
        seen[cursor]=1;length=p[cursor];
        if ((length&0xc0)==0xc0) {
            size_t target;
            if (cursor+1>=n) return EG_DNS_NAME;
            target=((size_t)(length&63)<<8)|p[cursor+1];
            if (target<12 || target>=cursor) return EG_DNS_NAME;
            if (!used) used=cursor+2;
            cursor=target;continue;
        }
        if ((length&0xc0) || cursor+1+length>n) return EG_DNS_NAME;
        cursor++;
        if (!length) {
            out[written]=0;*end=used ? used : cursor;return EG_OK;
        }
        if (written) {
            if (written==253) return EG_DNS_NAME;
            out[written++]='.';
        }
        if (length>253-written) return EG_DNS_NAME;
        for (i=0;i<length;i++) {
            uint8_t c=p[cursor+i];
            if (c<33 || c>126 || c=='.') return EG_DNS_NAME;
            out[written++]=(char)lower(c);
        }
        cursor+=length;
    }
    return EG_DNS_NAME;
}
struct answer { char owner[254],target[254];uint16_t kind;uint32_t ttl;uint8_t ip[4]; };
enum eg_status eg_dns_reply(const uint8_t *query,size_t qn,const uint8_t *p,size_t n,
    const uint8_t source[4],uint16_t port,struct eg_dns_answer *out) {
    static const uint8_t dns[4]={10,0,2,3};
    struct answer records[16];struct eg_dns_answer result;
    char current[254]="example.com",history[8][254];
    size_t at,end;uint16_t flags,an,ns,ar;unsigned i,hop;uint32_t ttl=60;
    if (out) memset(out,0,sizeof *out);
    if (!query || !p || !source || !out) return EG_ARGUMENT;
    if (qn!=EG_DNS_QUERY_BYTES || memcmp(query+2,"\1\0\0\1\0\0\0\0\0\0",10) ||
        memcmp(query+12,question,sizeof question)) return EG_DNS_QUERY;
    if (memcmp(source,dns,4) || port!=53) return EG_DNS_SOURCE;
    if (n<12 || n>EG_DNS_MAX) return EG_DNS_SIZE;
    flags=u16(p+2);an=u16(p+6);ns=u16(p+8);ar=u16(p+10);
    if (u16(p)!=u16(query) || !(flags&0x8000) || !(flags&0x0100) || (flags&0x7a4f) || u16(p+4)!=1)
        return EG_DNS_CORRELATION;
    if (!an || an>16 || ns>16 || ar>16 || (unsigned)an+ns+ar>32) return EG_DNS_RECORD;
    if (name(p,n,12,current,&end)) return EG_DNS_NAME;
    if (strcmp(current,"example.com") || end>n || n-end<4 || end+4-12!=sizeof question ||
        memcmp(p+12,question,sizeof question)) return EG_DNS_CORRELATION;
    at=end+4;memset(records,0,sizeof records);memset(&result,0,sizeof result);
    for (i=0;i<(unsigned)an+ns+ar;i++) {
        char owner[254];uint16_t kind,klass,size;uint32_t rrttl;
        enum eg_status e=name(p,n,at,owner,&end);
        if (e) return e;
        at=end;if (at>n || n-at<10) return EG_DNS_RECORD;
        kind=u16(p+at);klass=u16(p+at+2);rrttl=u32(p+at+4);size=u16(p+at+8);at+=10;
        if (size>n-at) return EG_DNS_RECORD;
        if (i<an) {
            struct answer *r=&records[i];
            if (klass!=1) return EG_DNS_RECORD;
            memcpy(r->owner,owner,strlen(owner)+1);r->kind=kind;r->ttl=rrttl;
            if (kind==5) {
                e=name(p,n,at,r->target,&end);
                if (e) return e;
                if (end!=at+size) return EG_DNS_RECORD;
            } else if (kind==1) {
                if (size!=4) return EG_DNS_RECORD;
                memcpy(r->ip,p+at,4);
            }
        }
        at+=size;
    }
    if (at!=n) return EG_DNS_RECORD;
    memcpy(current,"example.com",12);
    for (hop=0;hop<8;hop++) {
        const char *next=NULL;unsigned cnames=0,addresses=0;
        for (i=0;i<hop;i++) if (!strcmp(history[i],current)) return EG_DNS_CHAIN;
        memcpy(history[hop],current,strlen(current)+1);
        for (i=0;i<an;i++) if (!strcmp(records[i].owner,current)) {
            struct answer *r=&records[i];
            if (r->kind==5) {
                if (next && strcmp(next,r->target)) return EG_DNS_CHAIN;
                next=r->target;cnames++;
                if (r->ttl<ttl) ttl=r->ttl;
            } else if (r->kind==1) addresses++;
        }
        if (cnames) {
            if (addresses) return EG_DNS_CHAIN;
            memcpy(current,next,strlen(next)+1);continue;
        }
        if (!addresses || addresses>4) return EG_DNS_ADDRESS;
        for (i=0;i<an;i++) if (records[i].kind==1 && !strcmp(records[i].owner,current)) {
            if (!eg_ipv4_global(records[i].ip)) return EG_DNS_ADDRESS;
            memcpy(result.ipv4[result.count++],records[i].ip,4);
            if (records[i].ttl<ttl) ttl=records[i].ttl;
        }
        if (!ttl) return EG_DNS_TTL;
        result.lease_seconds=ttl;*out=result;return EG_OK;
    }
    return EG_DNS_CHAIN;
}

enum eg_status eg_http_request(const char *nonce,size_t nn,uint8_t *out,size_t cap,size_t *written) {
    static const char head[]="GET / HTTP/1.1\r\nHost: example.com\r\nUser-Agent: darwin-lab-egress/1\r\nAccept-Encoding: identity\r\nX-Darwin-Lab-Nonce: ";
    static const char tail[]="\r\nConnection: close\r\n\r\n";
    size_t i;
    _Static_assert(sizeof head-1+48+sizeof tail-1==EG_HTTP_REQUEST_BYTES,"fixed request length");
    if (written) *written=0;
    if (!nonce || !out || !written || nn!=48) return EG_ARGUMENT;
    for (i=0;i<nn;i++) if (!((nonce[i]>='0' && nonce[i]<='9') || (nonce[i]>='a' && nonce[i]<='f'))) return EG_ARGUMENT;
    if (cap<EG_HTTP_REQUEST_BYTES) return EG_HTTP_LIMIT;
    memcpy(out,head,sizeof head-1);memcpy(out+sizeof head-1,nonce,48);
    memcpy(out+sizeof head-1+48,tail,sizeof tail-1);*written=EG_HTTP_REQUEST_BYTES;return EG_OK;
}

static size_t find(const uint8_t *p,size_t n,size_t start,const char *text) {
    size_t i,len=strlen(text);
    if (start>n || len>n-start) return SIZE_MAX;
    for (i=start;i<=n-len;i++) if (!memcmp(p+i,text,len)) return i;
    return SIZE_MAX;
}
static void trim(const uint8_t *p,size_t *at,size_t *n) {
    while (*n && (p[*at]==' ' || p[*at]=='\t')) { (*at)++;(*n)--; }
    while (*n && (p[*at+*n-1]==' ' || p[*at+*n-1]=='\t')) (*n)--;
}
struct field { uint16_t name,nlen,value,vlen; };
static int names_equal(const uint8_t *p,const struct field *a,const struct field *b) {
    unsigned i;
    if (a->nlen!=b->nlen) return 0;
    for (i=0;i<a->nlen;i++) if (lower(p[a->name+i])!=lower(p[b->name+i])) return 0;
    return 1;
}
static const struct field *lookup(const uint8_t *p,const struct field *f,unsigned count,const char *key) {
    unsigned i;
    for (i=0;i<count;i++) if (equal_ci(p+f[i].name,f[i].nlen,key)) return f+i;
    return NULL;
}
struct content { unsigned matched,found; };
static void content(struct content *s,const uint8_t *p,size_t n) {
    static const uint8_t text[]="Example Domain";size_t i;
    for (i=0;i<n;i++) {
        if (p[i]==text[s->matched]) s->matched++;
        else s->matched=p[i]==text[0] ? 1u : 0u;
        if (s->matched==sizeof text-1) { s->found=1;s->matched=0; }
    }
}
static enum eg_status incomplete(int eof) { return eof ? EG_HTTP_TRUNCATED : EG_MORE; }
enum eg_status eg_http_response(const uint8_t *p,size_t n,int eof,struct eg_http_facts *out) {
    struct field fields[2048];unsigned count=0,i;
    struct eg_http_facts facts={0};struct content c={0};
    const struct field *f,*length,*transfer;size_t end,at,line,bstart,decoded=0;
    if (out) memset(out,0,sizeof *out);
    if ((!p && n) || !out || (eof!=0 && eof!=1)) return EG_ARGUMENT;
    if (n>EG_HTTP_MAX) return EG_HTTP_LIMIT;
    end=find(p,n,0,"\r\n\r\n");
    if (end==SIZE_MAX) return n>EG_HTTP_HEADER_END_MAX ? EG_HTTP_LIMIT : incomplete(eof);
    if (end>EG_HTTP_HEADER_END_MAX) return EG_HTTP_LIMIT;
    line=find(p,n,0,"\r\n");
    if (line==SIZE_MAX || line>end || line<12 ||
        (memcmp(p,"HTTP/1.0",8) && memcmp(p,"HTTP/1.1",8)) ||
        memcmp(p+8," 200",4) || (line>12 && p[12]!=' ')) return EG_HTTP_STATUS;
    for (at=0;at<line;at++) if (p[at]<32 || p[at]>126) return EG_HTTP_STATUS;
    at=line+2;
    while (at<end) {
        size_t colon=SIZE_MAX,start=at,vn,va,j;struct field field;
        line=find(p,n,at,"\r\n");
        if (line==SIZE_MAX || line>end || count==2048) return EG_HTTP_HEADER;
        for (j=at;j<line;j++) if (p[j]==':') { colon=j;break; }
        if (colon==SIZE_MAX || colon==start) return EG_HTTP_HEADER;
        for (j=start;j<colon;j++) {
            uint8_t ch=lower(p[j]);
            if (!((ch>='a' && ch<='z') || (ch>='0' && ch<='9') || ch=='-')) return EG_HTTP_HEADER;
        }
        va=colon+1;vn=line-va;trim(p,&va,&vn);
        for (j=va;j<va+vn;j++) if ((p[j]<32 && p[j]!=9) || p[j]>126) return EG_HTTP_HEADER;
        field.name=(uint16_t)start;field.nlen=(uint16_t)(colon-start);
        field.value=(uint16_t)va;field.vlen=(uint16_t)vn;
        for (i=0;i<count;i++) if (names_equal(p,fields+i,&field)) return EG_HTTP_HEADER;
        fields[count++]=field;at=line+2;
    }
    f=lookup(p,fields,count,"content-encoding");
    if (f && !equal_ci(p+f->value,f->vlen,"identity")) return EG_HTTP_ENCODING;
    f=lookup(p,fields,count,"content-type");
    if (!f) return EG_HTTP_HEADER;
    { size_t va=f->value,vn=f->vlen,j;
      for (j=0;j<vn;j++) if (p[va+j]==';') { vn=j;break; }
      trim(p,&va,&vn);if (!equal_ci(p+va,vn,"text/html")) return EG_HTTP_HEADER;
    }
    length=lookup(p,fields,count,"content-length");transfer=lookup(p,fields,count,"transfer-encoding");
    facts.header_bytes=end+4;bstart=end+4;
    if (transfer) {
        unsigned chunks;facts.framing=EG_HTTP_CHUNKED;at=bstart;
        if (length || !equal_ci(p+transfer->value,transfer->vlen,"chunked")) return EG_HTTP_FRAMING;
        for (chunks=0;chunks<1024;chunks++) {
            uint32_t size=0;size_t j;
            line=find(p,n,at,"\r\n");if (line==SIZE_MAX) return incomplete(eof);
            if (line-at<1 || line-at>8) return EG_HTTP_FRAMING;
            for (j=at;j<line;j++) {
                unsigned digit;uint8_t ch=lower(p[j]);
                if (ch>='0' && ch<='9') digit=ch-'0';
                else if (ch>='a' && ch<='f') digit=ch-'a'+10u;
                else return EG_HTTP_FRAMING;
                size=(size<<4)|digit;
            }
            at=line+2;
            if (size>EG_HTTP_MAX || size>EG_HTTP_MAX-decoded) return EG_HTTP_LIMIT;
            if (!size) {
                if (n-at<2) return incomplete(eof);
                if (n-at!=2 || memcmp(p+at,"\r\n",2)) return EG_HTTP_FRAMING;
                break;
            }
            if (size>n-at || n-at-size<2) return incomplete(eof);
            if (memcmp(p+at+size,"\r\n",2)) return EG_HTTP_FRAMING;
            content(&c,p+at,size);decoded+=size;at+=(size_t)size+2;
        }
        if (chunks==1024) return EG_HTTP_FRAMING;
    } else if (length) {
        uint32_t size=0;unsigned j;facts.framing=EG_HTTP_LENGTH;
        if (!length->vlen || length->vlen>6) return EG_HTTP_FRAMING;
        for (j=0;j<length->vlen;j++) {
            uint8_t ch=p[length->value+j];
            if (ch<'0' || ch>'9') return EG_HTTP_FRAMING;
            size=size*10+(unsigned)(ch-'0');
        }
        if (size>EG_HTTP_MAX) return EG_HTTP_LIMIT;
        if (n-bstart<size) return incomplete(eof);
        if (n-bstart!=size) return EG_HTTP_FRAMING;
        decoded=size;content(&c,p+bstart,decoded);
    } else {
        facts.framing=EG_HTTP_EOF;
        if (!eof) return EG_MORE;
        decoded=n-bstart;content(&c,p+bstart,decoded);
    }
    if (!c.found) return EG_HTTP_CONTENT;
    facts.body_bytes=decoded;facts.framing_complete=1;facts.eof_observed=(unsigned)eof;facts.example_content=1;
    *out=facts;return eof ? EG_OK : EG_MORE;
}
const char *eg_status_name(enum eg_status s) {
    static const char *const names[]={"ok","more","argument","dns_size","dns_query","dns_source",
        "dns_correlation","dns_name","dns_record","dns_chain","dns_address","dns_ttl","http_limit",
        "http_status","http_header","http_encoding","http_framing","http_truncated","http_content"};
    return (unsigned)s<sizeof names/sizeof names[0] ? names[s] : "unknown";
}
