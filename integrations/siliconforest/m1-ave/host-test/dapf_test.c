/* SPDX-License-Identifier: GPL-2.0-only */
#include "sfave/sfave_dapf.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct bank {uint32_t r0[16],r4[16];uint64_t start[16],end[16];unsigned writes,fail;int locked,bad;};
static int rd32(void *ctx,unsigned bank,uint32_t off,uint32_t *v) {
 struct bank *b=ctx;
 if(bank==SFAVE_DAPF_CPUDART){assert(off==0xf0);*v=b->locked;return 0;}
 assert(bank==SFAVE_DAPF_FILTER&&off<0x400);
 unsigned i=off/0x40;assert(off%0x40==0||off%0x40==4);
 *v=off%0x40?b->r4[i]:b->r0[i];if(b->bad&&i==15)*v^=1;return 0;
}
static int wr32(void *ctx,unsigned bank,uint32_t off,uint32_t v) {
 struct bank *b=ctx;assert(bank==SFAVE_DAPF_FILTER&&off<0x400);
 unsigned i=off/0x40,step=b->writes%4;assert((off%0x40)==(step==0?4:0)&& (step==0||step==3));
 b->writes++;if(b->fail==b->writes)return EIO;
 if(off%0x40)b->r4[i]=v;else b->r0[i]=v;return 0;
}
static int rd64(void *ctx,unsigned bank,uint32_t off,uint64_t *v) {
 struct bank *b=ctx;assert(bank==SFAVE_DAPF_FILTER&&off<0x400);
 assert(off%0x40==8||off%0x40==0x10);*v=off%0x40==8?b->start[off/0x40]:b->end[off/0x40];return 0;
}
static int wr64(void *ctx,unsigned bank,uint32_t off,uint64_t v) {
 struct bank *b=ctx;assert(bank==SFAVE_DAPF_FILTER&&off<0x400);
 unsigned i=off/0x40,step=b->writes%4;assert((off%0x40)==(step==1?8:0x10)&&(step==1||step==2));
 b->writes++;if(b->fail==b->writes)return EIO;
 if(step==1)b->start[i]=v;else b->end[i]=v;return 0;
}
int main(void) {
 struct sfave_preload p={.soc=0x8103,.preloaded=1,.segment_count=2,.cold_reserved=1,
  .segment={{0x100000,0,0x100000,0xcc000,3},{0x300000,0xcc000,0xf000cc000ULL,0x128000,0}},
  .iboot="iBoot-8422.141.2",.firmware="AppleAVE2FW-6070.11.1",.rtkit="RTKit-2062.141.1"};
 struct bank b={0};const struct sfave_dapf_io io={&b,rd32,wr32,rd64,wr64};
 assert(sfave_program_dapf(&p,0,0,1,&io)==EINVAL&&!b.writes);
 assert(sfave_program_dapf(&p,0xf00000000ULL,0xfffffffffULL,0,&io)==EINVAL&&!b.writes);
 b.locked=1;assert(sfave_program_dapf(&p,0xf00000000ULL,0xfffffffffULL,1,&io)==EPERM&&!b.writes);
 b.locked=0;assert(!sfave_program_dapf(&p,0xf00000000ULL,0xfffffffffULL,1,&io));assert(b.writes==64);
 assert(b.start[0]==p.segment[0].physical&&b.end[0]==p.segment[0].physical+0xcc000-4);
 assert(b.start[1]==0xf00000000ULL&&b.end[1]==0xffffffffcULL);
 for(unsigned i=2;i<16;i++)assert(!b.r0[i]&&!b.r4[i]&&!b.start[i]&&!b.end[i]);
 memset(&b,0,sizeof b);b.bad=1;assert(sfave_program_dapf(&p,0xf00000000ULL,0xffffffffcULL,1,&io)==EIO);
 memset(&b,0,sizeof b);b.fail=5;assert(sfave_program_dapf(&p,0xf00000000ULL,0xfffffffffULL,1,&io)==EIO&&b.writes==5);
 puts("AVE DAPF port: synthetic exact write-width/order, live-range parameters, locked/stopped gates, cleared unused slots, mismatch and write-error propagation PASS; no hardware");
}
