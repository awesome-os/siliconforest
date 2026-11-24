// SPDX-License-Identifier: MIT
#include "sf_pciec_port.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <map>
#include <utility>
struct Device {
 std::map<std::pair<unsigned,unsigned>,uint32_t> regs;
 unsigned calls=0,fail=0,reads=0,writes=0;
 static int read(void *ctx,sf_pciec_window win,uint32_t off,uint32_t *out){
  auto &d=*(Device *)ctx;assert((unsigned)win<=SF_PCIEC_OE && !(off&3) && off<0x4000);
  ++d.reads;if(++d.calls==d.fail)return -EIO;
  auto key=std::make_pair((unsigned)win,off);
  auto found=d.regs.find(key);*out=found==d.regs.end()?0xa5a5a5a5:found->second;return 0;
 }
 static int write(void *ctx,sf_pciec_window win,uint32_t off,uint32_t value){
  auto &d=*(Device *)ctx;++d.writes;if(++d.calls==d.fail)return -EIO;
  d.regs[{win,off}]=value;return 0;
 }
 sf_pciec_io io(){return {this,read,write,nullptr,nullptr};}
 int apply(){auto o=io();for(unsigned win=0;win<5;++win){
   int ret=sf_pciec_t8103_tune(&o,(sf_pciec_window)win);if(ret)return ret;
  }return 0;}
};
int main(){
 Device d;assert(d.apply()==0 && d.reads==43 && d.writes>0);
 assert(d.regs[std::make_pair(SF_PCIEC_CONFIG,0x814)]==0xf060);
 assert((d.regs[std::make_pair(SF_PCIEC_CONFIG,0x78)]&0x7000)==0);
 assert(d.regs[std::make_pair(SF_PCIEC_DEBUG,0x1000)]==0xa5a5a5a4);
 assert((d.regs[std::make_pair(SF_PCIEC_OE,4)]&0x70007)==0x40004);
 unsigned operations=d.calls;auto saved=d.regs;
 d.calls=d.reads=d.writes=0;assert(d.apply()==0 && d.reads==43 && !d.writes && d.regs==saved);
 for(unsigned n=1;n<=operations;++n){Device failure;failure.fail=n;
  assert(failure.apply()==-EIO && failure.calls==n);}
 Device invalid;auto io=invalid.io();
 assert(sf_pciec_t8103_tune(nullptr,SF_PCIEC_CONFIG)==-EINVAL);
 assert(sf_pciec_t8103_tune(&io,(sf_pciec_window)5)==-EINVAL);
 io.read=nullptr;assert(sf_pciec_t8103_tune(&io,SF_PCIEC_CONFIG)==-EINVAL);
 io=invalid.io();io.write=nullptr;assert(sf_pciec_t8103_tune(&io,SF_PCIEC_CONFIG)==-EINVAL);
 assert(invalid.calls==0);
 puts("published T8103 PCIe tuning: 43 records, every real I/O failure, critical masks, unchanged second application PASS; no hardware claim");
}
