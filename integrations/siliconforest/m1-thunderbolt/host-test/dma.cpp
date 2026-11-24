/* SPDX-License-Identifier: MIT */
/* Execute the actual CM DMA adapter, with native callback failure injection.
 * These buffers/IOVAs are host fixtures, not kernel or DART observations. */
#include "sf_tb_dma.h"
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>
struct Mem {void *cpu;size_t bytes;uint64_t iova;};
struct Model {
 std::mutex lock;
 std::vector<Mem *> owned;
 uint64_t next=0x04000000;
 bool power=true,retireFail=false,allocFail=false,badIova=false,badCpu=false,partial=false,overflowCpu=false;
 unsigned allocs=0,retires=0;
 void *dev=this;
};
static int faults;
extern "C" void sf_tb_native_fault(int e){assert(e<0);++faults;}
static void lock(void *ctx){((Model *)ctx)->lock.lock();}
static void unlock(void *ctx){((Model *)ctx)->lock.unlock();}
static bool powered(void *ctx){return ((Model *)ctx)->power;}
static int alloc(void *ctx,size_t bytes,size_t align,void **cpu,uint64_t *iova,void **token){
 auto &m=*(Model *)ctx;if(m.allocFail)return -ENOMEM;
 size_t a=align<16384?16384:align,span=(bytes+16383)&~16383UL;
 auto p=new Mem{};assert(!posix_memalign(&p->cpu,a,span));p->bytes=span;
 memset(p->cpu,0xa5,span);m.next=(m.next+a-1)&~(uint64_t)(a-1);p->iova=m.next;m.next+=span;
 *cpu=m.overflowCpu?(void *)(UINTPTR_MAX-159):(m.badCpu?(char *)p->cpu+1:p->cpu);
 *iova=m.badIova?UINT64_MAX:p->iova;*token=p;
 m.owned.push_back(p);++m.allocs;return m.partial?-ENOMEM:0;
}
static int retire(void *ctx,void *token){
 auto &m=*(Model *)ctx;auto p=(Mem *)token;
 if(m.retireFail)return -EIO;
 bool found=false;for(auto &item:m.owned)if(item==p){item=nullptr;found=true;break;}
 assert(found);++m.retires;free(p->cpu);delete p;return 0;
}
int main(int argc,char **argv){
 assert(argc==2);const char *mode=argv[1];Model m;
 sf_tb_dma_ops ops{&m,m.dev,lock,unlock,powered,alloc,retire};
 assert(!sf_tb_dma_install(&ops));assert(sf_tb_dma_install(&ops)==-EBUSY);
 uint64_t dma=0xfeed;assert(!sf_tb_dma_alloc(m.dev,160,&dma,true) && dma==0xfeed);
 sf_tb_dma_admit(true);
 if(!strcmp(mode,"ok")){
  assert(!sf_tb_dma_mask(m.dev,UINT64_MAX));
  auto pool=sf_tb_pool_create("ctl",m.dev,4096,4,0);assert(pool);
  std::vector<std::thread> threads;
  for(int t=0;t<8;++t)threads.emplace_back([&]{for(int i=0;i<1000;++i){
   uint64_t d;auto p=sf_tb_pool_alloc(pool,&d,true);assert(p && d);
   for(unsigned j=0;j<4096;++j)assert(((unsigned char *)p)[j]==0);
   sf_tb_pool_free(pool,p,d);
  }});
  for(auto &t:threads)t.join();sf_tb_pool_destroy(pool);
  assert(m.allocs==8000 && m.retires==8000 && !faults && !sf_tb_dma_drain_status());
 }else if(!strcmp(mode,"alloc_fail")){
  m.allocFail=true;assert(!sf_tb_dma_alloc(m.dev,160,&dma,true));assert(dma==0xfeed && !faults && !sf_tb_dma_drain_status());
 }else if(!strcmp(mode,"atomic")){
  assert(!sf_tb_dma_alloc(m.dev,160,&dma,false) && !m.allocs && !faults);
 }else if(!strcmp(mode,"not_powered")){
  m.power=false;assert(!sf_tb_dma_alloc(m.dev,160,&dma,true));assert(sf_tb_dma_mask(m.dev,UINT64_MAX)==-EIO && !m.allocs);
 }else if(!strcmp(mode,"admission")){
  auto p=sf_tb_dma_alloc(m.dev,160,&dma,true);assert(p);sf_tb_dma_admit(false);
  uint64_t other;assert(!sf_tb_dma_alloc(m.dev,160,&other,true));assert(sf_tb_dma_drain_status()==-EBUSY);
  sf_tb_dma_free(m.dev,160,p,dma);assert(!sf_tb_dma_drain_status() && !faults);
 }else if(!strcmp(mode,"quarantine")){
  auto p=sf_tb_dma_alloc(m.dev,160,&dma,true);assert(p);sf_tb_dma_quarantine();
  sf_tb_dma_free(m.dev,160,p,dma);assert(!m.retires && faults && sf_tb_dma_drain_status()==-EIO);
 }else if(!strcmp(mode,"mask_shrink")){
  auto p=sf_tb_dma_alloc(m.dev,160,&dma,true);assert(p);
  assert(sf_tb_dma_mask(m.dev,0xffffff)==-EOPNOTSUPP);
  sf_tb_dma_free(m.dev,160,p,dma);assert(sf_tb_dma_mask(m.dev,0xffffff)==-EOPNOTSUPP);
  assert(!sf_tb_dma_mask(m.dev,0x1fffffff));
  p=sf_tb_dma_alloc(m.dev,160,&dma,true);assert(p && !faults);sf_tb_dma_free(m.dev,160,p,dma);
 }else if(!strcmp(mode,"bad_iova") || !strcmp(mode,"bad_cpu") || !strcmp(mode,"partial") ||
          !strcmp(mode,"overflow_cpu") || !strcmp(mode,"aperture_end") || !strcmp(mode,"below_aperture")){
  m.badIova=!strcmp(mode,"bad_iova");m.badCpu=!strcmp(mode,"bad_cpu");m.partial=!strcmp(mode,"partial");
  m.overflowCpu=!strcmp(mode,"overflow_cpu");
  if(!strcmp(mode,"aperture_end"))m.next=SF_TB_DMA_END;
  if(!strcmp(mode,"below_aperture"))m.next=SF_TB_DMA_START-16384;
  assert(!sf_tb_dma_alloc(m.dev,160,&dma,true) && dma==0xfeed && faults && !m.retires);
  assert(sf_tb_dma_drain_status()==-EIO);
 }else if(!strcmp(mode,"capacity")){
  void *p[256];uint64_t d[256];for(unsigned i=0;i<256;++i){p[i]=sf_tb_dma_alloc(m.dev,160,&d[i],true);assert(p[i]);}
  assert(!sf_tb_dma_alloc(m.dev,160,&dma,true));for(unsigned i=0;i<256;++i)sf_tb_dma_free(m.dev,160,p[i],d[i]);
  assert(!faults && !sf_tb_dma_drain_status());
 }else if(!strcmp(mode,"busy_destroy")){
  auto pool=sf_tb_pool_create("ctl",m.dev,4096,4,0);assert(pool);
  auto p=sf_tb_pool_alloc(pool,&dma,true);assert(p);sf_tb_pool_destroy(pool);
  assert(faults && sf_tb_dma_drain_status()==-EIO && !m.retires);
 }else if(!strcmp(mode,"boundary")){
  auto pool=sf_tb_pool_create("ctl",m.dev,4096,4,65536);assert(pool);
  auto p=sf_tb_pool_alloc(pool,&dma,true);assert(p && dma%65536==0);
  sf_tb_pool_free(pool,p,dma);sf_tb_pool_destroy(pool);assert(!faults && !sf_tb_dma_drain_status());
 }else{
  auto p=sf_tb_dma_alloc(m.dev,160,&dma,true);assert(p);
  if(!strcmp(mode,"retire_fault"))m.retireFail=true;
  if(!strcmp(mode,"wrong_device"))sf_tb_dma_free(&ops,160,p,dma);
  else if(!strcmp(mode,"wrong_size"))sf_tb_dma_free(m.dev,161,p,dma);
  else if(!strcmp(mode,"duplicate_free")){sf_tb_dma_free(m.dev,160,p,dma);sf_tb_dma_free(m.dev,160,p,dma);}
  else sf_tb_dma_free(m.dev,160,p,dma);
  assert(faults && sf_tb_dma_drain_status()==-EIO);
  if(strcmp(mode,"duplicate_free"))assert(!m.retires);
 }
 // Fixture-only process teardown, after assertions: reaping quarantined host
 // allocations does not call the tested backend or claim hardware retirement.
 for(auto p:m.owned)if(p){free(p->cpu);delete p;}
 printf("CM DMA callback scenario %s PASS (faults=%d)\n",mode,faults);
}
