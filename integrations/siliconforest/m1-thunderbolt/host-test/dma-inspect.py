#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute the actual bounded snapshot method against owned-memory/MMIO fixtures.

The native AT instruction is SDK-compiled separately, not executed on this host.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile

here = Path(__file__).parents[1]
source = (here / 'kernel/SFCIOPCIDartMapper.cpp').read_text()
match = re.search(r'IOReturn SFCIOPCIDartMapper::inspectDMA\([^\n]+\)\s*\{', source)
assert match
end, depth = match.end(), 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
method = source[match.start():end]
fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include "sf_pciec_dma_inspect.h"
using IOReturn=int;using IOByteCount=uint64_t;
constexpr int kIOReturnSuccess=0,kIOReturnNotReady=1,kIOReturnBadArgument=2,kIOMemoryMapperNone=0;
constexpr unsigned pciMappings=2048;
struct IOService{};
static bool interruptContext;static unsigned locks,reads,walks,pars;static int failRead=-1;
static bool ml_at_interrupt_context(){return interruptContext;}
static void IOLockLock(void *){assert(!interruptContext);++locks;}
static void IOLockUnlock(void *){assert(locks);--locks;}
static uint64_t sf_cpu_par(const void *){++pars;return 0x4400000123400000ULL;}
struct Memory {uint64_t words[4]={1,2,3,4};uint64_t pa=0x123400000;
 void *getBytesNoCopy(){return words;}
 uint64_t getPhysicalSegment(int,IOByteCount *span,int){*span=0x4000;return pa;}
};
struct sf_dart_walk {uint64_t l1e=0,l2_phys=0,pte=0,phys=0;};
static int sf_dart_table_walk(void *,uint64_t iova,sf_dart_walk *out){++walks;
 out->l1e=0x1003;out->pte=0x123400003;out->phys=0x123400000+iova;return 0;}
constexpr int SF_PCIEC_PORT=1;
struct Controller {void *lock=this;
 static int read(void *,int win,uint32_t off,uint32_t *out){assert(win==SF_PCIEC_PORT && off>=0x828 && off<0x928);*out=off==0x828?0x80000300:0;return 0;}
};
using SFCIOPCIController=Controller;
struct SFCIOPCIDartMapper {
 IOService *endpoint;void *lock=this;Controller *controller;bool ready=true,powered=true;
 unsigned tableCount=1,mappingCount=4;void *pt=this;
 struct Table {Memory *memory;uint64_t physical;};Table tables[1];
 struct Mapping {Memory *memory=nullptr;uint64_t address=0,bytes=0;};Mapping *mappings;
 bool live(){return powered;}
 static int read(void *,uint32_t off,uint32_t *out){
  static const uint32_t allowed[]={0x20,0x40,0x50,0x54,0x60,0xfc,0x100,0x400,0x404,0x408,0x40c,0x104};
  assert(reads<76);
  if(reads<12)assert(off==allowed[reads]);else assert(off==0x100+4*(reads-12));
  if(int(reads++)==failRead)return 1;
  *out=off==0x40?0x80000001:off;return 0;
 }
 IOReturn inspectDMA(IOService *,void *);
};
METHOD
int main(){
 IOService endpoint,foreign;Controller controller;Memory memory;
 SFCIOPCIDartMapper::Mapping mappings[pciMappings]{};
 for(unsigned n: {0u,1u,257u,258u})mappings[n]={&memory,0x4000000+0x4000ULL*n,0x4000};
 SFCIOPCIDartMapper mapper;mapper.endpoint=&endpoint;mapper.controller=&controller;
 mapper.tables[0]={&memory,memory.pa};mapper.mappings=mappings;
 sf_pciec_dma_inspect s{};
 auto reset=[&]{s={};s.version=SF_PCIEC_DMA_INSPECT_VERSION;s.size=sizeof(s);reads=walks=pars=0;failRead=-1;};
 reset();assert(mapper.inspectDMA(&endpoint,&s)==0 && !locks);
 assert(reads==76 && s.rid2sid[0]==0x80000300 && s.tcr[63]==0x1fc && walks==4 && pars==5 && s.mapping_count==4 && s.table_count==1);
 assert(s.regs[1]==0x80000001); // Capture a valid fault without clearing/poisoning it.
 unsigned selected[]={0,1,257,258};
 for(unsigned n=0;n<4;++n){auto &m=s.sample[n];assert(m.slot==selected[n]);
  assert(m.iova==mappings[selected[n]].address && m.physical==memory.pa && m.span==0x4000);
  assert(!m.walk_error && m.walked==memory.pa+m.iova && m.words[3]==4);}
 reset();s.version=2;assert(mapper.inspectDMA(&endpoint,&s)==2 && !reads);
 reset();--s.size;assert(mapper.inspectDMA(&endpoint,&s)==2 && !reads);
 reset();assert(mapper.inspectDMA(&foreign,&s)==1 && !reads && !locks);
 reset();interruptContext=true;assert(mapper.inspectDMA(&endpoint,&s)==2 && !reads);interruptContext=false;
 reset();mapper.powered=false;assert(mapper.inspectDMA(&endpoint,&s)==1 && !reads);mapper.powered=true;
 reset();mapper.ready=false;assert(mapper.inspectDMA(&endpoint,&s)==1 && !reads);mapper.ready=true;
 reset();failRead=3;assert(mapper.inspectDMA(&endpoint,&s)==1 && reads==4 && !walks && !pars && !locks);
 // Empty and sparse mapping arrays never inspect an arbitrary/missing address.
 memset(mappings,0,sizeof(mappings));reset();mapper.mappingCount=0;
 assert(mapper.inspectDMA(&endpoint,&s)==0 && walks==0 && pars==1);
 for(auto &m:s.sample)assert(m.slot==pciMappings && !m.iova);
 mappings[100]={&memory,0x5000000,0x4000};reset();mapper.mappingCount=1;
 assert(mapper.inspectDMA(&endpoint,&s)==0 && walks==2 && pars==3);
 assert(s.sample[0].slot==100 && s.sample[1].slot==pciMappings && s.sample[2].slot==pciMappings && s.sample[3].slot==100);
 puts("actual DMA snapshot: retained selection/identity/version/power/IRQ/fault/bounded-read cases PASS; no native MMIO/AT execution");
}
'''.replace('METHOD', method).replace('#include <cstring>', '#include <cstring>\n#include <initializer_list>')
with tempfile.TemporaryDirectory() as directory:
    tmp = Path(directory)
    (tmp / 'test.cpp').write_text(fixture)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-O1', '-g',
                    '-fsanitize=address,undefined', '-I' + str(here / 'include'),
                    str(tmp / 'test.cpp'), '-o', str(tmp / 'test')], check=True)
    subprocess.run([str(tmp / 'test')], check=True)
