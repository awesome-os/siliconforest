/* SPDX-License-Identifier: MIT */
#include "sf_pci_dma_retire.h"
#include <assert.h>
#include <stdio.h>
struct state {unsigned calls,released;uint32_t fail_complete,fail_clear;};
static uint32_t complete(void *p){struct state *s=p;assert(s->calls++==0);return s->fail_complete;}
static uint32_t clear(void *p){struct state *s=p;assert(s->calls++==1);return s->fail_clear;}
static void release(void *p){struct state *s=p;assert(s->calls++==2);s->released++;}
int main(void){
 struct state good={0};struct sf_pci_dma_retire_ops io={&good,complete,clear,release};
 assert(!sf_pci_dma_retire(&io) && good.calls==3 && good.released==1);
 const uint32_t errors[]={1,22,0xe00002d8,0xffffffff};
 for(unsigned n=0;n<4;++n){
  struct state bad={.fail_complete=errors[n]};io.ctx=&bad;
  assert(sf_pci_dma_retire(&io)==errors[n] && bad.calls==1 && !bad.released);
  bad=(struct state){.fail_clear=errors[n]};
  assert(sf_pci_dma_retire(&io)==errors[n] && bad.calls==2 && !bad.released);
 }
 assert(sf_pci_dma_retire(NULL)==22);
 struct state untouched={0};io.ctx=&untouched;io.clear_inactive=NULL;
 assert(sf_pci_dma_retire(&io)==22 && !untouched.calls);
 puts("actual PCI DMA retirement ordering: callback errors preserved, no clear after failed complete, no release after either failure PASS; not hardware drain proof");
}
