/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_pciec_dart_block.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
struct model {
 uint32_t regs[512];unsigned calls,writes,fail,delays,busy;
 bool started,pre_stuck,post_stuck,bad_tcr,bad_root;
};
static bool tcr(uint32_t off){return off>=0x100 && off<0x200 && !(off&3);}
static bool root(uint32_t off){return off>=0x400 && off<0x800 && !(off&3);}
static int read_reg(void *p,uint32_t off,uint32_t *v)
{
 struct model *m=p;assert(tcr(off) || root(off) || off==0x20 || off==0x60);
 if(++m->calls==m->fail)return EIO;
 if(off==0x20){
  bool stuck=m->started?m->post_stuck:m->pre_stuck;
  *v=stuck || (m->started && m->busy)?4:0;
  if(m->started && m->busy)--m->busy;
 }else *v=m->regs[off/4] | (tcr(off) && m->bad_tcr?1u:0) | (root(off) && m->bad_root?1u:0);
 return 0;
}
static int write_reg(void *p,uint32_t off,uint32_t v)
{
 struct model *m=p;assert(tcr(off) || root(off) || off==0x20 || off==0x34 || off==0x38);
 if(++m->calls==m->fail)return EIO;
 if(root(off))for(unsigned sid=0;sid<64;++sid)assert(!m->regs[(0x100+4*sid)/4]);
 if(tcr(off) || root(off))assert(!v);
 if(off==0x34 || off==0x38)assert(v==UINT32_MAX);
 if(off==0x20){
  assert(v==(1u<<20) && m->regs[0x34/4]==UINT32_MAX && m->regs[0x38/4]==UINT32_MAX);
  for(unsigned i=0x400/4;i<0x800/4;++i)assert(!m->regs[i]);
  m->started=true;
 }
 m->regs[off/4]=v;++m->writes;return 0;
}
static void delay(void *p,unsigned us){struct model *m=p;assert(us==1);++m->delays;}
static void init(struct model *m)
{
 memset(m,0,sizeof(*m));m->busy=2;
 for(unsigned i=0x100/4;i<0x200/4;++i)m->regs[i]=UINT32_MAX;
 for(unsigned i=0x400/4;i<0x800/4;++i)m->regs[i]=UINT32_MAX;
}
static int run(struct model *m)
{struct sf_pciec_dart_block_io io={m,read_reg,write_reg,delay};return sf_pciec_dart_block(&io);}
int main(void)
{
 struct model m;init(&m);
 assert(sf_pciec_dart_block(NULL)==EINVAL);
 assert(!run(&m) && m.started && m.writes==64+256+3 && m.delays==2);
 unsigned calls=m.calls;
 for(unsigned fail=1;fail<=calls;++fail){
  init(&m);m.fail=fail;assert(run(&m)==EIO && m.calls==fail);
 }
 init(&m);m.regs[0x60/4]=1u<<15;assert(run(&m)==EPERM && !m.writes);
 init(&m);m.regs[0x60/4]=UINT32_MAX;assert(run(&m)==EIO && !m.writes);
 init(&m);m.pre_stuck=true;assert(run(&m)==ETIMEDOUT && !m.writes && m.delays==100);
 init(&m);m.post_stuck=true;assert(run(&m)==ETIMEDOUT && m.started && m.delays==100);
 init(&m);m.bad_tcr=true;assert(run(&m)==EIO && !m.started && m.writes==1);
 init(&m);m.bad_root=true;assert(run(&m)==EIO && !m.started && m.writes==65);
 printf("PCIe-C DART actual blocker:64TCR/256roots/two selectors/noPARAMS/noenable; all%u I/O failures + lock/readback/busy bounds PASS\n",calls);
}
