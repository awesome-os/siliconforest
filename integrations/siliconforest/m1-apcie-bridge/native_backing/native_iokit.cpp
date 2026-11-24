/* Original pinned-API adapter. Compile only; no start method or boot packet. */
#ifndef IOKIT_ENABLE_SHARED_PTR
#define IOKIT_ENABLE_SHARED_PTR 1
#endif
#ifndef IOKIT_ENABLE_EXPERIMENTAL_SHARED_PTR_IN_API
#define IOKIT_ENABLE_EXPERIMENTAL_SHARED_PTR_IN_API 1
#endif
#if __cplusplus < 201703L
#error "native backing requires explicit C++17 shared OSPtr API"
#endif
#include "native_iokit.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <mach/task.h>
#include <mach/vm_param.h>
#include <libkern/libkern.h>
extern task_t kernel_task;
static_assert(__is_same(decltype(IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
    nullptr,0,0,0)),OSSharedPtr<IOBufferMemoryDescriptor>),"actual shared factory return required");

class NativeAnchor final {
public:
    bpn_manager manager{};
    IOLock *mutex=nullptr;
    OSPtr<IOBufferMemoryDescriptor> refs[BPN_JOBS][BPN_ROLES];
    static void *operator new(size_t,void *memory) noexcept { return memory; }
    NativeAnchor()=default;
    NativeAnchor(const NativeAnchor &)=delete;
    NativeAnchor &operator=(const NativeAnchor &)=delete;
private:
    ~NativeAnchor()=delete; /* No facade/stack/member destructor can release refs. */
};
static NativeAnchor *anchor; /* Deliberately retained for the boot lifetime. */
static bool creation_attempted;
static bool configured;
static bpn_native_setup setup_record;
static void take_lock(void *ctx) { IOLockLock(static_cast<NativeAnchor *>(ctx)->mutex); }
static void drop_lock(void *ctx) { IOLockUnlock(static_cast<NativeAnchor *>(ctx)->mutex); }
static void native_call(void *ctx,const bpn_call *c,bpn_return *out)
{
    auto *a=static_cast<NativeAnchor *>(ctx);
    *out={c->sequence,c->generation,0,c->slot,c->role,c->action,BPN_CALL_ERROR,-1,{}};
    if(c->slot>=BPN_JOBS || c->role>=BPN_ROLES)return;
    auto &ref=a->refs[c->slot][c->role];
    if(c->action==BPN_ALLOCATE) {
        if(ref || c->bytes!=BPL_PAGE || PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return;
        /* Factory result moves directly into permanent anchor, never a local
         * sole owner across prepare/query/failure return. Constructor-internal
         * AutoPrepare remains XNU's responsibility, not our explicit pair. */
        ref=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,
            BPL_PAGE,BPN_MASK);
        out->identity=reinterpret_cast<uintptr_t>(ref.get());
        out->outcome=ref?BPN_CALL_OK:BPN_CALL_REFUSED;out->native_error=0;return;
    }
    out->identity=reinterpret_cast<uintptr_t>(ref.get());
    if(!ref || out->identity!=c->identity)return;
    switch(c->action) {
    case BPN_PREPARE: {
        for(unsigned i=0;i<BPN_ROLES;i++)if(!a->refs[c->slot][i])return;
        IOReturn result=ref->prepare(kIODirectionOutIn);
        out->native_error=static_cast<int32_t>(result);
        out->outcome=result==kIOReturnSuccess?BPN_CALL_OK:BPN_CALL_ERROR;return;
    }
    case BPN_QUERY: {
        IOByteCount span=0;
        addr64_t pa=ref->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
        out->view={reinterpret_cast<uintptr_t>(ref->getBytesNoCopy()),pa,span,
            ref->getLength(),ref->getCapacity()};
        out->outcome=BPN_CALL_OK;out->native_error=0;return;
    }
    case BPN_ZERO:
        if(c->bytes!=BPL_PAGE || c->offset ||
            reinterpret_cast<uintptr_t>(ref->getBytesNoCopy())!=c->expected.cpu)return;
        bzero(ref->getBytesNoCopy(),BPL_PAGE);
        break;
    case BPN_COPY_DATA:
        if(c->role!=2 || !c->source || !c->bytes || c->offset>BPL_PAGE || c->bytes>BPL_PAGE-c->offset ||
            reinterpret_cast<uintptr_t>(ref->getBytesNoCopy())!=c->expected.cpu)return;
        memcpy(static_cast<uint8_t *>(ref->getBytesNoCopy())+c->offset,c->source,c->bytes);
        break;
    case BPN_COMPLETE: {
        IOReturn result=ref->complete(kIODirectionOutIn);
        out->native_error=static_cast<int32_t>(result);
        out->outcome=result==kIOReturnSuccess?BPN_CALL_OK:BPN_CALL_ERROR;return;
    }
    case BPN_RESET_REF:
        ref.reset(); /* Only called AFTER all owned completes and core retirement. */
        out->identity=reinterpret_cast<uintptr_t>(ref.get());
        break;
    default:return;
    }
    out->outcome=BPN_CALL_OK;out->native_error=0;
}
extern "C" int bpn_native_pin_empty(bpn_native_setup *out)
{
    if(!out)return BPN_ARGUMENT;
    if(creation_attempted)return BPN_ARGUMENT;
    creation_attempted=true;
    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BPN_PAGE;
    void *memory=IOMallocZero(sizeof(NativeAnchor));
    if(!memory)return BPN_REFUSED;
    anchor=new(memory) NativeAnchor;
    anchor->mutex=IOLockAlloc();
    if(!anchor->mutex)return BPN_REFUSED; /* permanent, empty anchor retained */
    setup_record={reinterpret_cast<uintptr_t>(anchor),1,
        {reinterpret_cast<uintptr_t>(anchor),sizeof *anchor},reinterpret_cast<uintptr_t>(anchor->mutex)};
    *out=setup_record;
    return BPN_OK;
}
extern "C" int bpn_native_configure(const bpn_native_setup *setup,const bpl_policy *policy,
                                    const bpn_protection *protection)
{
    if(!anchor || !anchor->mutex || configured || !setup || !policy || !protection ||
        setup->identity!=setup_record.identity || setup->generation!=setup_record.generation ||
        setup->typed_anchor_cpu.base!=setup_record.typed_anchor_cpu.base ||
        setup->typed_anchor_cpu.bytes!=setup_record.typed_anchor_cpu.bytes ||
        setup->opaque_mutex_identity!=setup_record.opaque_mutex_identity)return BPN_ARGUMENT;
    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BPN_PAGE;
    bpn_ops ops={anchor,take_lock,drop_lock,native_call};
    int rc=bpn_init(&anchor->manager,&ops,policy,protection,PAGE_SIZE,
        {reinterpret_cast<uintptr_t>(anchor),sizeof *anchor});
    if(!rc)configured=true;
    return rc;
}
extern "C" int bpn_native_acquire(const bpl_request *r,uint64_t i,uint32_t p,bpn_token *t)
{ return configured?bpn_acquire(&anchor->manager,r,i,p,t):BPN_ARGUMENT; }
extern "C" int bpn_native_zero(const bpn_token *t)
{ return configured?bpn_zero_private(&anchor->manager,t):BPN_ARGUMENT; }
extern "C" int bpn_native_copy(const bpn_token *t,uint64_t o,const void *s,size_t n)
{ return configured?bpn_copy_private_data(&anchor->manager,t,o,s,n):BPN_ARGUMENT; }
extern "C" int bpn_native_retire(const bpn_token *t)
{ return configured?bpn_retire_private(&anchor->manager,t):BPN_ARGUMENT; }
extern "C" int bpn_native_inspect(uint32_t slot,bpn_snapshot *s)
{ return configured?bpn_inspect(&anchor->manager,slot,s):BPN_ARGUMENT; }
