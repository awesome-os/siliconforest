// SPDX-License-Identifier: MIT
/* CM-only coherent/pool DMA adapter; no generic LinuxKPI physical fallback.
 * Metadata is serialized; actual allocation/retirement is the native DART
 * callback. Admission, counters and tokens alone never establish quiescence.
 */
#include "sf_tb_dma.h"
#include <errno.h>
#include <string.h>
#define NALLOC 256
#define NPOOL 32
#define MAX_BYTES (256u*1024u*1024u)
#define PAGE 16384u
struct pool { bool used;size_t bytes,align,boundary; };
struct allocation { bool used;struct pool *pool;void *cpu,*token;uint64_t iova;size_t bytes; };
static struct sf_tb_dma_ops ops;
static struct pool pools[NPOOL];
static struct allocation allocations[NALLOC];
static int installed; // 0 uninstalled, 1 installing, 2 release-published
static bool admitting,failed;
static uint64_t mask=(1ULL<<42)-1;
void sf_tb_native_fault(int);
static int fault(int e){failed=true;sf_tb_native_fault(-e);return -e;}
static bool available(void){return __atomic_load_n(&installed,__ATOMIC_ACQUIRE)==2;}
int sf_tb_dma_install(const struct sf_tb_dma_ops *o)
{
 if(!o || !o->device || !o->lock || !o->unlock || !o->powered || !o->alloc || !o->retire)return -EINVAL;
 int expected=0;
 if(!__atomic_compare_exchange_n(&installed,&expected,1,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE))return -EBUSY;
 ops=*o;__atomic_store_n(&installed,2,__ATOMIC_RELEASE);return 0;
}
void sf_tb_dma_admit(bool yes)
{
 if(!available())return;
 ops.lock(ops.ctx);admitting=yes && !failed;ops.unlock(ops.ctx);
}
void sf_tb_dma_quarantine(void)
{
 if(!available())return;
 ops.lock(ops.ctx);admitting=false;fault(EIO);ops.unlock(ops.ctx);
}
int sf_tb_dma_drain_status(void)
{
 if(!available())return -ENODEV;
 ops.lock(ops.ctx);int ret=failed?-EIO:0;
 for(unsigned i=0;!ret && i<NALLOC;++i)if(allocations[i].used)ret=-EBUSY;
 ops.unlock(ops.ctx);return ret;
}
int sf_tb_dma_mask(void *device,uint64_t requested)
{
 if(!available() || device!=ops.device || !requested || (requested&(requested+1)))return -EINVAL;
 if(requested<SF_TB_DMA_END-1)return -EOPNOTSUPP;
 ops.lock(ops.ctx);int ret=0;
 // All addresses must also fit the physical NHI's 42-bit bus limit.
 uint64_t next=requested<((1ULL<<42)-1)?requested:((1ULL<<42)-1);
 if(failed || !ops.powered(ops.ctx))ret=-EIO;
 for(unsigned i=0;!ret && i<NALLOC;++i)if(allocations[i].used &&
     (allocations[i].iova>next || allocations[i].bytes-1>next-allocations[i].iova))ret=-EBUSY;
 if(!ret)mask=next;
 ops.unlock(ops.ctx);return ret;
}
static void *allocate(struct pool *pool,size_t bytes,size_t align,size_t boundary,void *handle,bool may_sleep)
{
 if(!may_sleep || !handle || !bytes || bytes>MAX_BYTES || !align || (align&(align-1)) ||
    align>MAX_BYTES || (boundary && ((boundary&(boundary-1)) || bytes>boundary || boundary>MAX_BYTES)))return NULL;
 if(!admitting || failed || !ops.powered(ops.ctx))return NULL;
 unsigned i;for(i=0;i<NALLOC && allocations[i].used;++i){}
 if(i==NALLOC)return NULL;
 struct allocation *a=&allocations[i];
 // Reserve the metadata before invoking a backend which might retain backing.
 *a=(struct allocation){.used=true,.pool=pool,.bytes=bytes};
 int ret=ops.alloc(ops.ctx,bytes,boundary>align?boundary:align,&a->cpu,&a->iova,&a->token);
 if(ret){
  if(a->token || a->cpu || a->iova || ret!=-ENOMEM){fault(EIO);return NULL;}
  memset(a,0,sizeof(*a));return NULL;
 }
 if(!a->cpu || !a->token || (uintptr_t)a->cpu>UINTPTR_MAX-bytes ||
    a->iova<SF_TB_DMA_START || a->iova>=SF_TB_DMA_END || bytes>SF_TB_DMA_END-a->iova ||
    a->iova>mask || bytes-1>mask-a->iova ||
    (a->iova&(align-1)) || ((uintptr_t)a->cpu&(align-1)) ||
    (boundary && a->iova/boundary!=(a->iova+bytes-1)/boundary)){
  fault(EPROTO);return NULL; // Preserve untrusted/failed mapping until reboot.
 }
 for(unsigned j=0;j<NALLOC;++j)if(j!=i && allocations[j].used && allocations[j].cpu &&
    (allocations[j].token==a->token ||
     ((uintptr_t)allocations[j].cpu<(uintptr_t)a->cpu+bytes && (uintptr_t)a->cpu<(uintptr_t)allocations[j].cpu+allocations[j].bytes) ||
     (allocations[j].iova<a->iova+bytes && a->iova<allocations[j].iova+allocations[j].bytes))){
  fault(EPROTO);return NULL;
 }
 memset(a->cpu,0,bytes);
 memcpy(handle,&a->iova,sizeof(a->iova));return a->cpu;
}
void *sf_tb_dma_alloc(void *device,size_t bytes,void *handle,bool may_sleep)
{
 if(!available() || device!=ops.device)return NULL;
 ops.lock(ops.ctx);void *cpu=allocate(NULL,bytes,PAGE,0,handle,may_sleep);ops.unlock(ops.ctx);return cpu;
}
static void retire(struct pool *pool,size_t bytes,void *cpu,uint64_t iova)
{
 struct allocation *a=NULL;
 for(unsigned i=0;i<NALLOC;++i)if(allocations[i].used && allocations[i].cpu==cpu &&
    allocations[i].iova==iova && allocations[i].pool==pool && allocations[i].bytes==bytes){a=&allocations[i];break;}
 if(!a){fault(EINVAL);return;}
 if(failed)return;
 if(!ops.powered(ops.ctx) || ops.retire(ops.ctx,a->token)){fault(EIO);return;}
 memset(a,0,sizeof(*a));
}
void sf_tb_dma_free(void *device,size_t bytes,void *cpu,uint64_t iova)
{
 if(!available())return;
 ops.lock(ops.ctx);
 if(device!=ops.device || !cpu || !bytes)fault(EINVAL);else retire(NULL,bytes,cpu,iova);
 ops.unlock(ops.ctx);
}
static struct pool *find_pool(void *object)
{
 for(unsigned i=0;i<NPOOL;++i)if(object==&pools[i] && pools[i].used)return &pools[i];
 return NULL;
}
void *sf_tb_pool_create(const char *name,void *device,size_t bytes,size_t align,size_t boundary)
{
 (void)name;
 if(!available() || device!=ops.device || !bytes || bytes>MAX_BYTES || !align ||
    (align&(align-1)) || align>MAX_BYTES || (boundary && ((boundary&(boundary-1)) || bytes>boundary || boundary>MAX_BYTES)))return NULL;
 ops.lock(ops.ctx);struct pool *p=NULL;
 if(admitting && !failed && ops.powered(ops.ctx))for(unsigned i=0;i<NPOOL;++i)if(!pools[i].used){
  p=&pools[i];*p=(struct pool){true,bytes,align,boundary};break;
 }
 ops.unlock(ops.ctx);return p;
}
void sf_tb_pool_destroy(void *object)
{
 if(!available() || !object)return;
 ops.lock(ops.ctx);struct pool *p=find_pool(object);bool pending=false;
 if(!p)fault(EINVAL);
 else{
  for(unsigned i=0;i<NALLOC;++i)if(allocations[i].used && allocations[i].pool==p)pending=true;
  if(pending)fault(EBUSY);else memset(p,0,sizeof(*p));
 }
 ops.unlock(ops.ctx);
}
void *sf_tb_pool_alloc(void *object,void *handle,bool may_sleep)
{
 if(!available())return NULL;
 ops.lock(ops.ctx);struct pool *p=find_pool(object);
 void *cpu=p?allocate(p,p->bytes,p->align,p->boundary,handle,may_sleep):NULL;
 ops.unlock(ops.ctx);return cpu;
}
void sf_tb_pool_free(void *object,void *cpu,uint64_t iova)
{
 if(!available())return;
 ops.lock(ops.ctx);struct pool *p=find_pool(object);
 if(!p || !cpu)fault(EINVAL);else retire(p,p->bytes,cpu,iova);
 ops.unlock(ops.ctx);
}
