/* SPDX-License-Identifier: MIT */
#include "sf_cd321x_power.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct model {
 unsigned calls,fail,reads,writes,polls,sleeps;
 uint8_t initial,final,result;uint32_t foreign;
 uint64_t now;bool started,stuck,invalid,frozen;
};
static int read_reg(void *ctx,uint8_t reg,void *dst,size_t n)
{
 struct model *m=ctx;++m->reads;if(++m->calls==m->fail)return EIO;
 if(reg==0x20){assert(n==1);*(uint8_t *)dst=m->started?m->final:m->initial;}
 else if(reg==0x09){assert(n==1 && m->started);*(uint8_t *)dst=m->result;}
 else {
  assert(reg==0x08 && n==4);uint32_t v=m->foreign;
  if(m->started){++m->polls;v=m->invalid?0x444d4321u:(m->stuck || m->polls==1)?0x53505353u:0;}
  uint8_t *p=dst;for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));
 }
 return 0;
}
static int write_reg(void *ctx,uint8_t reg,const void *src,size_t n)
{
 struct model *m=ctx;++m->writes;if(++m->calls==m->fail)return EIO;
 if(reg==0x09)assert(n==1 && *(const uint8_t *)src==0 && !m->started);
 else{assert(reg==0x08 && n==4 && !memcmp(src,"SSPS",4));m->started=true;}
 return 0;
}
static uint64_t now_ms(void *ctx){return ((struct model *)ctx)->now;}
static void sleep_ms(void *ctx,unsigned ms)
{struct model *m=ctx;assert(ms==1);++m->sleeps;if(!m->frozen)m->now+=ms;}
static int run(struct model *m,struct sf_cd321x_power_result *r)
{struct sf_cd321x_power_io io={m,read_reg,write_reg,now_ms,sleep_ms};return sf_cd321x_ensure_s0(&io,r);}
int main(void)
{
 struct sf_cd321x_power_result r;
 struct model ready={0};assert(!run(&ready,&r) && ready.calls==1 && !ready.writes && !r.command_issued && !r.polls && !r.before && !r.after);
 for(unsigned state=3;state<=5;state++){
  struct model m={.initial=(uint8_t)state};assert(!run(&m,&r));
  assert(r.before==state && !r.after && r.command_issued && r.polls==2 && m.calls==8 && m.writes==2 && m.sleeps==1);
 }
 for(unsigned fail=1;fail<=8;fail++){
  struct model m={.initial=3,.fail=fail};assert(run(&m,&r)==EIO && m.calls==fail);
  assert(r.command_issued==(fail>=4));
 }
 struct model busy={.initial=3,.foreign=0x41424344};assert(run(&busy,&r)==EBUSY && !busy.writes && !r.command_issued);
 struct model old_invalid={.initial=3,.foreign=0x444d4321};assert(!run(&old_invalid,&r));
 struct model rejected={.initial=3,.result=3};assert(run(&rejected,&r)==EPERM && r.after==3);
 struct model timed={.initial=3,.result=1};assert(run(&timed,&r)==ETIMEDOUT);
 struct model unchanged={.initial=3,.final=3};assert(run(&unchanged,&r)==EIO && r.after==3);
 struct model invalid={.initial=3,.invalid=true};assert(run(&invalid,&r)==EINVAL && r.polls==1);
 for(unsigned frozen=0;frozen<=1;frozen++){
  struct model m={.initial=3,.stuck=true,.frozen=frozen};assert(run(&m,&r)==ETIMEDOUT && r.polls==1001 && m.sleeps==1000);
 }
 assert(sf_cd321x_ensure_s0(NULL,&r)==EINVAL);
 puts("CD321x S0: read-only already-ready; S3/S4/S5; every transport failure; foreign busy/invalid; rejection; deadline and stalled-clock bounds; actual final readback passed");
}
