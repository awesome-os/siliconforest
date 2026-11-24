// SPDX-License-Identifier: MIT
// Actual installed ioctl implementation; provider fault/refcount controls only.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cerrno>
#include <sys/ioctl.h>
#include <vector>
#define O_CLOEXEC 0x1000000
struct dma_fence {int refs=1;bool done=false;};
struct file {dma_fence *fence;};
struct sync_file {file *file;};
enum dma_resv_usage {DMA_RESV_USAGE_KERNEL,DMA_RESV_USAGE_WRITE,DMA_RESV_USAGE_READ,DMA_RESV_USAGE_BOOKKEEP};
struct dma_resv {dma_fence *f=nullptr;dma_resv_usage usage=DMA_RESV_USAGE_WRITE;int locked=0;};
struct dma_buf {dma_resv *resv;};
struct dma_fence_unwrap {int index;};
enum dma_data_direction {DMA_FROM_DEVICE,DMA_TO_DEVICE,DMA_BIDIRECTIONAL};
static int from_fail,to_fail,fd_fail,single_fail,create_fail,reserve_fail;
static int reserved,installed,files,fgets_count,puts_count,last_flags,begin_calls,end_calls;
static dma_data_direction direction;
static dma_resv_usage queried,added;
static dma_fence stub;
static file *slots[32];
static int copy_from_user(void *d,void *s,size_t n){if(from_fail)return 1;memcpy(d,s,n);return 0;}
static int copy_to_user(void *d,void *s,size_t n){if(to_fail)return 1;memcpy(d,s,n);return 0;}
static dma_fence *dma_fence_get(dma_fence *f){if(f){assert(f->refs>0);++f->refs;}return f;}
static void dma_fence_put(dma_fence *f){if(f){assert(f->refs>0);--f->refs;++puts_count;}}
static dma_fence *dma_fence_get_stub(){return dma_fence_get(&stub);}
static dma_resv_usage dma_resv_usage_rw(bool write){return write?DMA_RESV_USAGE_READ:DMA_RESV_USAGE_WRITE;}
static int get_unused_fd_flags(int flags){last_flags=flags;if(fd_fail)return -EMFILE;++reserved;return 7;}
static void put_unused_fd(int fd){assert(fd==7&&reserved==1);--reserved;}
static void fd_install(int fd,file *f){assert(fd==7&&reserved==1&&!slots[fd]);--reserved;++installed;slots[fd]=f;}
static int dma_resv_get_singleton(dma_resv *r,dma_resv_usage u,dma_fence **f){queried=u;if(single_fail)return -ENOMEM;*f=(r->f&&r->usage<=u)?dma_fence_get(r->f):nullptr;return 0;}
static sync_file *sync_file_create(dma_fence *f){if(create_fail)return nullptr;auto *s=new sync_file; s->file=new file{dma_fence_get(f)};++files;return s;}
static void fput(file *f){dma_fence_put(f->fence);delete f;--files;}
// The test-only wrapper object is detached after export; production owns it as private_data.
static dma_fence *sync_file_get_fence(int fd){if(fd<0||fd>=32||!slots[fd])return nullptr;++fgets_count;return dma_fence_get(slots[fd]->fence);}
static void dma_resv_lock(dma_resv *r,void*){assert(!r->locked++);}
static void dma_resv_unlock(dma_resv *r){assert(!--r->locked);}
static int dma_resv_reserve_fences(dma_resv *r,unsigned n){assert(r->locked&&n==1);return reserve_fail?-ENOMEM:0;}
static void dma_resv_add_fence(dma_resv *r,dma_fence *f,dma_resv_usage u){assert(r->locked);dma_fence_put(r->f);r->f=dma_fence_get(f);r->usage=u;added=u;}
#define dma_fence_unwrap_for_each(f,it,root) for((it)->index=0;(it)->index<1&&!((root)->done)&&((f)=(root),true);++(it)->index)
static int dma_buf_begin_cpu_access(dma_buf*,dma_data_direction d){direction=d;++begin_calls;return 0;}
static int dma_buf_end_cpu_access(dma_buf*,dma_data_direction d){direction=d;++end_calls;return 0;}
@SOURCE@
static long call(dma_buf *b,unsigned cmd,sf_dma_buf_sync_file *a){return sf_dma_buf_sync_file_ioctl(b,cmd,(unsigned long)a);}
int main(){
 dma_resv r;dma_buf b{&r};sf_dma_buf_sync_file a{3,-1};
 assert(call(&b,123,&a)==-ENOTTY);
 for(uint32_t bad:{0u,4u,UINT32_MAX}){a={bad,-1};assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-EINVAL);assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==-EINVAL);}
 a={3,-1};from_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-EFAULT);assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==-EFAULT);from_fail=0;
 fd_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-EMFILE);fd_fail=0;
 single_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-ENOMEM&&reserved==0);single_fail=0;
 create_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-ENOMEM&&stub.refs==1&&reserved==0);create_fail=0;
 // Failure after a sync file exists must drop its fence/file and reserved descriptor.
 to_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==-EFAULT&&reserved==0&&files==0&&stub.refs==1);to_fail=0;
 assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==0&&a.fd==7&&last_flags==O_CLOEXEC&&reserved==0&&installed==1);fput(slots[7]);slots[7]=nullptr;
 dma_fence pending;file input{&pending};slots[9]=&input;a={1,9};reserve_fail=1;assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==-ENOMEM&&pending.refs==1&&!r.locked);reserve_fail=0;
 assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==0&&added==DMA_RESV_USAGE_READ&&pending.refs==2);
 a={1,-1};assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==0&&queried==DMA_RESV_USAGE_WRITE&&slots[7]->fence==&stub);fput(slots[7]);slots[7]=nullptr;
 a={2,-1};assert(call(&b,SF_DMA_BUF_IOCTL_EXPORT_SYNC_FILE,&a)==0&&queried==DMA_RESV_USAGE_READ&&slots[7]->fence==&pending);fput(slots[7]);slots[7]=nullptr;
 a={2,9};assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==0&&added==DMA_RESV_USAGE_WRITE&&pending.refs==2);
 pending.done=true;assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==0&&pending.refs==2);dma_fence_put(r.f);r.f=nullptr;slots[9]=nullptr;
 a={2,31};assert(call(&b,SF_DMA_BUF_IOCTL_IMPORT_SYNC_FILE,&a)==-EINVAL);
 for(uint64_t flags:{1u,2u,3u,5u,6u,7u}){struct sf_dma_buf_sync s{flags};assert(sf_dma_buf_sync_file_ioctl(&b,SF_DMA_BUF_IOCTL_SYNC,(unsigned long)&s)==0);}
 assert(begin_calls==3&&end_calls==3&&direction==DMA_BIDIRECTIONAL);
 for(uint64_t flags:{0u,4u,8u}){struct sf_dma_buf_sync s{flags};assert(sf_dma_buf_sync_file_ioctl(&b,SF_DMA_BUF_IOCTL_SYNC,(unsigned long)&s)==-EINVAL);}
 assert(!reserved&&!files&&pending.refs==1&&stub.refs==1&&!r.locked);puts("PASS installed actual dma-buf ioctl validation, READ/WRITE filtering, stub only for empty reservation, EFAULT/ENOMEM/EMFILE cleanup, pending refs, CPU flags; fake providers, no GPU/poll proof");
}
