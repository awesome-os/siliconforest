/* SPDX-License-Identifier: MIT */
#include "sf_tb_dock_policy.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
 for(uint64_t route=0;route<65536;++route)
  assert(sf_tb_ts3_direct(0x3d,0x11,route)==(route==1 || route==2));
 for(unsigned id=0;id<65536;++id){
  assert(sf_tb_ts3_direct(id,0x11,1)==(id==0x3d));
  assert(sf_tb_ts3_direct(0x3d,id,2)==(id==0x11));
 }
 assert(!sf_tb_ts3_direct(0x3d,0x11,UINT64_MAX));
 assert(!sf_tb_ts3_direct(0x3d,0x11,UINT64_C(1)<<32|1));
 puts("TS3 metadata allowlist: all16-bit model/route values and high-route rejection PASS; no physical/authorization evidence");
}
