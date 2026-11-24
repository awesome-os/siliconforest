// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/* Copyright (C) The Asahi Linux Contributors
 * Author of the ported register sequence: Sven Peter <sven@kernel.org>
 * T8103 specialization and native callback wrapper, see the upstream ATC DP include.
 */
#include "sf_atc_dp.h"
#include <stdint.h>
#include <errno.h>
#include <string.h>
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
#define BIT(n) (1u << (n))
#define GENMASK(h,l) ((UINT32_MAX >> (31-(h))) & (UINT32_MAX << (l)))
#define FIELD_PREP(mask,value) (((uint32_t)(value) << __builtin_ctz((uint32_t)(mask))) & (mask))
#define FIELD_GET(mask,value) (((uint32_t)(value) & (mask)) >> __builtin_ctz((uint32_t)(mask)))
#define SF_DP_LPDPTX 0x80000000u
struct atcphy_mode_configuration { bool dp_lane[2]; };
struct apple_atcphy {
    const struct sf_atc_io *io;
    struct { uintptr_t core, lpdptx; } regs;
    struct atcphy_mode_configuration mode_cfg;
    bool mode_dirty, pipe_pending, tunnel_clock_on, tunnel_saved;
    int mode, dp_link_rate;
    uint8_t tunnel_rate;
    unsigned tunnel_users;
};
enum atcphy_lane { APPLE_ATCPHY_LANE_0, APPLE_ATCPHY_LANE_1 };
static uint32_t dp_read(struct apple_atcphy *phy, uintptr_t addr)
{
    enum sf_atc_window w=addr&SF_DP_LPDPTX?SF_ATC_LPDPTX:SF_ATC_CORE;
    return phy->io->read(phy->io->ctx,w,(uint32_t)addr&~SF_DP_LPDPTX);
}
static void dp_mask(struct apple_atcphy *phy, uintptr_t addr,uint32_t clear,uint32_t set)
{
    enum sf_atc_window w=addr&SF_DP_LPDPTX?SF_ATC_LPDPTX:SF_ATC_CORE;
    phy->io->write(phy->io->ctx,w,(uint32_t)addr&~SF_DP_LPDPTX,(dp_read(phy,addr)&~clear)|set);
}
static const struct atcphy_mode_configuration *atcphy_get_mode_config(struct apple_atcphy *phy,int mode)
{ (void)mode; return &phy->mode_cfg; }
#define readl(addr) dp_read(atcphy,(addr))
#define writel(value,addr) dp_mask(atcphy,(addr),UINT32_MAX,(value))
#define mask32(addr,mask,value) dp_mask(atcphy,(addr),(mask),(value))
#define set32(addr,value) dp_mask(atcphy,(addr),0,(value))
#define clear32(addr,value) dp_mask(atcphy,(addr),(value),0)
#define core_mask32(phy,reg,mask,value) dp_mask((phy),(reg),(mask),(value))
#define core_set32(phy,reg,value) dp_mask((phy),(reg),0,(value))
#define core_clear32(phy,reg,value) dp_mask((phy),(reg),(value),0)
#define udelay(us) atcphy->io->delay_us(atcphy->io->ctx,(us))
/* Log messages remain in the extracted source; the native caller reports the
 * returned error and its own session/mode. No error is converted to success. */
#define U32_MAX UINT32_MAX
static bool apple_atc_tunnel_is_t600x(void) { return false; } // T8103 specialization
#define dev_dbg(...) ((void)0)
#define dev_warn(...) ((void)0)
#define dev_err(...) ((void)0)
#define readl_poll_timeout(addr,reg,condition,interval,timeout) ({ \
    int _ret=-ETIMEDOUT; \
    for(unsigned _elapsed=0;_elapsed<(timeout);_elapsed+=(interval)) { \
        (reg)=readl(addr); if(condition) {_ret=0;break;} udelay(interval); \
    } _ret; })
#include "atc-t8103-dp.inc"

