/* SPDX-License-Identifier: GPL-2.0 */
#include "sf_pciec_dart_sid0.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct model {
 uint32_t words[512];
 unsigned calls,fail,writes,published,flushes,delays,busy;
 bool pre_busy,post_busy,bad_root,bad_tcr,publish_fail;
};
static bool tcr(uint32_t o){return o>=0x100 && o<0x200 && !(o&3);}
static bool root(uint32_t o){return o>=0x400 && o<0x800 && !(o&3);}
static int read_reg(void *p,uint32_t off,uint32_t *out)
{
 struct model *m=p;
 assert(tcr(off) || root(off) || off==0x20 || off==0x40 || off==0x60 || off==0xfc);
 if(++m->calls==m->fail)return EIO;
 if(off==0x20){
  *out=m->pre_busy || (m->flushes && (m->post_busy || m->busy))?4:0;
  if(m->flushes && m->busy)--m->busy;
 }else{
  *out=m->words[off/4];
  if(m->writes && ((m->bad_root && off==0x400) || (m->bad_tcr && off==0x100)))*out^=1;
 }
 return 0;
}
static int write_reg(void *p,uint32_t off,uint32_t value)
{
 struct model *m=p;
 assert(off==0x100 || (off>=0x400 && off<0x410 && !(off&3)) ||
        off==0xfc || off==0x34 || off==0x38 || off==0x20);
 if(++m->calls==m->fail)return EIO;
 if(off==0x400 && value){
  assert(m->published && !m->words[0x100/4]);
  assert(value==((1u<<31)|(0x800004000ULL>>12)));
 }
 if(off==0x100){
  assert(value==0 || value==0x80); // No bypass/remap/upper-enable mask.
  if(value)assert(m->flushes && (m->words[0xfc/4]&1) && m->words[0x400/4]);
 }
 if(off>=0x404 && off<0x410)assert(!value && !m->words[0x100/4]);
 if(off==0x20){
  assert(value==(1u<<20) && m->words[0x34/4]==1 && !m->words[0x38/4]);
  assert(m->published);
  ++m->flushes;m->busy=2;
 }
 m->words[off/4]=value;++m->writes;return 0;
}
static void delay(void *p,unsigned us)
{struct model *m=p;assert(us==1);++m->delays;}
static int publish(void *p)
{
 struct model *m=p;
 if(++m->calls==m->fail || m->publish_fail)return EIO;
 ++m->published;return 0;
}
static void init(struct model *m){memset(m,0,sizeof(*m));m->words[0xfc/4]=0x55000000;}
static struct sf_pciec_dart_sid0_io io(struct model *m)
{struct sf_pciec_dart_sid0_io out={{m,read_reg,write_reg,delay},publish};return out;}
static int prepare(struct model *m,bool *touched)
{struct sf_pciec_dart_sid0_io regs=io(m);return sf_pciec_dart_sid0_prepare(&regs,0x800004000ULL,touched);}
static void isolated(struct model *m,bool active)
{
 for(unsigned sid=0;sid<64;++sid)assert(m->words[(0x100+4*sid)/4]==(active && !sid?0x80u:0));
 for(unsigned n=1;n<256;++n)assert(!m->words[(0x400+4*n)/4]);
 assert(m->words[0xfc/4]==0x55000001); // Unrelated global gate bits preserved.
}
int main(void)
{
 struct model m;bool touched;init(&m);struct sf_pciec_dart_sid0_io regs=io(&m);
 assert(sf_pciec_dart_sid0_prepare(NULL,0x4000,&touched)==EINVAL && !touched);
 assert(sf_pciec_dart_sid0_prepare(&regs,0,&touched)==EINVAL && !m.calls);
 assert(sf_pciec_dart_sid0_prepare(&regs,0x4001,&touched)==EINVAL && !m.calls);
 assert(sf_pciec_dart_sid0_prepare(&regs,1ULL<<36,&touched)==EINVAL && !m.calls);
 assert(sf_pciec_dart_sid0_prepare(&regs,0x4000,NULL)==EINVAL && !m.calls);
 assert(!prepare(&m,&touched) && touched && m.flushes==2 && m.published==3);
 isolated(&m,true);unsigned calls=m.calls;
 for(unsigned fail=1;fail<=calls;++fail){
  init(&m);m.fail=fail;assert(prepare(&m,&touched)==EIO && m.calls==fail);
  if(!touched)assert(!m.writes);
 }
 for(unsigned sid=0;sid<64;++sid){
  init(&m);m.words[(0x100+4*sid)/4]=0x80;
  assert(prepare(&m,&touched)==EBUSY && !touched && !m.writes);
 }
 for(unsigned n=0;n<256;++n){
  init(&m);m.words[(0x400+4*n)/4]=1u<<31;
  assert(prepare(&m,&touched)==EBUSY && !touched && !m.writes);
 }
 init(&m);m.words[0x60/4]=1u<<15;assert(prepare(&m,&touched)==EPERM && !touched);
 init(&m);m.words[0x60/4]=UINT32_MAX;assert(prepare(&m,&touched)==EIO && !touched);
 init(&m);m.words[0x40/4]=1u<<31;assert(prepare(&m,&touched)==EIO && !touched && !m.writes);
 init(&m);m.pre_busy=true;assert(prepare(&m,&touched)==ETIMEDOUT && !touched && m.delays==100);
 init(&m);m.post_busy=true;assert(prepare(&m,&touched)==ETIMEDOUT && touched && m.delays==100);
 init(&m);m.bad_root=true;assert(prepare(&m,&touched)==EIO && touched && !m.flushes);
 init(&m);m.bad_tcr=true;assert(prepare(&m,&touched)==EIO && touched && m.flushes==1);
 init(&m);m.publish_fail=true;assert(prepare(&m,&touched)==EIO && !touched && !m.writes);
 init(&m);assert(!prepare(&m,&touched));
 unsigned before=m.calls;regs=io(&m);assert(!sf_pciec_dart_sid0_disable(&regs,0x800004000ULL));
 unsigned disable_calls=m.calls-before;isolated(&m,false);assert(!m.words[0x400/4]);
 for(unsigned fail=1;fail<=disable_calls;++fail){
  init(&m);assert(!prepare(&m,&touched));m.fail=m.calls+fail;regs=io(&m);
  assert(sf_pciec_dart_sid0_disable(&regs,0x800004000ULL)==EIO && m.calls==m.fail);
 }
 init(&m);assert(!prepare(&m,&touched));regs=io(&m);unsigned writes=m.writes;
 assert(sf_pciec_dart_sid0_disable(&regs,0x800008000ULL)==EBUSY && m.writes==writes);
 for(unsigned sid=1;sid<64;++sid){
  init(&m);assert(!prepare(&m,&touched));regs=io(&m);writes=m.writes;
  m.words[(0x100+4*sid)/4]=0x80;
  assert(sf_pciec_dart_sid0_disable(&regs,0x800004000ULL)==EBUSY && m.writes==writes);
 }
 printf("PCIe-C SID0: all%u prepare/%u disable I/O failures;64TCR/256roots isolation,36bit root, ordered flush/gates, poison-boundary/readback/timeout PASS (host only)\n",calls,disable_calls);
}
