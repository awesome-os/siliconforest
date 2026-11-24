/* SPDX-License-Identifier: MIT */
/* Parked T8103 PIPE baseline: m1n1 src/usb.c at121fcbf35d45 (MIT,
 * Copyright The Asahi Linux Contributors), combined with Linux atc.c
 * reset_assert's held-reset/clamp state. T8103 parked-state initialization; native caller supplies actual ACIO-off
 * and controller-drain proof before starting a client.
 * Unlike the bootloader, this never releases an uninitialized DWC3 core.
 */
#include "sf_atc_pipe.h"
#include <errno.h>
#include <stdint.h>
/* acio_off is the native broker's physical power-state proof, established
 * before PHY clocks rise and held by its exclusive uncommitted lease.
 * Caller must already have joined the old DWC3 producers and parked PIPE.
 */
int sf_atc_pipe_bootstrap_dummy(const struct sf_atc_io *io,
                               const struct sf_atc_pipe_state *state,int acio_off)
{
    if(!io || !io->read || !io->write || !state)return EINVAL;
    if(!acio_off || state->backend!=SF_ATC_DUMMY || state->dummy_pending)return EBUSY;
    uint32_t mux=io->read(io->ctx,SF_ATC_PIPE,0xc);
    uint32_t ack=io->read(io->ctx,SF_ATC_PIPE,0x14);
    uint32_t aon=io->read(io->ctx,SF_ATC_PIPE,0x1c);
    uint32_t override=io->read(io->ctx,SF_ATC_PIPE,0x20);
    if(mux==UINT32_MAX || ack==UINT32_MAX || aon==UINT32_MAX || override==UINT32_MAX)return EIO;
    /* Hardware proof of reset asserted, clamp active, completed park. */
    if((mux&0x3f)!=0x22 || (ack&1) || (aon&0x11)!=0x10)return EBUSY;
    if((override&0x100f)!=0x1002)return EBUSY;
    /* m1n1's 0x9332 baseline enables the documented dummy PHY (bit15).
     * The native parked state already has the reset/power fields. Enable
     * only that missing clock backend, preserving every other PIPE bit. */
    if(override&(1u<<15))return 0;
    io->write(io->ctx,SF_ATC_PIPE,0x20,override|(1u<<15));
    if((io->read(io->ctx,SF_ATC_PIPE,0x20)&0x900f)!=0x9002 ||
       (io->read(io->ctx,SF_ATC_PIPE,0x1c)&0x11)!=0x10)return EIO;
    return 0;
}
int sf_atc_pipe_bootstrap_aon(const struct sf_atc_io *io,
                             const struct sf_atc_pipe_state *state,int acio_off)
{
    if(!io || !io->read || !io->write || !state)return EINVAL;
    if(!acio_off || state->backend!=SF_ATC_DUMMY || state->dummy_pending)return EBUSY;
    uint32_t mux=io->read(io->ctx,SF_ATC_PIPE,0xc);
    uint32_t ack=io->read(io->ctx,SF_ATC_PIPE,0x14);
    uint32_t aon=io->read(io->ctx,SF_ATC_PIPE,0x1c);
    uint32_t override=io->read(io->ctx,SF_ATC_PIPE,0x20);
    if(mux==UINT32_MAX || ack==UINT32_MAX || aon==UINT32_MAX || override==UINT32_MAX)return EIO;
    if((mux&0x3f)!=0x22 || (ack&1) || (override&0x900f)!=0x9002)return EBUSY;
    /* m1n1 writes the WHOLE AON word1. Linux reset_assert then clears
     * RESET_N(bit0) and sets CLAMP(bit4), giving word0x10. Darwin inherits
     * word0x30. Test only this exact cold baseline difference while both
     * controller and ACIO are stopped; do not infer bit5's function.
     * Reject any other unknown bits instead of destroying their state. */
    if(aon==0x10)return 0;
    if(aon!=0x30)return EBUSY;
    io->write(io->ctx,SF_ATC_PIPE,0x1c,0x10);
    if(io->read(io->ctx,SF_ATC_PIPE,0x1c)!=0x10 ||
       (io->read(io->ctx,SF_ATC_PIPE,0xc)&0x3f)!=0x22 ||
       (io->read(io->ctx,SF_ATC_PIPE,0x14)&1) ||
       (io->read(io->ctx,SF_ATC_PIPE,0x20)&0x900f)!=0x9002)return EIO;
    return 0;
}
int sf_atc_usb2_bootstrap_t8103(const struct sf_atc_io *io,
                               const struct sf_atc_pipe_state *state,int acio_off)
{
    if(!io || !io->read || !io->write || !state)return EINVAL;
    if(!acio_off || state->backend!=SF_ATC_DUMMY || state->dummy_pending)return EBUSY;
    uint32_t mux=io->read(io->ctx,SF_ATC_PIPE,0xc);
    uint32_t ack=io->read(io->ctx,SF_ATC_PIPE,0x14);
    uint32_t aon=io->read(io->ctx,SF_ATC_PIPE,0x1c);
    uint32_t override=io->read(io->ctx,SF_ATC_PIPE,0x20);
    uint32_t run=io->read(io->ctx,SF_ATC_USB2,0);
    uint32_t ctl=io->read(io->ctx,SF_ATC_USB2,4);
    uint32_t tune=io->read(io->ctx,SF_ATC_USB2,0x1c);
    if(mux==UINT32_MAX || ack==UINT32_MAX || aon==UINT32_MAX ||
       override==UINT32_MAX || run==UINT32_MAX || ctl==UINT32_MAX || tune==UINT32_MAX)return EIO;
    // Native caller freshly proved physical ACIO-off and owns the uncommitted
    // broker lease. Independently require completed dummy/reset/clamp and
    // USB2 isolation, SIDDQ, both resets, APB reset and both clock gates.
    if((mux&0x3f)!=0x22 || (ack&1) || aon!=0x10 ||
       (override&0x900f)!=0x9002 || (run&7)!=4 || (ctl&0xf)!=0xb ||
       (tune&0x60000000)!=0x60000000)return EBUSY;
    const uint32_t bit=1u<<11;
    if((tune&~bit)!=0x608c0013)return EBUSY;
    if(tune&bit)return 0;
    // Published m1n1 whole-word baseline, narrowed to its one measured missing
    // bit. Keep clock gates asserted until the normal mode-start sequence.
    io->write(io->ctx,SF_ATC_USB2,0x1c,tune|bit);
    return io->read(io->ctx,SF_ATC_USB2,0x1c)==(tune|bit)?0:EIO;
}
