#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Execute actual private bus-DMA sync and allocator-identity methods.

Arguments: original and transformed native source trees. The original sync
failure is preserved as FAIL; fixed host fixtures are not hardware DMA proof.
"""
from pathlib import Path
import os,re,subprocess,sys,tempfile
old,new=map(Path,sys.argv[1:]);bus='src/Kernel/fbsdkpi/haiku/freebsd_network/bus_dma.cpp';native='src/Kernel/fbsdkpi/xnu/sf_fbsd_os.cpp'
def function(s,pattern):
 m=re.search(pattern,s);assert m,pattern
 end,depth=m.end(),1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[m.start():end]
lookup=function((new/native).read_text(),r'static IOBufferMemoryDescriptor \*sf_owned_contiguous\([^{};]*\)\s*\{')
fixture=r'''
#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cstdlib>
#include <initializer_list>
using bus_addr_t=size_t;using bus_size_t=size_t;using bus_dmasync_op_t=unsigned;using caddr_t=char *;
enum {BUS_DMASYNC_PREWRITE=1,BUS_DMASYNC_PREREAD=2,BUS_DMASYNC_POSTREAD=4,BUS_DMASYNC_POSTWRITE=8};
struct Mbuf {uint8_t data[16];struct {size_t len;}m_pkthdr;};
struct Cookie {uint8_t *source,*device;size_t length;unsigned out=0,in=0;};
static void sfos_dma_sync(void *p,int output){auto c=(Cookie *)p;if(output){++c->out;memcpy(c->device,c->source,c->length);}else{++c->in;memcpy(c->source,c->device,c->length);}}
struct bus_dmamap {enum {BUFFER_NONE,BUFFER_PROHIBITED,BUFFER_TYPE_SIMPLE,BUFFER_TYPE_MBUF}buffer_type=BUFFER_NONE;void *buffer=nullptr,*bounce_buffer=nullptr;size_t buffer_length=0;Mbuf *mbuf=nullptr;int sf_ncookies=0;void *sf_cookies[2]{};};
using bus_dmamap_t=bus_dmamap *;using bus_dma_tag_t=void *;
static void memory_write_barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);}static void memory_read_barrier(){__atomic_thread_fence(__ATOMIC_SEQ_CST);}
static void panic(const char *){abort();}
#define KASSERT(c,args) assert(c)
static void m_copydata(Mbuf *m,size_t off,size_t n,char *p){assert(off+n<=16);memcpy(p,m->data+off,n);}
static void m_copyback(Mbuf *m,size_t off,size_t n,char *p){assert(off+n<=16);memcpy(m->data+off,p,n);}
extern "C" void bus_dmamap_sync_etc(bus_dma_tag_t,bus_dmamap_t,bus_addr_t,bus_size_t,bus_dmasync_op_t);
extern "C" {
ACTUAL_SYNC_FUNCTIONS
}
using UInt64=uint64_t;using IOByteCount=uint64_t;static constexpr int kIOMemoryMapperNone=0;
struct IOBufferMemoryDescriptor {UInt64 pa=0x800000000ULL,bytes=0x4000,span=0x4000;unsigned refs=1;UInt64 getPhysicalSegment(unsigned,IOByteCount *n,int){*n=span;return pa;}UInt64 getLength(){return bytes;}void retain(){++refs;}};
struct sf_mapping {sf_mapping *next=nullptr;IOBufferMemoryDescriptor *buffer=nullptr;void *map=nullptr,*desc=nullptr;};
static sf_mapping *sMappings;static void *sListLock;static bool locked;
static void lck_mtx_lock(void *){assert(!locked);locked=true;}static void lck_mtx_unlock(void *){assert(locked);locked=false;}
LOOKUP
int main(){
 unsigned failures=0;
 for(auto type:{bus_dmamap::BUFFER_NONE,bus_dmamap::BUFFER_PROHIBITED}){
  uint8_t coherent[16]{};Cookie c{coherent,coherent,16};bus_dmamap m;m.buffer_type=type;m.sf_cookies[0]=&c;m.sf_ncookies=1;
  bus_dmamap_sync(nullptr,&m,BUS_DMASYNC_PREWRITE|BUS_DMASYNC_POSTREAD);
  if(c.out!=1 || c.in!=1){++failures;puts("FAIL: coherent/native mapping synchronization skipped");}
 }
 for(auto type:{bus_dmamap::BUFFER_TYPE_SIMPLE,bus_dmamap::BUFFER_TYPE_MBUF}){
  uint8_t cpu[16],haiku[16],device[16];memset(cpu,0xa5,16);memset(haiku,0x11,16);memset(device,0x22,16);Mbuf mb;memcpy(mb.data,cpu,16);mb.m_pkthdr.len=16;
  Cookie c{haiku,device,16};bus_dmamap m;m.buffer_type=type;m.buffer=cpu;m.bounce_buffer=haiku;m.buffer_length=16;m.mbuf=&mb;m.sf_cookies[0]=&c;m.sf_ncookies=1;
  bus_dmamap_sync(nullptr,&m,BUS_DMASYNC_PREWRITE);
  if(device[0]!=0xa5 || device[15]!=0xa5){++failures;puts("FAIL: native DMA backing received stale Haiku bounce bytes");}
  memset(device,0x77,16);bus_dmamap_sync(nullptr,&m,BUS_DMASYNC_POSTREAD);
  assert((type==bus_dmamap::BUFFER_TYPE_SIMPLE?cpu:mb.data)[0]==0x77 && c.in==1);
 }
 bus_dmamap_sync(nullptr,nullptr,BUS_DMASYNC_PREWRITE);
 IOBufferMemoryDescriptor owned;sf_mapping entry;entry.buffer=&owned;sMappings=&entry;
 auto p=sf_owned_contiguous(owned.pa,4096);assert(p==&owned && owned.refs==2 && !locked);
 assert(!sf_owned_contiguous(owned.pa+4096,4096) && !sf_owned_contiguous(owned.pa,0x4001) && owned.refs==2);
 entry.map=&entry;assert(!sf_owned_contiguous(owned.pa,4096));entry.map=nullptr;entry.desc=&entry;assert(!sf_owned_contiguous(owned.pa,4096));entry.desc=nullptr;
 owned.bytes=4096;assert(!sf_owned_contiguous(owned.pa,4096));owned.bytes=0x4000;owned.span=4096;assert(!sf_owned_contiguous(owned.pa,4096));
 if(failures){printf("Actual source FAIL: %u DMA seam defects reproduced\n",failures);return 1;}
 puts("actual DMA sync/allocator identity: coherent native hooks, CPU->Haiku->device ordering, device->CPU copies and exact retained whole-page ownership/refusals PASS (host only)");return 0;
}
'''.replace('LOOKUP',lookup)
with tempfile.TemporaryDirectory() as tmp:
 tmp=Path(tmp)
 for label,tree in [('old',old),('fixed',new)]:
  s=(tree/bus).read_text();functions=[function(s,r'void\s+bus_dmamap_sync\([^{};]*\)\s*\{'),function(s,r'void\s+bus_dmamap_sync_etc\([^{};]*\)\s*\{')]
  notice=s[:s.index('extern "C"')];text=notice+fixture.replace('ACTUAL_SYNC_FUNCTIONS','\n'.join(functions));(tmp/'test.cpp').write_text(text)
  subprocess.run([os.environ.get('CXX','c++'),'-std=c++17','-O1','-g','-fsanitize=address,undefined',str(tmp/'test.cpp'),'-o',str(tmp/'test')],check=True)
  result=subprocess.run([str(tmp/'test')],check=False)
  if label=='old':
   assert result.returncode==1,('old-source failures were not reproduced',result.returncode)
   print('Original actual-source result: FAIL (preserved reproduction)',flush=True)
  else:assert result.returncode==0,result.returncode
