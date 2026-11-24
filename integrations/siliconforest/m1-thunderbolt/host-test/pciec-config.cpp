// SPDX-License-Identifier: MIT
#include "sf_pciec_config.h"
#include <cassert>
#include <cerrno>
#include <climits>
#include <initializer_list>
#include <cstdio>
int main()
{
 sf_pciec_config_view view={0x10000000,0,127,true};uint64_t off;
 for(unsigned bus: {1u,2u,3u,7u,127u})for(unsigned dev=0;dev<32;dev++)for(unsigned fn=0;fn<8;fn++){
  assert(!sf_pciec_config_offset(&view,bus,dev,fn,4092,4,&off));
  assert(off==((uint64_t)bus<<20 | (uint64_t)dev<<15 | (uint64_t)fn<<12 | 4092));
 }
 for(unsigned dev=0;dev<32;dev++)for(unsigned fn=0;fn<8;fn++)
  assert(sf_pciec_config_offset(&view,0,dev,fn,0,4,&off)==(dev||fn?-ENODEV:0));
 for(unsigned width: {1u,2u,4u}){
  view.bytes=0x8000000;assert(!sf_pciec_config_offset(&view,127,31,7,4096-width,width,&off));
  --view.bytes;uint64_t before=off;
  assert(sf_pciec_config_offset(&view,127,31,7,4096-width,width,&off)==-ERANGE && off==before);
 }
 view.bytes=0x10000000;
 for(unsigned bad: {0u,3u,8u,UINT_MAX})assert(sf_pciec_config_offset(&view,3,0,0,0,bad,&off)==-EINVAL);
 assert(sf_pciec_config_offset(&view,3,32,0,0,4,&off)==-EINVAL);
 assert(sf_pciec_config_offset(&view,3,0,8,0,4,&off)==-EINVAL);
 assert(sf_pciec_config_offset(&view,128,0,0,0,4,&off)==-ERANGE);
 assert(sf_pciec_config_offset(&view,UINT_MAX,0,0,0,4,&off)==-ERANGE);
 assert(sf_pciec_config_offset(&view,3,0,0,4095,4,&off)==-EINVAL);
 assert(sf_pciec_config_offset(&view,3,0,0,UINT_MAX,4,&off)==-EINVAL);
 view.admitted=false;off=0xdead;
 assert(sf_pciec_config_offset(&view,3,0,0,0,4,&off)==-ENODEV && off==0xdead);
 view={0x200000,126,127,true};assert(!sf_pciec_config_offset(&view,127,0,0,0,4,&off) && off==0x100000);
 assert(sf_pciec_config_offset(&view,126,1,0,0,4,&off)==-ENODEV);
 uint32_t value;
 for(unsigned width: {1u,2u,4u})for(unsigned original: {0u,7u,UINT_MAX}){
  assert(!sf_pciec_config_write_filter(4,width,original,false,&value) && value==(original&~4u));
  assert(!sf_pciec_config_write_filter(4,width,original,true,&value) && value==original);
 }
 assert(!sf_pciec_config_write_filter(5,1,UINT_MAX,false,&value) && value==UINT_MAX);
 assert(!sf_pciec_config_write_filter(0x10,4,UINT_MAX,false,&value) && value==UINT_MAX);
 value=0xdead;assert(sf_pciec_config_write_filter(4,3,UINT_MAX,false,&value)==-EINVAL && value==0xdead);
 puts("PCIe-C ECAM host only: subordinate device1..31, root exclusions, byte-exact aperture edges, rejected revocation/overflow/unaligned access, bus-master filtering");
}
