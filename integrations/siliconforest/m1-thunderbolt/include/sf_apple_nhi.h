/* SPDX-License-Identifier: GPL-2.0 */
/* Apple NHI register subset from drivers/thunderbolt/apple.c and nhi.c.
 * Copyright (c) Sven Peter <sven@kernel.org>
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018 Intel Corporation.
 */
#ifndef SF_APPLE_NHI_H
#define SF_APPLE_NHI_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_nhi_io {
    void *ctx;
    /* Must return true only while ACIO/RTKit/children are live. Caller holds
     * the session/ring lock across admission and all following MMIO. */
    bool (*powered)(void *);
    uint32_t (*read)(void *,uint32_t);
    void (*write)(void *,uint32_t,uint32_t);
    void (*fence)(void *);
    size_t span, pdf_span;
    void (*write_pdf)(void *,uint32_t,uint32_t);
};
struct sf_nhi_ring {
    uint64_t dma; // descriptor IOVA in this ACIO's DART, never CPU PA
    unsigned hop, nrings, count;
    uint16_t sof_mask, eof_mask;
    uint32_t interval_ns;
    unsigned e2e_tx_hop;
    bool tx, frame_mode, e2e, interrupts, running;
};
/* Caller owns descriptor/payload mappings before start, then drains all CPU
 * operations/callbacks before stop/unmap. Stop programs the hardware but does
 * not itself join workers or free DMA. No control-channel/CM is supplied here. */
int sf_apple_nhi_ring_start(const struct sf_nhi_io *,struct sf_nhi_ring *);
int sf_apple_nhi_ring_stop(const struct sf_nhi_io *,struct sf_nhi_ring *);
int sf_apple_nhi_descriptor(uint8_t out[16],uint64_t dma,unsigned bytes,unsigned sof,unsigned eof,bool irq);
/* Translate RTKit's SRAM IOVA with bounds/overflow checks; no arbitrary
 * firmware DMA mapping. Returns an offset within already-owned ACIO SRAM. */
int sf_apple_cio_sram_offset(uint64_t iova,uint64_t bytes,uint64_t sram_phys,
                            uint64_t sram_bytes,uint64_t *offset);
/* M1 DP-IN handshake: one 500ms wait budget covers HPD and INACTIVE_ACK. */
struct sf_dpin_io {
    void *ctx;
    uint32_t (*read)(void *,uint32_t);
    void (*write)(void *,uint32_t,uint32_t);
    int (*wait)(void *); // bounded by one deadline created before the call
};
int sf_apple_dpin_t8103_active(const struct sf_dpin_io *,bool active);
#ifdef __cplusplus
}
#endif
#endif
