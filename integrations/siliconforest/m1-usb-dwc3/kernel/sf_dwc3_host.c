// SPDX-License-Identifier: GPL-2.0
/*
 * T8103 DWC_usb31 core host initialization subset.
 * Linux drivers/usb/dwc3/core.c + dwc3-apple.c (linux-aurora 6a08a08e).
 * Copyright (C) 2010-2011 Texas Instruments Incorporated - https://www.ti.com
 * Authors: Felipe Balbi <balbi@ti.com>,
 *          Sebastian Andrzej Siewior <bigeasy@linutronix.de>
 * Copyright (C) The Asahi Linux Contributors
 * Apple glue based on dwc3-qcom.c:
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 * and dwc3-of-simple.c: Copyright (c) 2015 Texas Instruments Incorporated.
 */
#include "sf_dwc3_host.h"
#include <errno.h>
static void mask(const struct sf_dwc3_io *io, uint32_t reg, uint32_t clear, uint32_t set)
{ io->write(io->ctx, reg, (io->read(io->ctx, reg) & ~clear) | set); }
int sf_dwc3_host_init(const struct sf_dwc3_io *io)
{
    uint32_t id=io->read(io->ctx,0xc120), version=io->read(io->ctx,0xc1a0);
    if ((id & 0xffff0000u) != 0x33310000u || !version || version == UINT32_MAX) return ENODEV;
    uint32_t mode=io->read(io->ctx,0xc140)&3;
    if (mode != 1 && mode != 2) return ENODEV;
    // dwc3_ss_phy_setup / dwc3_hs_phy_setup, no DT quirks on T8103.
    mask(io,0xc2c0,(1u<<27)|(1u<<17),0);
    mask(io,0xc200,1u<<6,1u<<8);
    // dwc3_core_soft_reset. Never change the host block until the PHY is up.
    // Host-only mode is reset by xHCI. Other roles reset the device half now.
    if (((io->read(io->ctx,0xc110)>>12)&3) != 1) {
        mask(io,0xc704,1u<<31,1u<<30);
        unsigned n;
        for (n=0;n<10;++n) {
            if (!(io->read(io->ctx,0xc704)&(1u<<30))) break;
            io->delay_us(io->ctx,20000);
        }
        if (n==10) return ETIMEDOUT;
        if (version <= 0x3138302a) io->delay_us(io->ctx,50000); // <=1.80a
    }
    // dwc3_core_setup_global_control, retaining firmware clock timing fields.
    uint32_t gctl=io->read(io->ctx,0xc110)&~((3u<<4)|(1u<<3));
    unsigned power=(io->read(io->ctx,0xc144)>>24)&3;
    if (power==1) gctl &= ~1u;
    else if (power==2) gctl |= 2;
    // STAR#4846132, DWC_usb31 2.00a CSR/RAM clock-gating race.
    if (power && version==0x3230302a) gctl |= 1;
    io->write(io->ctx,0xc110,gctl);
    mask(io,0xc11c,0,(1u<<24)|(1u<<31)); // L1 exit/decouple as in core.c
    // Apple CIO settings, after core initialization and before role/PHY route.
    io->write(io->ctx,0xcd38,0x0f800f80);
    io->write(io->ctx,0xcd3c,0x0fc00fc0);
    mask(io,0xcd40,0xffffff,0x140a10);
    // No peripheral producer is active in this host-only service.
    io->write(io->ctx,0xc708,0);
    io->write(io->ctx,0xc408,1u<<31); // zero-length, masked gadget event buffer
    mask(io,0xc110,3u<<12,1u<<12);
    mask(io,0xc200,0,1u<<6);
    mask(io,0xc2c0,0,1u<<17);
    return 0;
}
