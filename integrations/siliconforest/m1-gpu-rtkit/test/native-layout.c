// SPDX-License-Identifier: MIT
#include "sf_rtkit_layout.h"
#include <assert.h>
#include <stdio.h>
static unsigned permutations;
static void check(const uint32_t irq[4],bool m3,uint64_t phys,uint64_t size)
{
    struct sf_rtkit_mbox_res r={};strcpy(r.name,"test");
    assert(!sf_rtkit_mbox_layout(m3,phys,size,irq,&r));
    assert(!strcmp(r.name,"test") && !memcmp(r.aic_listed,irq,16));
    assert(r.aic_send_empty==irq[0] || r.aic_send_empty==irq[1] || r.aic_send_empty==irq[2] || r.aic_send_empty==irq[3]);
    for(unsigned i=0;i<4;++i)assert(irq[i]>=r.aic_send_empty && irq[i]<=r.aic_send_empty+3);
    assert(r.aic_send_not_empty==r.aic_send_empty+1 && r.aic_recv_empty==r.aic_send_empty+2 && r.aic_recv_not_empty==r.aic_send_empty+3);
    assert(r.mbox_phys==phys+(m3?0:0x8000) && r.mbox_size==(m3?size:0x4000));
    assert(!strcmp(r.linux_compatible,m3?"apple,m3-mailbox-v2":"apple,asc-mailbox-v4"));
    ++permutations;
}
int main(void)
{
    for(unsigned a=0;a<4;++a)for(unsigned b=0;b<4;++b)for(unsigned c=0;c<4;++c)for(unsigned d=0;d<4;++d) {
        if(a==b || a==c || a==d || b==c || b==d || c==d)continue;
        uint32_t asc[]={466+a,466+b,466+c,466+d},m3[]={892+a,892+b,892+c,892+d};
        check(asc,false,0x271c00000,0x6c000);check(m3,true,0x501100000,0x144);
    }
    struct sf_rtkit_mbox_res r,saved;memset(&r,0xa5,sizeof(r));saved=r;
    uint32_t irq[]={4,5,6,7};
    for(unsigned m3=0;m3<2;++m3) {
        uint64_t minimum=m3?0xb0:0xc000;
        for(unsigned i=0;i<minimum;++i) {
            assert(sf_rtkit_mbox_layout(m3,0x10000,i,irq,&r)==-EINVAL && !memcmp(&r,&saved,sizeof(r)));
        }
        assert(sf_rtkit_mbox_layout(m3,UINT64_MAX-3,minimum,irq,&r)==-EINVAL && !memcmp(&r,&saved,sizeof(r)));
        assert(sf_rtkit_mbox_layout(m3,0,minimum,irq,&r)==-EINVAL);
    }
    uint32_t duplicate[]={4,5,5,7},gap[]={4,5,6,8},overflow[]={0xfffffffe,0xffffffff,0,1};
    assert(sf_rtkit_mbox_layout(true,0x10000,0x144,duplicate,&r)==-EINVAL);
    assert(sf_rtkit_mbox_layout(true,0x10000,0x144,gap,&r)==-EINVAL);
    assert(sf_rtkit_mbox_layout(true,0x10000,0x144,overflow,&r)==-EINVAL);
    assert(!memcmp(&r,&saved,sizeof(r)));
    printf("Native ASCv4/M3v2 layout: %u IRQ permutations, every undersized window, overflow/duplicate/gap refusal with unchanged output passed\n",permutations);
}
