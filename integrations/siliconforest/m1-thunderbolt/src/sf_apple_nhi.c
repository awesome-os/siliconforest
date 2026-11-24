// SPDX-License-Identifier: GPL-2.0
/* T8103 NHI/DP-IN/SRAM backend subset, Linux apple.c/nhi.c (6a08a08e).
 * Copyright (c) Sven Peter <sven@kernel.org>
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018 Intel Corporation
 */
#include "sf_apple_nhi.h"
#include <errno.h>
#define IRQ_ENABLE 0xd0010
#define ENABLE (1u<<31)
#define E2E (1u<<28)
#define RAW (1u<<30)
static int valid(const struct sf_nhi_io *io,const struct sf_nhi_ring *r)
{
    if (!io || !r || !io->powered || !io->read || !io->write || !io->fence ||
        !r->nrings || r->nrings>16 || r->hop>=r->nrings || r->count<2 ||
        r->count>65535 || !r->dma || (r->dma&15) || r->dma>=(1ULL<<42) ||
        r->count*16ULL>(1ULL<<42)-r->dma || r->e2e_tx_hop>=r->nrings ||
        io->span<0xd004c+4*(2*r->nrings)) return -EINVAL;
    if (!r->tx && io->write_pdf && io->pdf_span<r->hop*0x4000ULL+4) return -EINVAL;
    return io->powered(io->ctx)?0:-ESHUTDOWN;
}
int sf_apple_nhi_ring_start(const struct sf_nhi_io *io,struct sf_nhi_ring *r)
{
    int ret=valid(io,r);if(ret)return ret;
    if (r->running) return -EBUSY;
    // Read before any write: a mismatched hardware/IRQ ring count is fatal.
    if ((io->read(io->ctx,0)&0x3ff)!=r->nrings) return -ENODEV;
    uint32_t desc=(r->tx?0x10000:0x80000)+r->hop*0x4000;
    uint32_t options=desc+0x10;
    uint32_t flags=ENABLE|(r->frame_mode?0:RAW);
    if(r->e2e)flags|=E2E|(r->tx?0:r->e2e_tx_hop<<12);
    io->write(io->ctx,desc,(uint32_t)r->dma);
    io->write(io->ctx,desc+4,(uint32_t)(r->dma>>32));
    io->write(io->ctx,desc+12,r->count|(!r->tx&&!r->frame_mode?256u<<16:0));
    if(r->tx)io->write(io->ctx,options+4,r->hop==0?2:r->hop<=5?40:5);
    else {
        uint32_t masks=(uint32_t)r->sof_mask<<16|r->eof_mask;
        io->write(io->ctx,options+4,masks);
        if(io->write_pdf)io->write_pdf(io->ctx,r->hop*0x4000,masks);
    }
    io->fence(io->ctx); // descriptor memory/address/config precede VALID
    r->running=true; // a failed readback must retain mappings, not free them
    io->write(io->ctx,options,flags); // VALID and E2E sampled together
    if(io->read(io->ctx,options)!=flags)return -EIO;
    if(r->interrupts) {
        unsigned idx=r->hop+(r->tx?0:r->nrings);
        if(r->interval_ns) {
            uint32_t ns=r->interval_ns, max=65535u*256;
            if(ns>max)ns=max;
            io->write(io->ctx,0xd004c+4*idx,(ns+255)/256);
        }
        uint32_t enable=io->read(io->ctx,IRQ_ENABLE);
        if(enable==UINT32_MAX)return -EIO;
        io->write(io->ctx,IRQ_ENABLE,enable|(1u<<idx));
    }
    return 0;
}
int sf_apple_nhi_ring_stop(const struct sf_nhi_io *io,struct sf_nhi_ring *r)
{
    int ret=valid(io,r);if(ret)return ret;
    if(!r->running)return 0;
    uint32_t desc=(r->tx?0x10000:0x80000)+r->hop*0x4000;
    if(r->interrupts) {
        uint32_t enable=io->read(io->ctx,IRQ_ENABLE);
        if(enable==UINT32_MAX)return -EIO;
        io->write(io->ctx,IRQ_ENABLE,enable&~(1u<<(r->hop+(r->tx?0:r->nrings))));
    }
    io->write(io->ctx,desc+0x10,0);
    io->fence(io->ctx);
    if(io->read(io->ctx,desc+0x10)&ENABLE)return -EIO;
    io->write(io->ctx,desc,0);io->write(io->ctx,desc+4,0);
    io->write(io->ctx,desc+8,0);io->write(io->ctx,desc+12,0);
    io->fence(io->ctx);
    r->running=false;
    return 0;
}
static void put32(uint8_t *out,uint32_t v)
{ for(unsigned n=0;n<4;++n)out[n]=(uint8_t)(v>>(8*n)); }
int sf_apple_nhi_descriptor(uint8_t out[16],uint64_t dma,unsigned bytes,unsigned sof,unsigned eof,bool irq)
{
    if(!out || !dma || dma>=(1ULL<<42) || !bytes || bytes>4096 ||
       bytes>(1ULL<<42)-dma || sof>15 || eof>15)return -EINVAL;
    put32(out,(uint32_t)dma);put32(out+4,(uint32_t)(dma>>32));
    put32(out+8,(bytes&0xfff)|(eof<<12)|(sof<<16)|((4u|(irq?8:0))<<20));
    put32(out+12,0);
    return 0;
}
int sf_apple_cio_sram_offset(uint64_t iova,uint64_t bytes,uint64_t phys,uint64_t span,uint64_t *out)
{
    if(!out || !bytes || !span || iova<0x10000000ULL || span-1>UINT64_MAX-phys)return -EINVAL;
    uint64_t off=iova-0x10000000ULL;
    if(off>=span || bytes>span-off)return -EFAULT;
    *out=off;return 0;
}
int sf_apple_dpin_t8103_active(const struct sf_dpin_io *io,bool active)
{
    if(!io || !io->read || !io->write || !io->wait)return -EINVAL;
    uint32_t want=active?0:1,st,ctrl,ack;
    int ret;
    if(active)for(;;) {
        st=io->read(io->ctx,0);
        if(st==UINT32_MAX)return -EIO;
        if(st&4)break;
        if((ret=io->wait(io->ctx)))return ret;
    }
    ctrl=io->read(io->ctx,0xc);ack=io->read(io->ctx,0x10);
    if(ctrl==UINT32_MAX || ack==UINT32_MAX)return -EIO;
    if(!!(ctrl&1)==!active && (ack&1)==want)return 0;
    io->write(io->ctx,0xc,active?ctrl&~1u:ctrl|1u);
    for(;;) {
        ack=io->read(io->ctx,0x10);
        if(ack==UINT32_MAX)return -EIO;
        if((ack&1)==want)return 0;
        if((ret=io->wait(io->ctx)))return ret;
    }
}
