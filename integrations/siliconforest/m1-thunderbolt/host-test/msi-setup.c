/* SPDX-License-Identifier: MIT */
#include "sf_pciec_msi_setup.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct backend {unsigned operations,fail;uint32_t regs[3],off[6],value[6];unsigned writes;int mismatch;};
static int index_of(uint32_t off){return off==0x124?0:off==0x128?1:off==0x168?2:-1;}
static int read_word(void *ctx,uint32_t off,uint32_t *value)
{
 struct backend *b=ctx;assert(index_of(off)>=0);
 if(++b->operations==b->fail)return ENODEV;
 *value=b->regs[index_of(off)];
 if(b->writes==6 && index_of(off)==b->mismatch)*value^=1;
 return 0;
}
static int write_word(void *ctx,uint32_t off,uint32_t value)
{
 struct backend *b=ctx;if(++b->operations==b->fail)return ENODEV;
 assert(b->writes<6);b->off[b->writes]=off;b->value[b->writes++]=value;
 int n=index_of(off);if(n>=0)b->regs[n]=value;return 0;
}
int main(void)
{
 const uint32_t offsets[6]={0x104,0x100,0x210,0x168,0x128,0x124};
 const uint32_t values[6]={UINT32_MAX,UINT32_MAX,UINT32_MAX,0xfffff000,0,0x51};
 for(unsigned failure=0;failure<=12;++failure){
  struct backend b={.fail=failure,.mismatch=-1};
  struct sf_pciec_msi_setup_io io={&b,read_word,write_word};
  struct sf_pciec_msi_setup_report report={0};
  int ret=sf_pciec_msi_setup(&io,&report);
  assert(ret==(failure?ENODEV:0));assert(b.operations==(failure?failure:12));
  assert(report.before_count<=3 && report.after_count<=3);
  for(unsigned n=0;n<b.writes;++n){assert(b.off[n]==offsets[n]);assert(b.value[n]==values[n]);}
  if(!failure){assert(report.before_count==3 && report.after_count==3);assert(report.after[0]==0x51 && !report.after[1] && report.after[2]==0xfffff000);}
 }
 for(int n=0;n<3;++n){
  struct backend b={.mismatch=n};struct sf_pciec_msi_setup_io io={&b,read_word,write_word};
  struct sf_pciec_msi_setup_report report={0};assert(sf_pciec_msi_setup(&io,&report)==EIO);
  assert(report.after_count==(unsigned)n+1);
  b=(struct backend){.mismatch=-1};b.regs[n]=UINT32_MAX;
  assert(sf_pciec_msi_setup(&io,&report)==ENODEV);assert(!b.writes);
 }
 struct sf_pciec_msi_setup_report report={0};assert(sf_pciec_msi_setup(NULL,&report)==EINVAL);
 puts("PCIe-C MSI setup: Linux mask/ack/address/base/enable order, all12 I/O refusals, inaccessible/mismatched readbacks PASS; host only, no IRQ/DMA proof");
 return 0;
}
