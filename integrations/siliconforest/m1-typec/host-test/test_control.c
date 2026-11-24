/* SPDX-License-Identifier: MIT */
#include "sf_cd321x_control.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct model {unsigned calls,fail;};
static int read_reg(void *ctx,uint8_t reg,void *out,size_t size)
{
 struct model *m=ctx;unsigned n=m->calls++;
 const uint8_t regs[]={0x14,0x16,0x20,0x1a,0x5f};const size_t sizes[]={8,8,1,4,4};
 assert(n<5 && reg==regs[n] && size==sizes[n]);
 if(m->calls==m->fail)return EIO;
 memset(out,reg,size);return 0;
}
int main(void)
{
 struct sf_cd321x_control out,saved;memset(&out,0xa5,sizeof(out));saved=out;
 struct model m={0};
 assert(sf_cd321x_control_observe(NULL,read_reg,&out)==EINVAL);
 assert(sf_cd321x_control_observe(&m,NULL,&out)==EINVAL);
 assert(sf_cd321x_control_observe(&m,read_reg,NULL)==EINVAL);
 assert(m.calls==0 && !memcmp(&out,&saved,sizeof(out)));
 for(unsigned fail=1;fail<=5;++fail){m=(struct model){.fail=fail};
  assert(sf_cd321x_control_observe(&m,read_reg,&out)==EIO && m.calls==fail);
  assert(!memcmp(&out,&saved,sizeof(out)));}
 m=(struct model){0};assert(!sf_cd321x_control_observe(&m,read_reg,&out) && m.calls==5);
 for(unsigned i=0;i<8;++i)assert(out.event1[i]==0x14 && out.mask1[i]==0x16);
 assert(out.power==0x20);
 for(unsigned i=0;i<4;++i)assert(out.status[i]==0x1a && out.data[i]==0x5f);
 puts("actual CD321x readonly control observer: exact five reads, every I/O failure leaves output unchanged, invalid inputs PASS");
}
