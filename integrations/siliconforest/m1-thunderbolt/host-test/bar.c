/* SPDX-License-Identifier: MIT */
#include "sf_pciec_bar.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static void check(uint64_t bus,uint64_t bytes,int expected,uint64_t physical)
{
 uint64_t result=UINT64_MAX;
 assert(sf_pciec_bar_cpu(bus,bytes,&result)==expected);
 assert(result==(expected?UINT64_MAX:physical));
}
int main(void)
{
 uint32_t bars[6]={0x100008,0,0,4,5,4};uint64_t bus=UINT64_MAX;
 assert(!sf_pciec_bar_bus(bars,0,&bus) && bus==0x100000);
 assert(!sf_pciec_bar_bus(bars,3,&bus) && bus==0x500000000ULL);
 const unsigned invalid[]={1,2,4,5,6};
 for(unsigned n=0;n<sizeof(invalid)/sizeof(*invalid);++n){bus=UINT64_MAX;assert(sf_pciec_bar_bus(bars,invalid[n],&bus) && bus==UINT64_MAX);}
 bars[0]=0x80000004;bars[1]=5;
 assert(!sf_pciec_bar_bus(bars,0,&bus) && bus==0x580000000ULL);
 bars[0]=4;bars[1]=0;bus=UINT64_MAX;
 assert(sf_pciec_bar_bus(bars,0,&bus)==ENXIO && bus==UINT64_MAX);
 bars[1]=UINT32_MAX;assert(sf_pciec_bar_bus(bars,0,&bus)==EPROTO && bus==UINT64_MAX);
 bars[0]=1;assert(sf_pciec_bar_bus(bars,0,&bus)==ENXIO && bus==UINT64_MAX);
 bars[0]=2;assert(sf_pciec_bar_bus(bars,0,&bus)==EPROTO && bus==UINT64_MAX);
 assert(sf_pciec_bar_bus(NULL,0,&bus)==EINVAL && sf_pciec_bar_bus(bars,0,NULL)==EINVAL);
 const uint64_t starts[]={0x100000,0x40000000,0x580000000ULL};
 const uint64_t ends[]={0x40000000,0x80000000,0x600000000ULL};
 const uint64_t cpu[]={0x600100000ULL,0x640000000ULL,0x580000000ULL};
 for(unsigned n=0;n<3;++n){
  check(starts[n],1,0,cpu[n]);
  check(starts[n],ends[n]-starts[n],0,cpu[n]);
  check(ends[n]-1,1,0,cpu[n]+ends[n]-starts[n]-1);
  check(starts[n],ends[n]-starts[n]+1,ERANGE,0);
  check(ends[n]-1,2,ERANGE,0);
  check(starts[n],UINT64_MAX,ERANGE,0);
  check(starts[n],0,EINVAL,0);
 }
 const uint64_t invalidWindows[]={0,0xfffff,0x80000000,0x57fffffffULL,
  0x600000000ULL,0x6c0000000ULL,UINT64_MAX};
 for(unsigned n=0;n<sizeof(invalidWindows)/sizeof(*invalidWindows);++n)
  check(invalidWindows[n],1,ERANGE,0);
 assert(sf_pciec_bar_cpu(0x100000,1,NULL)==EINVAL);
 puts("PCIe-C BAR translation: exact windows, ends, overflow and Wi-Fi exclusion PASS");
}