static int dp_context(struct sf_atc_dp *dp, struct apple_atcphy *phy)
{
    if (!dp || !dp->io || !dp->io->read || !dp->io->write || !dp->io->delay_us ||
        !dp->lane_mask || dp->lane_mask>3) return -EINVAL;
    memset(phy,0,sizeof(*phy));
    phy->io=dp->io; phy->regs.lpdptx=SF_DP_LPDPTX;
    phy->mode_cfg.dp_lane[0]=!!(dp->lane_mask&1);
    phy->mode_cfg.dp_lane[1]=!!(dp->lane_mask&2);
    phy->mode_dirty=dp->mode_dirty; phy->pipe_pending=dp->pipe_pending;
    phy->tunnel_clock_on=dp->tunnel_clock_on; phy->tunnel_saved=dp->tunnel_saved;
    switch(dp->rate) {
    case 1620: phy->dp_link_rate=ATCPHY_DP_LINK_RATE_RBR;break;
    case 2700: phy->dp_link_rate=ATCPHY_DP_LINK_RATE_HBR;break;
    case 5400: phy->dp_link_rate=ATCPHY_DP_LINK_RATE_HBR2;break;
    case 8100: phy->dp_link_rate=ATCPHY_DP_LINK_RATE_HBR3;break;
    default: phy->dp_link_rate=-1;break;
    }
    return 0;
}
int sf_atc_dp_aux_start(struct sf_atc_dp *dp)
{
    struct apple_atcphy phy;
    int ret=dp_context(dp,&phy);
    if (ret) return ret;
    if (phy.mode_dirty || phy.pipe_pending || phy.tunnel_clock_on || phy.tunnel_saved) return -EBUSY;
    ret=atcphy_enable_dp_aux(&phy);
    dp->rate=-1;
    return ret;
}
int sf_atc_dp_aux_stop(struct sf_atc_dp *dp)
{
    struct apple_atcphy phy;
    int ret=dp_context(dp,&phy);
    if (ret) return ret;
    // A tunnel owns the same clock controls; its teardown must release first.
    if (phy.tunnel_clock_on || phy.tunnel_saved) return -EBUSY;
    ret=atcphy_disable_dp_aux(&phy);
    dp->rate=-1;
    return ret;
}
int sf_atc_dp_set_rate(struct sf_atc_dp *dp,unsigned mbps)
{
    // T8103 phy_configure accepts DCP's zero-rate notification unchanged.
    // This is not clock shutdown and does not clear our actual rate/ownership.
    if (!mbps) { struct apple_atcphy phy;return dp_context(dp,&phy); }
    enum atcphy_dp_link_rate lr;
    switch(mbps) {
    case 1620:lr=ATCPHY_DP_LINK_RATE_RBR;break;
    case 2700:lr=ATCPHY_DP_LINK_RATE_HBR;break;
    case 5400:lr=ATCPHY_DP_LINK_RATE_HBR2;break;
    case 8100:lr=ATCPHY_DP_LINK_RATE_HBR3;break;
    default:return -EINVAL;
    }
    struct apple_atcphy phy;
    int ret=dp_context(dp,&phy);
    if (ret) return ret;
    ret=atcphy_dp_configure(&phy,lr);
    if (phy.dp_link_rate<0) dp->rate=-1;
    else if (!ret) dp->rate=(int)mbps;
    return ret;
}

int sf_atc_tunnel_set_rate(struct sf_atc_tunnel *t,uint8_t rate)
{
    if (!t || !t->io || !t->io->read || !t->io->write || !t->io->delay_us) return -EINVAL;
    if (rate && (!t->configured || t->mode_dirty || t->pipe_pending ||
                 (t->clock_on && !t->rate))) return -EBUSY;
    struct apple_atcphy phy;
    memset(&phy,0,sizeof(phy));
    phy.io=t->io;phy.regs.lpdptx=SF_DP_LPDPTX;
    phy.tunnel_clock_on=t->clock_on;phy.tunnel_rate=t->rate;
    phy.dp_link_rate=-1;
    int ret=rate?atc_tunnel_start_t8103(&phy,rate):atc_tunnel_stop_t8103(&phy);
    t->clock_on=phy.tunnel_clock_on;t->rate=phy.tunnel_rate;
    return ret;
}
