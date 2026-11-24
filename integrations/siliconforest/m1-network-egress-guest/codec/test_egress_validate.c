#include "egress_validate.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { checks++;assert(x); } while(0)
static const uint8_t source[4]={10,0,2,3},public_a[4]={93,184,216,34};
static uint8_t query[29],buffer[EG_HTTP_MAX+1];
static void put16(uint8_t *p,unsigned n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static void put32(uint8_t *p,uint32_t n) {
    p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;
}
static size_t begin(uint8_t *p,unsigned count) {
    memcpy(p,query,29);put16(p+2,0x8180);put16(p+6,count);return 29;
}
static size_t rr(uint8_t *p,size_t at,const uint8_t *owner,size_t owner_n,
    unsigned type,uint32_t ttl,const uint8_t *data,size_t n) {
    CHECK(at+owner_n+10+n<=EG_DNS_MAX+1);
    memcpy(p+at,owner,owner_n);at+=owner_n;
    put16(p+at,type);put16(p+at+2,1);put32(p+at+4,ttl);put16(p+at+8,(unsigned)n);at+=10;
    memcpy(p+at,data,n);return at+n;
}
static size_t a(uint8_t *p,size_t at,const uint8_t *ip,uint32_t ttl) {
    static const uint8_t pointer[2]={0xc0,12};return rr(p,at,pointer,2,1,ttl,ip,4);
}
static int zero(const void *p,size_t n) {
    const unsigned char *b=p;size_t i;for(i=0;i<n;i++)if(b[i])return 0;return 1;
}
static void dns_tests(void) {
    uint8_t p[513],copy[513],other[4]={10,0,2,2};size_t n,i,written;
    struct eg_dns_answer out;
    CHECK(eg_dns_query(0x1234,query,sizeof query,&written)==EG_OK && written==29);
    memset(copy,42,sizeof copy);written=9;
    CHECK(eg_dns_query(1,copy,28,&written)==EG_DNS_SIZE && written==0 && copy[0]==42);
    n=a(p,begin(p,2),public_a,300);n=a(p,n,(const uint8_t[]){1,1,1,1},27);
    CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_OK);
    CHECK(out.count==2 && out.lease_seconds==27 && !memcmp(out.ipv4[0],public_a,4) && out.ipv4[1][0]==1);
    for(i=0;i<n;i++) {
        memset(&out,42,sizeof out);
        CHECK(eg_dns_reply(query,29,p,i,source,53,&out)!=EG_OK && zero(&out,sizeof out));
    }
    CHECK(eg_dns_reply(query,29,p,513,source,53,&out)==EG_DNS_SIZE);
    CHECK(eg_dns_reply(query,29,p,n,other,53,&out)==EG_DNS_SOURCE);
    CHECK(eg_dns_reply(query,29,p,n,source,54,&out)==EG_DNS_SOURCE);
    CHECK(eg_dns_reply(query,28,p,n,source,53,&out)==EG_DNS_QUERY);
    for(i=0;i<29;i++) {
        uint8_t bad_query[29];memcpy(bad_query,query,29);bad_query[i]^=1;
        CHECK(eg_dns_reply(bad_query,29,p,n,source,53,&out)!=EG_OK);
    }
    { const unsigned flags[]={0x0180,0x8380,0x8183,0x8980,0x8080,0x81c0};
      for(i=0;i<sizeof flags/sizeof flags[0];i++) {
          memcpy(copy,p,n);put16(copy+2,flags[i]);
          CHECK(eg_dns_reply(query,29,copy,n,source,53,&out)!=EG_OK);
      }
    }
    memcpy(copy,p,n);put16(copy+6,17);CHECK(eg_dns_reply(query,29,copy,n,source,53,&out)==EG_DNS_RECORD);
    memcpy(copy,p,n);put16(copy+8,16);put16(copy+10,16);CHECK(eg_dns_reply(query,29,copy,n,source,53,&out)==EG_DNS_RECORD);
    memcpy(copy,p,n);copy[n]=0;CHECK(eg_dns_reply(query,29,copy,n+1,source,53,&out)==EG_DNS_RECORD);
    n=a(p,begin(p,1),public_a,0);CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_TTL);
    { const uint8_t bad[][4]={{0,1,2,3},{10,0,2,2},{127,0,0,1},{169,254,1,2},
        {100,64,0,1},{172,16,0,1},{192,0,0,8},{192,0,2,1},{192,168,0,1},
        {198,18,1,1},{198,51,100,1},{203,0,113,1},{224,0,0,1},{240,0,0,1}};
      for(i=0;i<sizeof bad/sizeof bad[0];i++) {
          n=a(p,begin(p,2),public_a,60);n=a(p,n,bad[i],60);
          CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_ADDRESS && !out.count);
      }
    }
    CHECK(eg_ipv4_global((const uint8_t[]){192,0,0,9}));CHECK(eg_ipv4_global((const uint8_t[]){192,0,0,10}));
    for(i=4;i<=5;i++) {
        size_t j;n=begin(p,(unsigned)i);for(j=0;j<i;j++)n=a(p,n,public_a,60);
        CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==(i==4?EG_OK:EG_DNS_ADDRESS));
    }
    /* Checked compressed names, CNAME ownership/TTL and unrelated glue. */
    { static const uint8_t owner[]={0xc0,12},alias[]={3,'w','w','w',7,'e','x','a','m','p','l','e',3,'c','o','m',0};
      static const uint8_t evil[]={4,'e','v','i','l',0};
      n=begin(p,2);n=rr(p,n,owner,2,5,37,alias,sizeof alias);n=rr(p,n,alias,sizeof alias,1,300,public_a,4);
      put16(p+10,1);n=rr(p,n,evil,sizeof evil,1,60,other,4);
      CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_OK && out.count==1 && out.lease_seconds==37);
      n=begin(p,1);n=rr(p,n,owner,2,5,60,owner,2);
      CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_CHAIN);
      n=begin(p,2);n=rr(p,n,owner,2,5,60,alias,sizeof alias);n=a(p,n,public_a,60);
      CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_CHAIN);
      n=begin(p,1);n=rr(p,n,owner,2,1,60,public_a,3);
      CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_RECORD);
      n=a(p,begin(p,1),public_a,60);
      p[29]=0xc0;p[30]=29;CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_NAME);
      p[30]=255;CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_NAME);
      p[29]=0x40;CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==EG_DNS_NAME);
      p[29]=0xc0;CHECK(eg_dns_reply(query,29,p,30,source,53,&out)==EG_DNS_NAME);
    }
    for(i=7;i<=8;i++) {
        size_t j;uint8_t from[3]={1,'a',0},to[3]={1,'a',0};
        static const uint8_t owner[2]={0xc0,12};
        n=begin(p,(unsigned)i+1);
        for(j=0;j<i;j++) {
            to[1]=(uint8_t)('a'+j);
            if(!j)n=rr(p,n,owner,2,5,60,to,3);
            else { from[1]=(uint8_t)('a'+j-1);n=rr(p,n,from,3,5,60,to,3); }
        }
        n=rr(p,n,to,3,1,60,public_a,4);
        CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==(i==7?EG_OK:EG_DNS_CHAIN));
    }
    for(i=253;i<=254;i++) {
        uint8_t longname[256],pointer[2]={0xc0,41};size_t j,at=0;
        static const uint8_t owner[2]={0xc0,12};
        for(j=0;j<4;j++) {
            unsigned len=j==3?(unsigned)(i-192):63;
            longname[at++]=(uint8_t)len;memset(longname+at,'a',len);at+=len;
        }
        longname[at++]=0;n=begin(p,2);n=rr(p,n,owner,2,5,60,longname,at);
        n=rr(p,n,pointer,2,1,60,public_a,4);
        CHECK(eg_dns_reply(query,29,p,n,source,53,&out)==(i==253?EG_OK:EG_DNS_NAME));
    }
    CHECK(eg_dns_reply(NULL,29,p,n,source,53,&out)==EG_ARGUMENT);
}
static void reply_check(const char *wire,enum eg_status wanted) {
    struct eg_http_facts f;memset(&f,42,sizeof f);
    CHECK(eg_http_response((const uint8_t*)wire,strlen(wire),1,&f)==wanted);
    if(wanted!=EG_OK)CHECK(zero(&f,sizeof f));
}
static void http_tests(void) {
    static const char length[]="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample Domain";
    static const char chunks[]="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n7\r\nExample\r\n7\r\n Domain\r\n0\r\n\r\n";
    static const char close_body[]="HTTP/1.0 200 OK\r\nContent-Type: text/html\r\n\r\nExample Domain";
    struct eg_http_facts f;size_t i,n,written;
    uint8_t request[187];char nonce[49];memset(nonce,'a',48);nonce[48]=0;
    CHECK(eg_http_request(nonce,48,request,sizeof request,&written)==EG_OK && written==186);
    CHECK(!memcmp(request,"GET / HTTP/1.1\r\nHost: example.com\r\n",33));
    CHECK(!memcmp(request+written-23,"\r\nConnection: close\r\n\r\n",23));
    request[0]=42;CHECK(eg_http_request(nonce,48,request,185,&written)==EG_HTTP_LIMIT && !written && request[0]==42);
    nonce[0]='A';CHECK(eg_http_request(nonce,48,request,sizeof request,&written)==EG_ARGUMENT && !written);
    CHECK(eg_http_request(nonce,SIZE_MAX,request,sizeof request,&written)==EG_ARGUMENT);
    CHECK(eg_http_response(NULL,0,0,&f)==EG_MORE && zero(&f,sizeof f));
    CHECK(eg_http_response(NULL,0,1,&f)==EG_HTTP_TRUNCATED);
    CHECK(eg_http_response((const uint8_t*)length,strlen(length),0,&f)==EG_MORE && f.framing_complete && !f.eof_observed);
    CHECK(eg_http_response((const uint8_t*)length,strlen(length),1,&f)==EG_OK && f.body_bytes==14 && f.eof_observed && f.framing==EG_HTTP_LENGTH);
    CHECK(eg_http_response((const uint8_t*)chunks,strlen(chunks),0,&f)==EG_MORE && f.framing_complete && f.body_bytes==14);
    CHECK(eg_http_response((const uint8_t*)chunks,strlen(chunks),1,&f)==EG_OK && f.framing==EG_HTTP_CHUNKED);
    CHECK(eg_http_response((const uint8_t*)close_body,strlen(close_body),0,&f)==EG_MORE && !f.framing_complete);
    CHECK(eg_http_response((const uint8_t*)close_body,strlen(close_body),1,&f)==EG_OK && f.framing==EG_HTTP_EOF);
    for(i=0;i<strlen(length);i++) {
        CHECK(eg_http_response((const uint8_t*)length,i,0,&f)==EG_MORE);
        CHECK(eg_http_response((const uint8_t*)length,i,1,&f)!=EG_OK);
    }
    for(i=0;i<strlen(chunks);i++) {
        CHECK(eg_http_response((const uint8_t*)chunks,i,0,&f)==EG_MORE);
        CHECK(eg_http_response((const uint8_t*)chunks,i,1,&f)!=EG_OK);
    }
    reply_check("HTTP/1.1 302 Found\r\nContent-Type: text/html\r\n\r\nExample Domain",EG_HTTP_STATUS);
    reply_check("HTTP/1.1 200\001OK\r\nContent-Type: text/html\r\n\r\nExample Domain",EG_HTTP_STATUS);
    reply_check("HTTP/1.1 200 OK\r\n Content-Type: text/html\r\n\r\nExample Domain",EG_HTTP_HEADER);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/htmlx\r\n\r\nExample Domain",EG_HTTP_HEADER);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nX: a\r\nx: b\r\n\r\nExample Domain",EG_HTTP_HEADER);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Encoding: gzip\r\n\r\nExample Domain",EG_HTTP_ENCODING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\nTransfer-Encoding: chunked\r\n\r\n",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 999999\r\n\r\n",EG_HTTP_LIMIT);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 0000014\r\n\r\nExample Domain",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: +14\r\n\r\nExample Domain",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nExample DomainX",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\n\r\nSomething Else",EG_HTTP_CONTENT);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\nffffffff\r\n",EG_HTTP_LIMIT);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n100000000\r\n",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\ne;extension\r\n",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\ne\r\nExample DomainXX0\r\n\r\n",EG_HTTP_FRAMING);
    reply_check("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\ne\r\nExample Domain\r\n0\r\nTrailer: no\r\n\r\n",EG_HTTP_FRAMING);
    /* Exact backend delimiter-start cap8192, not a silently stricter total. */
    { const char *head="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: 14\r\nX:";
      size_t prefix=strlen(head),pad=8192-prefix;
      memcpy(buffer,head,prefix);memset(buffer+prefix,'a',pad);memcpy(buffer+8192,"\r\n\r\nExample Domain",18);
      CHECK(eg_http_response(buffer,8210,1,&f)==EG_OK && f.header_bytes==8196);
      memmove(buffer+8193,buffer+8192,18);buffer[8192]='a';CHECK(eg_http_response(buffer,8211,1,&f)==EG_HTTP_LIMIT);
    }
    { size_t prefix=strlen(close_body)-14;
      memcpy(buffer,close_body,prefix);memset(buffer+prefix,'x',EG_HTTP_MAX-prefix);memcpy(buffer+prefix,"Example Domain",14);
      CHECK(eg_http_response(buffer,EG_HTTP_MAX,1,&f)==EG_OK && f.body_bytes==EG_HTTP_MAX-prefix);
      CHECK(eg_http_response(buffer,EG_HTTP_MAX+1,1,&f)==EG_HTTP_LIMIT);
    }
    for(i=1023;i<=1024;i++) {
        const char *head="HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nTransfer-Encoding: chunked\r\n\r\n";
        size_t j;n=strlen(head);memcpy(buffer,head,n);
        for(j=0;j<i;j++) { memcpy(buffer+n,"1\r\nx\r\n",6);buffer[n+3]=j<14?(uint8_t)"Example Domain"[j]:'x';n+=6; }
        memcpy(buffer+n,"0\r\n\r\n",5);n+=5;
        CHECK(eg_http_response(buffer,n,1,&f)==(i==1023?EG_OK:EG_HTTP_FRAMING));
    }
    CHECK(eg_http_response((const uint8_t*)length,strlen(length),2,&f)==EG_ARGUMENT);
}
static size_t read_file(const char *dir,const char *name,uint8_t *data,size_t cap) {
    char path[2048];FILE *f;size_t n;int count=snprintf(path,sizeof path,"%s/%s",dir,name);
    CHECK(count>0 && (size_t)count<sizeof path);f=fopen(path,"rb");CHECK(f!=NULL);
    n=fread(data,1,cap,f);CHECK(!ferror(f) && fgetc(f)==EOF);CHECK(!fclose(f));return n;
}
static void fixtures(const char *dir) {
    uint8_t q[512],r[512],request[256],built[256],nonce[49];struct eg_dns_answer dns;
    struct eg_http_facts http;size_t qn,rn,n,i,written,reqn,non;
    qn=read_file(dir,"dns-query.bin",q,sizeof q);rn=read_file(dir,"dns-reply.bin",r,sizeof r);
    CHECK(eg_dns_reply(q,qn,r,rn,source,53,&dns)==EG_OK && dns.count);
    n=read_file(dir,"http-response.bin",buffer,sizeof buffer);
    for(i=0;i<n;i++) {
        CHECK(eg_http_response(buffer,i,0,&http)==EG_MORE);
        CHECK(eg_http_response(buffer,i,1,&http)!=EG_OK);
    }
    CHECK(eg_http_response(buffer,n,0,&http)==EG_MORE && http.framing_complete);
    CHECK(eg_http_response(buffer,n,1,&http)==EG_OK);
    non=read_file(dir,"nonce.txt",nonce,sizeof nonce);CHECK(non==48);
    reqn=read_file(dir,"http-request-observed.bin",request,sizeof request);
    CHECK(eg_http_request((const char*)nonce,non,built,sizeof built,&written)==EG_OK && written==reqn && !memcmp(built,request,reqn));
    printf("REPLAY first=%u.%u.%u.%u lease=%u candidates=%u wire=%zu headers=%zu body=%zu request=%zu\n",
        dns.ipv4[0][0],dns.ipv4[0][1],dns.ipv4[0][2],dns.ipv4[0][3],dns.lease_seconds,dns.count,n,http.header_bytes,http.body_bytes,written);
}
static size_t unhex(const char *text,uint8_t *out,size_t cap) {
    size_t i,n=strlen(text);if(!strcmp(text,"-"))return 0;
    CHECK(!(n%2) && n/2<=cap);
    for(i=0;i<n/2;i++) {
        unsigned hi,lo;char a=text[2*i],b=text[2*i+1];
        CHECK((a>='0'&&a<='9')||(a>='a'&&a<='f'));CHECK((b>='0'&&b<='9')||(b>='a'&&b<='f'));
        hi=a<='9'?(unsigned)(a-'0'):(unsigned)(a-'a'+10);lo=b<='9'?(unsigned)(b-'0'):(unsigned)(b-'a'+10);
        out[i]=(uint8_t)((hi<<4)|lo);
    }
    return n/2;
}
static int oracle(void) {
    static char line[2*(EG_HTTP_MAX+1)+128];static uint8_t wire[EG_HTTP_MAX+1];
    while(fgets(line,sizeof line,stdin)) {
        char *words[6],*p=line,*next;unsigned count=0;size_t n=strlen(line);
        CHECK(n && line[n-1]=='\n');line[n-1]=0;
        while(p && count<6) { words[count++]=p;next=strchr(p,' ');if(next)*next++=0;p=next; }
        CHECK(!p);
        if(count==3 && !strcmp(words[0],"H")) {
            struct eg_http_facts f;enum eg_status s;int eof;
            CHECK(!strcmp(words[1],"0")||!strcmp(words[1],"1"));eof=words[1][0]-'0';
            n=unhex(words[2],wire,sizeof wire);s=eg_http_response(wire,n,eof,&f);
            printf("H %u %u %u %u %zu %zu %u\n",(unsigned)s,(unsigned)f.framing,f.framing_complete,f.eof_observed,f.header_bytes,f.body_bytes,f.example_content);
        } else if(count==5 && !strcmp(words[0],"D")) {
            uint8_t q[513],src[4];size_t qn,sn;char *end;unsigned long port;
            struct eg_dns_answer a;enum eg_status s;
            qn=unhex(words[1],q,sizeof q);sn=unhex(words[2],src,sizeof src);CHECK(sn==4);
            port=strtoul(words[3],&end,10);CHECK(!*end && port<=65535);
            n=unhex(words[4],wire,sizeof wire);s=eg_dns_reply(q,qn,wire,n,src,(uint16_t)port,&a);
            printf("D %u %u %u %02x%02x%02x%02x\n",(unsigned)s,a.count,a.lease_seconds,a.ipv4[0][0],a.ipv4[0][1],a.ipv4[0][2],a.ipv4[0][3]);
        } else return 2;
    }
    return ferror(stdin)?1:0;
}
int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--oracle"))return oracle();
    if(argc==2 && !strcmp(argv[1],"--classify")) {
        char line[32];while(fgets(line,sizeof line,stdin)) {
            unsigned v;char tail;uint8_t ip[4];
            if(sscanf(line,"%8x%c",&v,&tail)!=2 || tail!='\n')return 2;
            put32(ip,(uint32_t)v);printf("%d\n",eg_ipv4_global(ip));
        }
        return ferror(stdin)?1:0;
    }
    dns_tests();http_tests();if(argc==2)fixtures(argv[1]);else CHECK(argc==1);
    printf("PASS %u assertions; portable host validation only, no Darwin/network execution\n",checks);return 0;
}
