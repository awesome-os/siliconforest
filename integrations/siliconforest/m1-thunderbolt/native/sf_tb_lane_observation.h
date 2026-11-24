/* SPDX-License-Identifier: GPL-2.0 */
/* Bounded real lane observation using the pinned tb_regs.h layout.
 * Layout from Linux Thunderbolt (Intel Corporation / Andreas Noever).
 * No writes, retries, cached topology, authorization or polling timer.
 */
#ifndef SF_TB_LANE_OBSERVATION_H
#define SF_TB_LANE_OBSERVATION_H
#include <stdint.h>
#include <errno.h>
struct sf_tb_lane_observation {
 uint32_t state,disabled,locked;
 uint32_t phy[2],header[8];
};
typedef int (*sf_tb_lane_reader)(void *,uint32_t,uint32_t *,unsigned);
static inline int sf_tb_lane_observe(void *ctx,sf_tb_lane_reader read,
 uint32_t cap,struct sf_tb_lane_observation *out)
{
 uint32_t phy[2],header[8];
 if(!ctx || !read || !out || !cap || cap==UINT32_MAX)return -EINVAL;
 int ret=read(ctx,cap,phy,2);if(ret)return ret;
 ret=read(ctx,0,header,8);if(ret)return ret;
 struct sf_tb_lane_observation result={0};
 result.state=(phy[1]>>26)&15;result.disabled=(phy[1]>>14)&1;result.locked=(header[4]>>31)&1;
 for(unsigned i=0;i<2;++i)result.phy[i]=phy[i];
 for(unsigned i=0;i<8;++i)result.header[i]=header[i];
 *out=result;return 0;
}
#endif
