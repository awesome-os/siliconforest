/* SPDX-License-Identifier: GPL-2.0-only */
/* T8103 ACIO1 default pinctrl, from linux-aurora 6a08a08e
 * arch/arm64/boot/dts/apple/t8103.dtsi and
 * drivers/pinctrl/pinctrl-apple-gpio.c: apple_gpio_pinmux_set.
 * Copyright (C) The Asahi Linux Contributors
 * Copyright (C) 2020 Corellium LLC
 * Based on pinctrl-pistachio.c, Copyright (C) 2014 Imagination Technologies
 * Ltd., Copyright (C) 2014 Google, Inc.
 */
#ifndef SF_ACIO_PINMUX_H
#define SF_ACIO_PINMUX_H
#include <stdint.h>
#include <sys/errno.h>
struct sf_acio_pinmux_io {
    void *ctx;
    uint32_t (*read)(void *);
    void (*write)(void *,uint32_t);
};
/* Caller validates AOP GPIO41's actual resource and owns the exclusive
 * preparing TBT/USB4 PHY lease, with physical ACIO1 proved off. No IRQ,
 * pull, mode, data or drive setting is changed. Do not use for GPIO39. */
static inline int sf_acio1_pinmux(const struct sf_acio_pinmux_io *io,
                                uint32_t *before,uint32_t *after)
{
    if(!io || !io->read || !io->write || !before || !after)return EINVAL;
    *before=*after=io->read(io->ctx);
    if(*before==UINT32_MAX)return ENXIO;
    const uint32_t mask=0x260,value=0x220;
    uint32_t desired=(*before&~mask)|value;
    if(desired==*before)return 0;
    if(*before&(1u<<21))return EBUSY;
    io->write(io->ctx,desired);
    *after=io->read(io->ctx);
    /* DATA can change asynchronously; all other fields must be preserved. */
    return *after==UINT32_MAX || ((*after^desired)&~1u)?EIO:0;
}
#endif
