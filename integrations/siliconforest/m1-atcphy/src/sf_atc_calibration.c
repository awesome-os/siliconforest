// SPDX-License-Identifier: MIT
/* Copyright The Asahi Linux Contributors
 * ADT lane grouping/base/span from m1n1 src/kboot_atc.c (MIT), in the
 * application order of the T8103 Linux ATC PHY. Permission in NOTICE.
 */
#include "sf_atc_calibration.h"
#include "sf_atc_tunables.h"
#include <errno.h>
static const char *const names[3][2][4]={
    {{"tunable_USB_LN0_AUSPMA_TX_TOP","tunable_USB_LN0_AUSPMA_RX_TOP","tunable_USB_LN0_AUSPMA_RX_SHM","tunable_USB_LN0_AUSPMA_RX_EQ"},
     {"tunable_USB_LN1_AUSPMA_TX_TOP","tunable_USB_LN1_AUSPMA_RX_TOP","tunable_USB_LN1_AUSPMA_RX_SHM","tunable_USB_LN1_AUSPMA_RX_EQ"}},
    {{"tunable_DP_LN0_AUSPMA_TX_TOP",0,0,0},{"tunable_DP_LN1_AUSPMA_TX_TOP",0,0,0}},
    {{"tunable_CIO_LN0_AUSPMA_TX_TOP","tunable_CIO_LN0_AUSPMA_RX_TOP","tunable_CIO_LN0_AUSPMA_RX_SHM","tunable_CIO_LN0_AUSPMA_RX_EQ"},
     {"tunable_CIO_LN1_AUSPMA_TX_TOP","tunable_CIO_LN1_AUSPMA_RX_TOP","tunable_CIO_LN1_AUSPMA_RX_SHM","tunable_CIO_LN1_AUSPMA_RX_EQ"}}
};
static const uint32_t offsets[2][4]={{0xc000,0x9000,0xb000,0xa000},{0x13000,0x10000,0x12000,0x11000}};
static bool same(const struct sf_atc_program *a,const struct sf_atc_program *b)
{
    return a->lanes==b->lanes && a->crossbar==b->crossbar && a->dp_lanes==b->dp_lanes &&
        a->usb3_lanes==b->usb3_lanes && a->usb4_lanes==b->usb4_lanes &&
        a->swap==b->swap && a->aux==b->aux && a->pipe==b->pipe && a->reversed==b->reversed;
}
static int group(struct sf_atc_cal_bundle *b,unsigned kind,unsigned lane)
{
    for(unsigned n=0;n<(kind==1?1u:4u);++n) {
        if(b->count==8)return EOVERFLOW;
        struct sf_atc_cal_entry *e=&b->entries[b->count];
        e->bytes=0;e->base=offsets[lane][n];
        e->raw=b->getprop(b->adt_ctx,"/arm-io/atc-phy1",names[kind][lane][n],&e->bytes);
        if(!e->raw || !e->bytes)return ENOENT;
        int ret=sf_atc_tunable_validate(e->raw,e->bytes,0x1000);if(ret)return ret;
        ++b->count;
    }
    return 0;
}
static int validate(void *ctx,const struct sf_atc_program *p)
{
    struct sf_atc_cal_bundle *b=ctx;
    if(!b)return EINVAL;
    b->checked=false;b->count=0;
    if(!p || !b->getprop || !b->io || !b->io->read || !b->io->write)return EINVAL;
    unsigned first=p->reversed?1:0;int ret;
    // USB3 uses USB tuning on one physical PMA, DP tuning on the other;
    // DP/USB4 follow the cable's logical-lane ordering on both PMAs.
    if(p->usb3_lanes) {
        if(p->usb3_lanes!=(1u<<first) || p->dp_lanes!=(1u<<(1-first)) || p->usb4_lanes)return EINVAL;
        if((ret=group(b,0,first)) || (ret=group(b,1,1-first)))return ret;
    } else if(p->usb4_lanes) {
        if(p->usb4_lanes!=3 || p->dp_lanes)return EINVAL;
        if((ret=group(b,2,first)) || (ret=group(b,2,1-first)))return ret;
    } else if(p->dp_lanes==3) {
        if((ret=group(b,1,first)) || (ret=group(b,1,1-first)))return ret;
    } else return EINVAL;
    b->program=*p;b->checked=true;return 0;
}
static uint32_t read_reg(void *ctx,uint32_t off)
{ struct sf_atc_cal_bundle *b=ctx;return b->io->read(b->io->ctx,SF_ATC_CORE,off); }
static void write_reg(void *ctx,uint32_t off,uint32_t value)
{ struct sf_atc_cal_bundle *b=ctx;b->io->write(b->io->ctx,SF_ATC_CORE,off,value); }
static int apply(void *ctx,const struct sf_atc_program *p)
{
    struct sf_atc_cal_bundle *b=ctx;
    if(!b || !p || !b->checked || !same(p,&b->program))return EINVAL;
    // Recheck the complete borrowed bundle before the first write too.
    for(unsigned i=0;i<b->count;++i) {
        int ret=sf_atc_tunable_validate(b->entries[i].raw,b->entries[i].bytes,0x1000);
        if(ret){b->checked=false;return ret;}
    }
    for(unsigned i=0;i<b->count;++i) {
        const struct sf_atc_cal_entry *e=&b->entries[i];
        int ret=sf_atc_tunable_apply(e->raw,e->bytes,0x1000,e->base,b,read_reg,write_reg);
        if(ret){b->checked=false;return ret;}
    }
    return 0;
}
struct sf_atc_calibration sf_atc_calibration_callbacks(struct sf_atc_cal_bundle *b)
{ return (struct sf_atc_calibration){b,validate,apply}; }
