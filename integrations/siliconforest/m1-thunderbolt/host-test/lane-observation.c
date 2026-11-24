/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sf_tb_lane_observation.h"
struct hardware {unsigned calls,fail;uint32_t phy,adapter;};
static int read_lane(void *ctx,uint32_t offset,uint32_t *out,unsigned count)
{
 struct hardware *h=ctx;unsigned call=++h->calls;
 assert(call<=2);
 if(call==1){assert(offset==0x20 && count==2);}
 else{assert(offset==0 && count==8);}
 if(call==h->fail)return -EIO;
 if(call==1){out[0]=0xdeadbeef;out[1]=h->phy;}
 else{for(unsigned i=0;i<8;++i)out[i]=0xa5a50000|i;out[4]=h->adapter;}
 return 0;
}
int main(void)
{
 struct sf_tb_lane_observation out={.state=99,.disabled=98,.locked=97};
 struct sf_tb_lane_observation saved=out;
 struct hardware h={0};
 assert(sf_tb_lane_observe(NULL,read_lane,0x20,&out)==-EINVAL);
 assert(sf_tb_lane_observe(&h,NULL,0x20,&out)==-EINVAL);
 assert(sf_tb_lane_observe(&h,read_lane,0x20,NULL)==-EINVAL);
 assert(sf_tb_lane_observe(&h,read_lane,0,&out)==-EINVAL);
 assert(sf_tb_lane_observe(&h,read_lane,UINT32_MAX,&out)==-EINVAL);
 assert(h.calls==0 && out.state==99 && out.disabled==98 && out.locked==97);
 for(unsigned fail=1;fail<=2;++fail){
  h=(struct hardware){.fail=fail};
  assert(sf_tb_lane_observe(&h,read_lane,0x20,&out)==-EIO);
  assert(h.calls==fail && !memcmp(&out,&saved,sizeof(out)));
 }
 for(unsigned state=0;state<16;++state)for(unsigned disabled=0;disabled<2;++disabled)
  for(unsigned locked=0;locked<2;++locked){
   /* Unrelated bits set to catch accidental field overlap. */
   h=(struct hardware){.phy=(state<<26)|(disabled<<14)|0xc3ffbfff,
                       .adapter=(locked<<31)|0x7fffffff};
   assert(sf_tb_lane_observe(&h,read_lane,0x20,&out)==0);
   assert(h.calls==2 && out.state==state && out.disabled==disabled && out.locked==locked);
   assert(out.phy[0]==0xdeadbeef && out.phy[1]==h.phy && out.header[4]==h.adapter);
   for(unsigned n=0;n<8;++n)if(n!=4)assert(out.header[n]==(0xa5a50000|n));
  }
 puts("actual native lane observer: bounds, both read failures, 64 state/LD/LCK combinations PASS");
}
