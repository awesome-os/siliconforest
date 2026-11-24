// SPDX-License-Identifier: GPL-2.0
/* T8103 cold-init/link sequence adapted from drivers/pci/controller/pcie-apple.c,
 * linux-aurora 6a08a08e57558083f552733a5276d71b1a4d8ff6.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#include "sf_pciec_port.h"
#include <errno.h>
#include <stddef.h>
#define PORT SF_PCIEC_PORT
static int valid(const struct sf_pciec_io *io)
{return io && io->read && io->write && io->tune && io->delay_us;}
static int rmw(const struct sf_pciec_io *io,uint32_t off,uint32_t clear,uint32_t set)
{
    uint32_t value;int ret=io->read(io->ctx,PORT,off,&value);
    return ret?ret:io->write(io->ctx,PORT,off,(value&~clear)|set);
}
static int poll(const struct sf_pciec_io *io,uint32_t off,uint32_t mask,uint32_t want,
                unsigned step,unsigned timeout)
{
    for(unsigned elapsed=0;;elapsed+=step){
        uint32_t value;int ret=io->read(io->ctx,PORT,off,&value);
        if(ret)return ret;
        if((value&mask)==want)return 0;
        if(elapsed>=timeout)return -ETIMEDOUT;
        io->delay_us(io->ctx,step);
    }
}
int sf_pciec_cold_init(const struct sf_pciec_io *io)
{
    if(!valid(io))return -EINVAL;
    static const struct {uint16_t off;uint32_t value;} reset[]={
        {0x08c,0x110},{0x100,UINT32_MAX},{0x148,UINT32_MAX},{0x210,UINT32_MAX},
        {0x080,0},{0x084,0},{0x104,UINT32_MAX},{0x124,0},{0x128,0},{0x168,0},
        {0x13c,0x10},{0x800,0x100100},{0x808,0x100045},{0x810,0x100},
        {0x814,0},{0x130,0},{0x140,0x10},{0x144,0x253770},{0x21c,0},
        {0x81c,0},{0x824,0}
    };
    int ret=io->tune(io->ctx,SF_PCIEC_DEBUG);if(ret)return ret;
    ret=io->tune(io->ctx,SF_PCIEC_FABRIC);if(ret)return ret;
    for(size_t i=0;i<sizeof(reset)/sizeof(reset[0]);++i){
        ret=io->write(io->ctx,PORT,reset[i].off,reset[i].value);if(ret)return ret;
    }
    for(unsigned i=0;i<64;++i){
        uint32_t value;ret=io->write(io->ctx,PORT,0x828+4*i,0);if(ret)return ret;
        ret=io->read(io->ctx,PORT,0x828+4*i,&value);if(ret)return ret;
        if(value)return -EIO;
    }
    ret=io->tune(io->ctx,PORT);if(ret)return ret;
    ret=rmw(io,0x814,0,1);if(ret)return ret;
    ret=rmw(io,0x800,0,1);if(ret)return ret;
    /* T8103 clears CGDIS and tunes the root before waiting for RUN. The
     * T600x cold-init ordering differs; this routine is T8103 only. */
    ret=rmw(io,0x800,0x100,0);if(ret)return ret;
    ret=io->tune(io->ctx,SF_PCIEC_OE);if(ret)return ret;
    ret=io->tune(io->ctx,SF_PCIEC_CONFIG);if(ret)return ret;
    ret=io->write(io->ctx,PORT,0x4020,3);if(ret)return ret;
    ret=io->write(io->ctx,PORT,0x100,UINT32_MAX);if(ret)return ret;
    ret=io->write(io->ctx,PORT,0x210,UINT32_MAX);if(ret)return ret;
    ret=poll(io,0x804,1,1,10,250000);if(ret)return ret;
    return poll(io,0x208,4,0,10,250000);
}
int sf_pciec_link_start(const struct sf_pciec_io *io)
{
    if(!valid(io))return -EINVAL;
    int ret=io->write(io->ctx,PORT,0x988,0);if(ret)return ret;
    ret=poll(io,0x98c,1,0,1000,250000);if(ret)return ret;
    ret=rmw(io,0x80,0,1);if(ret)return ret;
    return poll(io,0x208,1,1,1000,500000);
}
