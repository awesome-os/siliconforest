/* SPDX-License-Identifier: GPL-2.0 */
/* Unlocked T8103 NHI DART reset, adapted from apple_dart_hw_reset() and
 * apple_dart_hw_finish_reset(), linux-aurora 6a08a08e, apple-dart.c.
 * Copyright (C) 2021 The Asahi Linux Contributors
 * Based on ARM SMMU: Copyright (C) 2013, 2015 ARM Limited;
 * exynos-iommu: Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 */
#ifndef SF_CIO_DART_RESET_H
#define SF_CIO_DART_RESET_H
#include <stdint.h>
#include <sys/errno.h>
struct sf_cio_dart_reset_io {
    void *ctx;
    int (*read)(void *, uint32_t, uint32_t *);
    int (*write)(void *, uint32_t, uint32_t);
    int (*invalidate)(void *, uint32_t streams);
};
/* Positive errno convention. Caller owns the entire unlocked ACIO1 NHI
 * DART, has proved all 12 rings disabled and drained prior DMA/handlers.
 * Does not touch the separate PCIe-C DART, map memory or enable bypass. */
static inline int sf_cio_dart_reset(const struct sf_cio_dart_reset_io *io)
{
    if(!io || !io->read || !io->write || !io->invalidate)return EINVAL;
    int ret;uint32_t value;
    /* Disable every stream before clearing any root, including the reset
     * SID15 translation-enabled/no-TTBR state observed on CIO06. */
    for(unsigned sid=0;sid<16;++sid){
        uint32_t off=0x100+4*sid;
        ret=io->write(io->ctx,off,0);if(ret)return ret;
        ret=io->read(io->ctx,off,&value);if(ret)return ret;
        if(value)return EIO;
    }
    for(unsigned sid=0;sid<16;++sid)for(unsigned n=0;n<4;++n){
        uint32_t off=0x200+16*sid+4*n;
        ret=io->write(io->ctx,off,0);if(ret)return ret;
        ret=io->read(io->ctx,off,&value);if(ret)return ret;
        if(value)return EIO;
    }
    /* Linux globally enables streams, with TCR controlling isolation.
     * Unused streams stay disabled, with no physical-address bypass. */
    ret=io->write(io->ctx,0xfc,0xffff);if(ret)return ret;
    ret=io->read(io->ctx,0xfc,&value);if(ret)return ret;
    if(value!=0xffff)return EIO;
    ret=io->read(io->ctx,0x40,&value);if(ret)return ret;
    ret=io->write(io->ctx,0x40,value);if(ret)return ret;
    ret=io->read(io->ctx,0x40,&value);if(ret)return ret;
    if(value&(1u<<31))return EIO;
    return io->invalidate(io->ctx,0xffff);
}
#endif
