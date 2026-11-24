/* Original LP64 ARM64 routing wire codec. Actual SDK assertions are separate. */
#include "route_wire.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t ip(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void w16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static void addr(uint8_t *p,uint32_t v){p[0]=16;p[1]=2;for(unsigned i=0;i<4;i++)p[4+i]=(uint8_t)(v>>(24-i*8));}
int dlf_encode(uint8_t *out,size_t cap,size_t *length,uint32_t pid,uint8_t type,const struct dlf_owner *o){
    if(!out||!length||!pid||pid>INT32_MAX||(type!=1&&type!=4))return -1;
    uint8_t b[192]={0};size_t n=0;
    if(type==1){
        if(!o||!o->index||strncmp(o->name,"utun",4))return -1;
        for(;n<sizeof(o->name)&&o->name[n];n++);
        if(n<5||n==sizeof(o->name))return -1;
        for(size_t i=4;i<n;i++)if(o->name[i]<'0'||o->name[i]>'9')return -1;
    }
    /* A zero-length REQUEST netmask makes rt_xaddrs return early, skipping IFP/IFA.
     * Always send a full sockaddr_in for /0; only replies may compress it. */
    size_t ifp=type==1?((8+n+3)&~(size_t)3):8;
    size_t used=92+16+(type==1?16:0)+16+ifp+16;
    if(cap<used)return -1;
    w16(b,(uint16_t)used);b[2]=5;b[3]=type;w16(b+4,type==1?o->index:0);
    w32(b+8,type==1?0x803:0);w32(b+12,type==1?0x37:0x35);w32(b+16,pid);w32(b+20,1);
    size_t pos=92;addr(b+pos,0);pos+=16;
    if(type==1){addr(b+pos,DLF_PEER);pos+=16;}
    addr(b+pos,0);pos+=16;b[pos]=(uint8_t)(type==1?8+n:8);b[pos+1]=18;
    if(type==1){w16(b+pos+2,o->index);b[pos+5]=(uint8_t)n;memcpy(b+pos+8,o->name,n);}
    pos+=ifp;addr(b+pos,type==1?DLF_LOCAL:0);pos+=16;
    if(pos!=used)return -1;
    memcpy(out,b,used);*length=used;return 0;
}
int dlf_decode(const uint8_t *b,size_t n,uint32_t pid,uint8_t type,struct dlf_route *out){
    if(!b||!out||!pid||pid>INT32_MAX||(type!=1&&type!=4)||n<4||n>4096)return -1;
    if(b[2]==5&&(b[3]==12||b[3]==13||b[3]==14||b[3]==15||b[3]==16||b[3]==18||b[3]==19))return 1;
    if(n<92)return -1;
    if(u32(b+16)!=pid||u32(b+20)!=1)return 1;
    if(u16(b)!=n||b[2]!=5||b[3]!=type)return -1;
    struct dlf_route r={0};r.flags=u32(b+8);r.error=u32(b+24);r.index=u16(b+4);
    size_t pos=92;uint32_t mask=u32(b+12);
    if(mask!=(r.error&&type==4?0x35u:0x37u))return -1;
    for(unsigned k=0;k<8;k++){
        if(!(mask&(1u<<k)))continue;
        if(n-pos<2)return -1;
        const uint8_t *p=b+pos;size_t size=p[0],step=size?((size+3)&~(size_t)3):4;
        if(step>n-pos||size>128||size>n-pos)return -1;
        if(!size){
            if(k!=2||p[0]||p[1]||p[2]||p[3])return -1;
            r.have_netmask=1;r.netmask=0;pos+=step;continue;
        }
        if(size<2)return -1;
        for(size_t i=size;i<step;i++)if(p[i])return -1;
        if(k==0||k==1||k==5){
            if(size!=16||p[1]!=2||p[2]||p[3])return -1;
            for(size_t i=8;i<16;i++)if(p[i])return -1;
            if(k==0){r.destination=ip(p+4);r.have_destination=1;}
            if(k==1){r.gateway=ip(p+4);r.have_gateway=1;}
            if(k==5){r.ifa=ip(p+4);r.have_ifa=1;}
        }else if(k==2){
            if(size>16||(p[1]!=0&&p[1]!=2))return -1;
            if((size>2&&p[2])||(size>3&&p[3]))return -1;
            for(size_t i=8;i<size;i++)if(p[i])return -1;
            uint8_t full[16]={0};memcpy(full,p,size);r.netmask=ip(full+4);r.have_netmask=1;
        }else if(k==4){
            if(size<8||p[1]!=18||p[6]||p[7]||p[5]>=sizeof(r.ifp_name)||8u+p[5]>size||
               p[4]!=((type==4&&!r.error)?1:0)||memchr(p+8,0,p[5]))return -1;
            size_t expected=8u+p[5];
            /* dlil_alloc_lladdr reserves at least sizeof(sockaddr_dl), rounded
             * to4, unlike our compact ADD request and its reflected ACK. */
            if(type==4&&!r.error){expected=(expected+3)&~(size_t)3;if(expected<20)expected=20;}
            if(size!=expected)return -1;
            for(size_t i=8u+p[5];i<size;i++)if(p[i])return -1;
            r.ifp_index=u16(p+2);memcpy(r.ifp_name,p+8,p[5]);r.have_ifp=1;
        }
        pos+=step;
    }
    if(pos!=n)return -1;
    *out=r;return 0;
}
