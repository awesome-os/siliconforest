// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* T8103 PIPE routing/parking from Omacom Linux 6a08a08e atc.c.
 * Copyright (C) The Asahi Linux Contributors
 * Author: Sven Peter <sven@kernel.org>
 * Failed completion keeps explicit ownership; no teardown is inferred here.
 */
#include "sf_atc_pipe.h"
#include <errno.h>
static bool valid(const struct sf_atc_io *io,const struct sf_atc_pipe_state *s)
{ return io && io->read && io->write && io->delay_us && s && (unsigned)s->backend<=SF_ATC_USB4_BACKEND; }
static void mask(const struct sf_atc_io *io,unsigned off,unsigned clear,unsigned set)
{ io->write(io->ctx,SF_ATC_PIPE,off,(io->read(io->ctx,SF_ATC_PIPE,off)&~clear)|set); }
static int ack(const struct sf_atc_io *io,unsigned wanted)
{
    for(unsigned elapsed=0;elapsed<1000;elapsed+=10) {
        if((io->read(io->ctx,SF_ATC_PIPE,0x14)&1)==wanted)return 0;
        io->delay_us(io->ctx,10);
    }
    return ETIMEDOUT;
}
static int unlock(const struct sf_atc_io *io)
{ mask(io,0x10,1,0);return ack(io,0); }
static int lock(const struct sf_atc_io *io)
{
    mask(io,0x10,0,1);int ret=ack(io,1);
    if(ret)mask(io,0x10,1,0);
    return ret;
}
static int check(const struct sf_atc_io *io)
{ return io->read(io->ctx,SF_ATC_PIPE,0x14)&1?unlock(io):0; }
static void mux(const struct sf_atc_io *io,enum sf_atc_pipe backend)
{
    unsigned data=backend==SF_ATC_USB3_BACKEND?0:backend==SF_ATC_USB4_BACKEND?1:2;
    unsigned clock=backend==SF_ATC_USB3_BACKEND?1:backend==SF_ATC_USB4_BACKEND?2:4;
    mask(io,0xc,0x38,0);io->delay_us(io->ctx,10);
    mask(io,0xc,7,data);io->delay_us(io->ctx,10);
    mask(io,0xc,0x38,clock<<3);io->delay_us(io->ctx,10);
}
static void disable_detect(const struct sf_atc_io *io)
{ mask(io,4,6,0);mask(io,0,0,1);mask(io,0,0,4); }
static void finish_dummy(const struct sf_atc_io *io)
{ mask(io,0x20,0xf,2);mask(io,0x20,0,1u<<12); }
int sf_atc_pipe_route(const struct sf_atc_io *io,struct sf_atc_pipe_state *s,enum sf_atc_pipe target)
{
    if(!valid(io,s) || (target!=SF_ATC_USB3_BACKEND && target!=SF_ATC_USB4_BACKEND))return EINVAL;
    if(s->dummy_pending)return EBUSY;
    if(s->backend==target)return check(io);
    if(s->backend!=SF_ATC_DUMMY)return EBUSY;
    int ret=check(io);if(ret)return ret;
    if(target==SF_ATC_USB4_BACKEND){disable_detect(io);(void)lock(io);}
    // T8103 USB3 uses host_bist=false, including in host role.
    mux(io,target);s->backend=target;
    mask(io,0,1,0);mask(io,0,4,0);
    // Unlike upstream's discarded USB4 unlock failure, retain an error so
    // the owner cannot publish a client before the handoff completes.
    return target==SF_ATC_USB4_BACKEND?unlock(io):0;
}
int sf_atc_pipe_park(const struct sf_atc_io *io,struct sf_atc_pipe_state *s,bool stopped)
{
    if(!valid(io,s))return EINVAL;
    if(s->dummy_pending) {
        int ret=unlock(io);if(ret)return ret;
        finish_dummy(io);s->dummy_pending=false;return 0;
    }
    if(s->backend==SF_ATC_DUMMY)return 0;
    if(!stopped && s->backend!=SF_ATC_USB3_BACKEND)return EBUSY;
    int ret=check(io);if(ret)return ret;
    unsigned overrides=io->read(io->ctx,SF_ATC_PIPE,0);
    unsigned values=io->read(io->ctx,SF_ATC_PIPE,4);
    disable_detect(io);
    for(unsigned attempt=0;;++attempt) {
        ret=lock(io);
        if(!ret || stopped || attempt==9)break;
        io->delay_us(io->ctx,5000);
    }
    if(ret && !stopped) {
        mask(io,4,6,values&6);mask(io,0,5,overrides&5);return ret;
    }
    mux(io,SF_ATC_DUMMY);s->backend=SF_ATC_DUMMY;s->dummy_pending=true;
    ret=unlock(io);if(ret)return ret;
    finish_dummy(io);s->dummy_pending=false;return 0;
}
