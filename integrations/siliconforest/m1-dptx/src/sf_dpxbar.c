// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* T8103 crossbar register adapter.
 * Copyright (C) Asahi Linux Contributors
 * Author: Sven Peter <sven@svenpeter.dev>
 */
#include "sf_dpxbar.h"
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
#define apple_dpxbar sf_dpxbar
#define BIT(n) (1u<<(n))
#define GENMASK(h,l) ((UINT32_MAX>>(31-(h))) & (UINT32_MAX<<(l)))
#define FIELD_PREP(mask,v) (((u32)(v)<<__builtin_ctz(mask)) & (mask))
#define MUX_IDLE_DISCONNECT SF_DPXBAR_DISCONNECT
enum { MUX_DPPHY, MUX_DPIN0, MUX_DPIN1, MUX_MAX };
static const u32 apple_dpxbar_atc_bits[MUX_MAX]={1u<<8,1u,1u<<4};
static inline void dpxbar_mask32(struct sf_dpxbar *dpxbar,u32 reg,u32 mask,u32 set)
{ dpxbar->io.write(dpxbar->io.ctx,reg,(dpxbar->io.read(dpxbar->io.ctx,reg)&~mask)|set); }
static inline void dpxbar_set32(struct sf_dpxbar *dpxbar,u32 reg,u32 set)
{ dpxbar_mask32(dpxbar,reg,0,set); }
static inline void dpxbar_clear32(struct sf_dpxbar *dpxbar,u32 reg,u32 clear)
{ dpxbar_mask32(dpxbar,reg,clear,0); }
// Every call below requires the native owner's shared ATC/crossbar lock.
#define spin_lock_irqsave(lock,flags) do { (void)sizeof(*(lock));(void)sizeof(flags); } while(0)
#define spin_unlock_irqrestore(lock,flags) spin_lock_irqsave(lock,flags)
#define lockdep_assert_held(lock) ((void)sizeof(*(lock)))
#define readl(address) dpxbar->io.read(dpxbar->io.ctx,(uint32_t)(address))
#define udelay(us) dpxbar->io.delay_us(dpxbar->io.ctx,(us))
#include "dpxbar-t8103.inc"
static bool ready(struct sf_dpxbar *x,unsigned index)
{ return x && x->initialized && index<MUX_MAX; }
static bool input_ok(int input)
{ return input==SF_DPXBAR_DISCONNECT || (input>=0 && input<2); }
int sf_dpxbar_init(struct sf_dpxbar *x,const struct sf_dpxbar_io *io)
{
    if(!x || !io || !io->read || !io->write || !io->delay_us)return -EINVAL;
    if(x->initialized)return -EALREADY;
    x->io=*io;x->regs=0;x->lock=0;
    // Upstream T8103 probe's tunable is zero; no device calibration payload.
    (void)x->io.read(x->io.ctx,UNK_TUNABLE);
    x->io.write(x->io.ctx,UNK_TUNABLE,0);
    (void)x->io.read(x->io.ctx,UNK_TUNABLE);
    for(unsigned i=0;i<MUX_MAX;++i)x->selected_dispext[i]=-1;
    x->initialized=true;return 0;
}
int sf_dpxbar_select(struct sf_dpxbar *x,enum sf_dpxbar_output output,int input)
{
    if(!ready(x,(unsigned)output) || !input_ok(input))return -EINVAL;
    return apple_dpxbar_set(x,output,input);
}
int sf_dpxbar_preselect(struct sf_dpxbar *x,enum sf_dpxbar_output output,int input)
{
    if(!ready(x,(unsigned)output) || !input_ok(input))return -EINVAL;
    return apple_dpxbar_preselect(x,output,input);
}
int sf_dpxbar_link(struct sf_dpxbar *x,enum sf_dpxbar_output output,bool up)
{
    if(!ready(x,(unsigned)output))return -EINVAL;
    return up?apple_dpxbar_t8103_link_up(x,output):apple_dpxbar_t8103_link_down(x,output);
}
