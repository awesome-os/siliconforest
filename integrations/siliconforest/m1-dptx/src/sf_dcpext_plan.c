// SPDX-License-Identifier: MIT
/* Resource layout: Omacom linux-aurora 6a08a08e t8103.dtsi/t8103-jxxx.dtsi.
 * ADT conversion follows m1n1 src/kboot.c and m1-gpu-dt/src/dispdt.c,
 * Copyright The Asahi Linux Contributors (MIT). See NOTICE.
 */
#include "sf_dcpext_plan.h"
#include <string.h>
#include <errno.h>
static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint64_t le64(const uint8_t *p)
{ return le32(p)|((uint64_t)le32(p+4)<<32); }
static int region(const struct sf_dcpext_adt *a,const char *path,const char *prop,
                  unsigned index,int arm_io,struct sf_dcpext_region *out)
{
    size_t bytes=0;
    const uint8_t *raw=a->property(a->ctx,path,prop,&bytes);
    if(!raw || bytes%16 || bytes/16<=index || (!arm_io && bytes!=16))return -ENOENT;
    uint64_t p=le64(raw+16*index),len=le64(raw+16*index+8);
    uint64_t bias=arm_io?0x200000000ULL:0;
    if(!p || !len || p>UINT64_MAX-bias || len>UINT64_MAX-(p+bias))return -EINVAL;
    out->phys=p+bias;out->bytes=len;return 0;
}
static int expect(const struct sf_dcpext_adt *a,const char *path,unsigned index,
                  uint64_t phys,uint64_t min,struct sf_dcpext_region *out)
{
    int ret=region(a,path,"reg",index,1,out);
    if(ret)return ret;
    return out->phys==phys && out->bytes>=min?0:-ENODEV;
}
int sf_dcpext_plan_t8103(const struct sf_dcpext_adt *a,uint32_t chip,
                        const uint32_t version[3],struct sf_dcpext_plan *out)
{
    if(!a || !a->property || !version || !out)return -EINVAL;
    if(chip!=0x8103)return -ENODEV;
    size_t bytes=0;const char *board=a->property(a->ctx,"/","compatible",&bytes);
    if(!board || bytes<7 || memcmp(board,"J293AP",7))return -ENODEV;
    // Only firmware implemented by the installed DCP port; do not silently
    // route an unknown IOMFB ABI through the internal panel's methods.
    if(version[0]!=13 || !((version[1]==5) || (version[1]==6&&version[2]==1)))return -EOPNOTSUPP;
    struct sf_dcpext_plan p={0};int ret;
#define EXPECT(path,index,base,min,dest) do { ret=expect(a,path,index,base,min,&p.dest);if(ret)return ret; } while(0)
    EXPECT("/arm-io/dcpext",0,0x271c00000ULL,0xc000,coproc);
    // Linux's coprocessor window excludes the separate mailbox registers.
    p.coproc.bytes=0x4000;p.mailbox.phys=p.coproc.phys+0x8000;p.mailbox.bytes=0x4000;
    static const uint64_t base[]={0x270000000ULL,0x271320000ULL,0x271344000ULL,0x271800000ULL,0x23b3d0000ULL};
    static const uint64_t size[]={0x118000,0x4000,0x4000,0x800000,0x4000};
    for(unsigned i=0;i<5;++i) {ret=expect(a,"/arm-io/dispext0",i,base[i],size[i],&p.disp[i]);if(ret)return ret;p.disp[i].bytes=size[i];}
    EXPECT("/arm-io/dart-dcpext",0,0x27130c000ULL,0x4000,dcp_dart);
    EXPECT("/arm-io/dart-dispext0",0,0x271304000ULL,0x4000,disp_dart);
    // Stream 4 (piodma) is in the same display DART, not reg1's second bank.
    p.piodma_dart=p.disp_dart;
    EXPECT("/arm-io/dispext0",5,0x23b738000ULL,0x1000,bw_scratch);
    EXPECT("/arm-io/dispext0",6,0x23bc3c000ULL,0x1000,bw_doorbell);
#undef EXPECT
    ret=region(a,"/chosen/carveout-memory-map","region-id-73",0,0,&p.firmware[0]);if(ret)return ret;
    ret=region(a,"/chosen/carveout-memory-map","region-id-74",0,0,&p.firmware[1]);if(ret)return ret;
    uint64_t a0=p.firmware[0].phys,a1=p.firmware[1].phys;
    if(a0<a1+p.firmware[1].bytes && a1<a0+p.firmware[0].bytes)return -EINVAL;
    const uint8_t *irq=a->property(a->ctx,"/arm-io/dcpext","interrupts",&bytes);
    if(!irq || bytes!=16)return -EINVAL;
    for(unsigned i=0;i<4;++i)p.mailbox_irq[i]=le32(irq+4*i);
    for(unsigned i=0;i<4;++i)for(unsigned j=i+1;j<4;++j)if(p.mailbox_irq[j]<p.mailbox_irq[i]) {
        uint32_t x=p.mailbox_irq[i];p.mailbox_irq[i]=p.mailbox_irq[j];p.mailbox_irq[j]=x;
    }
    for(unsigned i=0;i<4;++i)if(p.mailbox_irq[i]!=466+i)return -ENODEV;
    memcpy(p.firmware_version,version,sizeof(p.firmware_version));
    p.firmware_compat[0]=13;p.firmware_compat[1]=5;
    p.typec_port=1;p.dptx_phy=1;p.remote_core=0;p.stream_count=1;
    *out=p;return 0;
}
