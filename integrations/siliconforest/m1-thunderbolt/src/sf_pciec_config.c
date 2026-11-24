// SPDX-License-Identifier: GPL-2.0
/* Native PCIe-C ECAM adaptation of pcie-apple.c and SFApcieT8103Host.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#include "sf_pciec_config.h"
#include <errno.h>
static int access_size(unsigned reg,unsigned width)
{
    return (width==1 || width==2 || width==4) && !(reg&(width-1)) &&
           reg<=4096-width;
}
int sf_pciec_config_offset(const struct sf_pciec_config_view *view,unsigned bus,
                          unsigned device,unsigned function,unsigned reg,
                          unsigned width,uint64_t *offset)
{
    if(!view || !offset || view->first_bus>view->last_bus || view->last_bus>255 ||
       device>31 || function>7 || !access_size(reg,width))return -EINVAL;
    if(!view->admitted)return -ENODEV;
    if(bus<view->first_bus || bus>view->last_bus)return -ERANGE;
    /* Only one initialized root port; downstream TB bridges have multiple
     * device numbers, unlike the existing Wi-Fi host's device-zero filter. */
    if(bus==view->first_bus && (device || function))return -ENODEV;
    uint64_t value=((uint64_t)(bus-view->first_bus)<<20) |
                   ((uint64_t)device<<15) | ((uint64_t)function<<12) | reg;
    if(value>view->bytes || width>view->bytes-value)return -ERANGE;
    *offset=value;return 0;
}
int sf_pciec_config_write_filter(unsigned reg,unsigned width,uint32_t value,
                                bool dma_admitted,uint32_t *filtered)
{
    if(!filtered || !access_size(reg,width))return -EINVAL;
    if(!dma_admitted && reg==4)value&=~(1u<<2);
    *filtered=value;return 0;
}
