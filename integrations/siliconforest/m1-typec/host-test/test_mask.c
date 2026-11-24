/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_cd321x_mask.h"
#include <assert.h>
#include <stdio.h>
struct model { uint8_t mask[8];unsigned reads,writes,fail_read;bool fail_write,wrong_readback; };
static int read_reg(void *p,uint8_t reg,void *dst,size_t len)
{
 struct model *m=p;assert(reg==0x16 && len==8);++m->reads;
 if(m->fail_read==m->reads)return EIO;
 memcpy(dst,m->mask,8);if(m->wrong_readback && m->writes)((uint8_t *)dst)[7]^=1;
 return 0;
}
static int write_reg(void *p,uint8_t reg,const void *src,size_t len)
{
 struct model *m=p;const uint8_t wanted[8]={2,6,0,0,0,0,0,0};
 assert(reg==0x16 && len==8 && m->reads==1 && !memcmp(src,wanted,8));
 ++m->writes;memcpy(m->mask,src,8);return m->fail_write?EIO:0;
}
static int run(struct model *m,struct sf_cd321x_mask_result *r)
{return sf_cd321x_ensure_mask(m,read_reg,write_reg,r);}
int main(void)
{
 struct model m={0};struct sf_cd321x_mask_result r;
 assert(sf_cd321x_ensure_mask(NULL,read_reg,write_reg,&r)==EINVAL);
 assert(sf_cd321x_ensure_mask(&m,NULL,write_reg,&r)==EINVAL);
 assert(sf_cd321x_ensure_mask(&m,read_reg,NULL,&r)==EINVAL);
 assert(sf_cd321x_ensure_mask(&m,read_reg,write_reg,NULL)==EINVAL);
 assert(!m.reads && !m.writes);
 m=(struct model){.mask={2,6,0,0,0,0,0,0}};
 assert(!run(&m,&r) && m.reads==1 && !m.writes && !r.write_attempted);
 assert(r.before_valid && r.after_valid && !memcmp(r.before,r.after,8));
 m=(struct model){.mask={255,255,255,255,255,255,255,255}};
 assert(!run(&m,&r) && m.reads==2 && m.writes==1 && r.write_attempted);
 assert(r.before_valid && r.after_valid && r.before[7]==255 && !r.after[7]);
 m=(struct model){.fail_read=1};
 assert(run(&m,&r)==EIO && !r.before_valid && !r.write_attempted && !r.after_valid && !m.writes);
 m=(struct model){.fail_write=true};
 assert(run(&m,&r)==EIO && r.before_valid && r.write_attempted && !r.after_valid && m.reads==1);
 m=(struct model){.fail_read=2};
 assert(run(&m,&r)==EIO && r.before_valid && r.write_attempted && !r.after_valid);
 m=(struct model){.wrong_readback=true};
 assert(run(&m,&r)==EIO && r.after_valid && r.after[7]==1);
 puts("CD3217 startup Linux mask: exact64bit/read-only-fastpath/all I/O errors/readback mismatch PASS");
}
