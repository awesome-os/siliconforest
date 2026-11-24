#include "inventory.h"
#include <string.h>
static uint16_t u16(const uint8_t *p){return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int dlf_inventory(const uint8_t *raw,size_t n,struct dlf_route_set *out,struct dlf_route_set *work){
    if(!out||!work||out==work||(!raw&&n)||n>DLF_DUMP_LIMIT)return -1;
    memset(work,0,sizeof(*work));size_t off=0;
    while(off<n){
        if(n-off<92||work->count==DLF_ROUTE_LIMIT)return -1;
        const uint8_t *p=raw+off;size_t len=u16(p);
        if(len<92||len>n-off||p[2]!=5||p[3]!=4||u32(p+24)||(u32(p+12)&~255u))return -1;
        struct dlf_route_key k={0};k.index=u16(p+4);k.flags=u32(p+8);k.addresses=u32(p+12);
        if(!(k.addresses&1)||len-92>sizeof(k.body))return -1;
        size_t pos=92;
        for(unsigned bit=0;bit<8;bit++){
            if(!(k.addresses&(1u<<bit)))continue;
            if(len-pos<2)return -1;
            size_t size=p[pos],step=size?((size+3)&~(size_t)3):4;
            if(step>len-pos||size>128||size==1)return -1;
            if(!size){if(bit!=2&&bit!=3)return -1;}
            for(size_t i=size;i<step;i++)if(p[pos+i])return -1;
            if(bit==0&&(size!=16||p[pos+1]!=2))return -1;
            pos+=step;
        }
        if(pos!=len)return -1;
        k.length=(uint16_t)(len-92);memcpy(k.body,p+92,k.length);
        size_t i=work->count;
        while(i&&memcmp(&work->key[i-1],&k,sizeof(k))>0){work->key[i]=work->key[i-1];i--;}
        if((i&& !memcmp(&work->key[i-1],&k,sizeof(k))) ||
           (i<work->count&&!memcmp(&work->key[i],&k,sizeof(k))))return -1;
        work->key[i]=k;work->count++;off+=len;
    }
    *out=*work;return 0;
}
int dlf_inventory_equal(const struct dlf_route_set *a,const struct dlf_route_set *b){
    return a&&b&&a->count<=DLF_ROUTE_LIMIT&&a->count==b->count&&
        !memcmp(a->key,b->key,a->count*sizeof(a->key[0]));
}
int dlf_inventory_has_default(const struct dlf_route_set *set){
    if(!set||set->count>DLF_ROUTE_LIMIT)return -1;
    for(size_t i=0;i<set->count;i++){
        const struct dlf_route_key *k=&set->key[i];
        if(k->length>sizeof(k->body)||(k->addresses&~255u)||!(k->addresses&1))return -1;
        size_t pos=0;int dst_zero=0,mask_zero=1;
        for(unsigned bit=0;bit<8;bit++){
            if(!(k->addresses&(1u<<bit)))continue;
            if(k->length-pos<2)return -1;
            const uint8_t *p=k->body+pos;size_t size=p[0],step=size?((size+3)&~(size_t)3):4;
            if(size==1||step>k->length-pos||size>128)return -1;
            if(bit==0){if(size!=16||p[1]!=2)return -1;dst_zero=!(p[4]|p[5]|p[6]|p[7]);}
            if(bit==2)for(size_t j=4;j<8&&j<size;j++)if(p[j])mask_zero=0;
            pos+=step;
        }
        if(pos!=k->length)return -1;
        if(dst_zero&&mask_zero&&!(k->flags&4u))return 1;
    }
    return 0;
}
