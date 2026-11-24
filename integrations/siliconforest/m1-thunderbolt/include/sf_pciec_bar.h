/* SPDX-License-Identifier: MIT */
/* T8103 apciec1 ranges: pinned linux-aurora t8103.dtsi, not Wi-Fi apcie. */
#ifndef SF_PCIEC_BAR_H
#define SF_PCIEC_BAR_H
#include <stdint.h>
#include <errno.h>
static inline int sf_pciec_bar_bus(const uint32_t bars[6],unsigned index,uint64_t *bus)
{
 if(!bars || !bus || index>=6)return EINVAL;
 uint32_t low=bars[index];
 if(low==UINT32_MAX || (low&1))return ENXIO;
 unsigned kind=(low>>1)&3;uint64_t value=low&~0xfu;
 if(kind==2){
  if(index==5 || bars[index+1]==UINT32_MAX)return EPROTO;
  value|=(uint64_t)bars[index+1]<<32;
 }else if(kind)return EPROTO;
 if(!value)return ENXIO;
 *bus=value;return 0;
}
static inline int sf_pciec_bar_cpu(uint64_t bus,uint64_t bytes,uint64_t *cpu)
{
 if(!cpu || !bytes)return EINVAL;
 uint64_t end,offset;
 if(bus>=0x00100000 && bus<0x40000000){end=0x40000000;offset=0x600000000ULL;}
 else if(bus>=0x40000000 && bus<0x80000000){end=0x80000000;offset=0x600000000ULL;}
 else if(bus>=0x580000000ULL && bus<0x600000000ULL){end=0x600000000ULL;offset=0;}
 else return ERANGE;
 if(bytes>end-bus)return ERANGE;
 *cpu=bus+offset;return 0;
}
#endif
