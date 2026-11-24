#include "dl_spi_hid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)
static void put16(uint8_t *p,unsigned n) { p[0]=(uint8_t)n; p[1]=(uint8_t)(n>>8); }
static size_t message(uint8_t *m,size_t n,int descriptor)
{
    memset(m,0,n+10);m[0]=descriptor?0x20:0x10;m[1]=descriptor?0x10:1;m[3]=37;
    put16(m+6,(unsigned)n);for(size_t i=0;i<n;i++)m[i+8]=(uint8_t)(i*13+7);
    put16(m+n+8,dlsp_crc16(m,n+8));return n+10;
}
static void packet(uint8_t *p,unsigned flags,unsigned dev,size_t off,size_t remain,const uint8_t *data,size_t n)
{
    CHECK(n<=246);memset(p,0,256);p[0]=(uint8_t)flags;p[1]=(uint8_t)dev;
    put16(p+2,(unsigned)off);put16(p+4,(unsigned)remain);put16(p+6,(unsigned)n);
    if(n)memcpy(p+8,data,n);put16(p+254,dlsp_crc16(p,254));
}
static int fragments(struct dlsp_state *s,const uint8_t *m,size_t n,size_t step,int descriptor,struct dlsp_message *out)
{
    uint8_t p[256];int result=99;
    for(size_t off=0;off<n;) {
        size_t part=n-off<step?n-off:step;
        packet(p,descriptor?0x40:0x20,descriptor?0xd0:1,off,n-off-part,m+off,part);
        result=dlsp_push(s,p,256,out);off+=part;
        if(off<n) {CHECK(result==DLSP_WAIT);CHECK(out->payload==NULL);}
    }
    return result;
}
int main(void)
{
    struct arena { uint64_t first;struct dlsp_state s;uint64_t last; } *a=calloc(1,sizeof(*a));
    CHECK(a!=NULL);a->first=0x12345678;a->last=0x87654321;
    struct dlsp_state *s=&a->s;struct dlsp_message out;
    uint8_t p[256],m[2048],small[246];size_t n;
    CHECK(dlsp_crc16((const uint8_t *)"123456789",9)==0xbb3d);
    CHECK(dlsp_crc16(NULL,0)==0);puts("crc golden vector");
    memset(p,0xa5,sizeof(p));uint8_t unchanged[256];memcpy(unchanged,p,256);
    CHECK(dlsp_request(0xd0,0x20,0x10,1,255,513,NULL,0,p)==DLSP_INVALID);
    CHECK(!memcmp(p,unchanged,256));
    CHECK(dlsp_request(4,0x20,1,0,0,0,NULL,0,p)==DLSP_INVALID);
    CHECK(dlsp_request(1,0x20,1,0,0,0,NULL,237,p)==DLSP_INVALID);
    CHECK(dlsp_request(1,0x20,1,0,0,0,NULL,1,p)==DLSP_INVALID);
    CHECK(dlsp_request(0xd0,0x20,0x10,1,255,512,NULL,0,p)==DLSP_WAIT);
    const uint8_t first[]={0x40,0xd0,0,0,0,0,10,0,0x20,0x10,1,255,0,2,0,0};
    CHECK(!memcmp(p,first,sizeof(first)));CHECK(dlsp_crc16(p,254)==(uint16_t)(p[254]|(unsigned)p[255]<<8));
    puts("request fields and output preservation");
    /* Test every supported total length, and one-byte fragment boundaries. */
    for(size_t payload=1;payload<=2038;payload++) {
        dlsp_init(s);n=message(m,payload,0);
        CHECK(fragments(s,m,n,payload%246+1,0,&out)==DLSP_MESSAGE);
        CHECK(out.payload_length==payload && out.device==1 && out.flags==0x20 && out.id==37);
        CHECK(!memcmp(out.payload,m+8,payload));CHECK(!s->active);
    }
    puts("all 2038 supported message lengths");
    dlsp_init(s);n=message(m,512,1);packet(p,0x40,0xd0,0,n-246,m,246);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_WAIT);
    size_t short_n=message(small,5,0);packet(p,0x20,1,0,0,small,short_n);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_MESSAGE);CHECK(out.payload_length==5 && s->used==246);
    packet(p,0x40,0xd0,246,n-492,m+246,246);CHECK(dlsp_push(s,p,256,&out)==DLSP_WAIT);
    packet(p,0x40,0xd0,492,0,m+492,n-492);CHECK(dlsp_push(s,p,256,&out)==DLSP_MESSAGE);
    CHECK(out.payload_length==512 && !memcmp(out.payload,m+8,512));
    dlsp_init(s);n=message(m,513,1);CHECK(fragments(s,m,n,246,1,&out)==DLSP_INVALID);
    CHECK(s->last_error==DLSP_E_DESCRIPTOR && out.payload==NULL);puts("interleaving and descriptor cap");
    n=message(m,20,0);packet(p,0x20,1,0,0,m,n);p[50]^=1;
    CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && s->last_error==DLSP_E_PACKET_CRC && !out.payload);
    m[n-1]^=1;packet(p,0x20,1,0,0,m,n);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && s->last_error==DLSP_E_MESSAGE_CRC && !out.payload);
    m[n-1]^=1;packet(p,0x80,1,0,0,m,n);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && s->last_error==DLSP_E_FLAGS);
    packet(p,0x20,4,0,0,m,n);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && s->last_error==DLSP_E_DEVICE);
    packet(p,0x20,1,65535,65535,m,n);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && s->last_error==DLSP_E_LENGTH);
    puts("packet/message CRC, flags, device and arithmetic refusals");
    dlsp_init(s);n=message(m,300,0);packet(p,0x20,1,0,n-246,m,246);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_WAIT);
    packet(p,0x20,2,246,0,m+246,n-246);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID && !s->active);
    packet(p,0x20,1,246,0,m+246,n-246);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID);
    packet(p,0x20,1,0,n-246,m,246);CHECK(dlsp_push(s,p,256,&out)==DLSP_WAIT);
    packet(p,0x40,1,246,0,m+246,n-246);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID);
    packet(p,0x20,1,0,n-246,m,246);CHECK(dlsp_push(s,p,256,&out)==DLSP_WAIT);
    packet(p,0x20,1,245,1,m+246,n-246);CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID);
    puts("fragment identity/order reset");
    const uint8_t boot[]={0xa0,0x80,0,0};packet(p,0x81,0xee,19,65535,boot,4);
    CHECK(dlsp_push(s,p,256,&out)==DLSP_BOOT && !s->active && out.payload==NULL);
    p[255]^=1;CHECK(dlsp_push(s,p,256,&out)==DLSP_INVALID);puts("CRC-valid boot notification only");
    CHECK(dlsp_push(s,p,255,&out)==DLSP_INVALID && !out.payload);
    CHECK(dlsp_push(s,NULL,256,&out)==DLSP_INVALID);
    CHECK(dlsp_push(NULL,p,256,&out)==DLSP_INVALID && !out.payload);
    CHECK(dlsp_push(s,p,256,NULL)==DLSP_INVALID);puts("argument refusals");
    uint8_t unaligned[257];n=message(m,10,0);packet(unaligned+1,0x20,1,0,0,m,n);
    CHECK(dlsp_push(s,unaligned+1,256,&out)==DLSP_MESSAGE);puts("unaligned wire storage");
    uint32_t random=0x183efad;
    for(unsigned i=0;i<50000;i++) {
        for(unsigned j=0;j<256;j++){random^=random<<13;random^=random>>17;random^=random<<5;p[j]=(uint8_t)random;}
        if(!(i%3)){p[0]=0x20;p[1]=1;put16(p+6,i%247);put16(p+254,dlsp_crc16(p,254));}
        int result=dlsp_push(s,p,256,&out);
        CHECK(result>=DLSP_INVALID && result<=DLSP_BOOT);
        if(result==DLSP_INVALID || result==DLSP_WAIT)CHECK(out.payload==NULL);
        CHECK(s->used<=2048 && s->total<=2048 && a->first==0x12345678 && a->last==0x87654321);
    }
    puts("50000 bounded malformed wire inputs");free(a);return 0;
}
