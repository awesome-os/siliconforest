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
#include "native_owned.h"
#include "owned.h"
#if defined(BNO_TEST_DISPATCH) && defined(KERNEL)
#error "TEST dispatcher cannot be compiled with KERNEL"
#endif
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
    bno_manager manager{};
    IOLock *mutex=nullptr;
    OSPtr<IOBufferMemoryDescriptor> refs[BNO_JOBS][BNO_ROLES];
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
static bno_native_setup setup_record;
static void take_lock(void *ctx) { IOLockLock(static_cast<NativeAnchor *>(ctx)->mutex); }
static void drop_lock(void *ctx) { IOLockUnlock(static_cast<NativeAnchor *>(ctx)->mutex); }
static void native_call(void *ctx,const bno_call *c,bno_return *out)
{
    auto *a=static_cast<NativeAnchor *>(ctx);
    *out={c->sequence,c->generation,0,c->slot,c->role,c->action,BNO_CALL_ERROR,-1,{}};
    if(c->slot>=BNO_JOBS || c->role>=BNO_ROLES)return;
    auto &ref=a->refs[c->slot][c->role];
    if(c->action==BNO_ALLOCATE) {
        if(ref || c->bytes!=BPL_PAGE || PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return;
        /* Factory result moves directly into permanent anchor, never a local
         * sole owner across prepare/query/failure return. Constructor-internal
         * AutoPrepare remains XNU's responsibility, not our explicit pair. */
        ref=IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
            kIODirectionOutIn | kIOMemoryMapperNone | kIOMemoryHostPhysicallyContiguous,
            BPL_PAGE,BNO_MASK);
        out->identity=reinterpret_cast<uintptr_t>(ref.get());
        out->outcome=ref?BNO_CALL_OK:BNO_CALL_REFUSED;out->native_error=0;return;
    }
    out->identity=reinterpret_cast<uintptr_t>(ref.get());
    if(!ref || out->identity!=c->identity)return;
    switch(c->action) {
    case BNO_PREPARE: {
        for(unsigned i=0;i<BNO_ROLES;i++)if(!a->refs[c->slot][i])return;
        IOReturn result=ref->prepare(kIODirectionOutIn);
        out->native_error=static_cast<int32_t>(result);
        out->outcome=result==kIOReturnSuccess?BNO_CALL_OK:BNO_CALL_ERROR;return;
    }
    case BNO_QUERY: {
        IOByteCount span=0;
        addr64_t pa=ref->getPhysicalSegment(0,&span,kIOMemoryMapperNone);
        out->view={reinterpret_cast<uintptr_t>(ref->getBytesNoCopy()),pa,span,
            ref->getLength(),ref->getCapacity()};
        out->outcome=BNO_CALL_OK;out->native_error=0;return;
    }
    case BNO_ZERO:
        if(c->bytes!=BPL_PAGE || c->offset ||
            reinterpret_cast<uintptr_t>(ref->getBytesNoCopy())!=c->expected.cpu)return;
        bzero(ref->getBytesNoCopy(),BPL_PAGE);
        break;
    case BNO_COPY_DATA:
        if(c->role!=2 || !c->source || !c->bytes || c->offset>BPL_PAGE || c->bytes>BPL_PAGE-c->offset ||
            reinterpret_cast<uintptr_t>(ref->getBytesNoCopy())!=c->expected.cpu)return;
        memcpy(static_cast<uint8_t *>(ref->getBytesNoCopy())+c->offset,c->source,c->bytes);
        break;
    case BNO_COMPLETE: {
        IOReturn result=ref->complete(kIODirectionOutIn);
        out->native_error=static_cast<int32_t>(result);
        out->outcome=result==kIOReturnSuccess?BNO_CALL_OK:BNO_CALL_ERROR;return;
    }
    case BNO_RESET_REF:
        ref.reset(); /* Only called AFTER all owned completes and core retirement. */
        out->identity=reinterpret_cast<uintptr_t>(ref.get());
        break;
    default:return;
    }
    out->outcome=BNO_CALL_OK;out->native_error=0;
}
#ifdef BNO_TEST_DISPATCH
#ifndef BNO_TEST_SDK
#error "TEST dispatcher requires original TEST SDK stand-ins"
#endif
/* HOST ONLY ordinary memory/trace dispatcher, not an endpoint/backend. */
extern int bno_test_effect_observe(const bno_effect_call *);
static void test_dispatch(void *ctx,const bno_effect_call *c,bno_effect_return *out)
{
    auto *a=static_cast<NativeAnchor *>(ctx);
    *out={c->sequence,c->generation,c->slot,c->operation,c->ticket.sequence,
        BPL_REPORTED_FAILURE,-810,{0,0,0}};
    if(c->slot>=BNO_JOBS)return;
    for(unsigned i=0;i<BNO_ROLES;i++) {
        auto &ref=a->refs[c->slot][i];
        if(!ref || reinterpret_cast<uintptr_t>(ref.get())!=c->identities[i] ||
            reinterpret_cast<uintptr_t>(ref->getBytesNoCopy())!=c->views[i].cpu)return;
        out->identities[i]=reinterpret_cast<uintptr_t>(ref.get());
    }
    const int fault=bno_test_effect_observe(c);
    if(fault==1)return; /* before ordinary-memory application */
    if(c->operation==BNO_OP_DESCRIPTOR || c->operation==BNO_OP_SUBMIT) {
        auto *bytes=static_cast<uint8_t *>(a->refs[c->slot][2]->getBytesNoCopy());
        for(uint64_t i=0;i<c->request.length;i++)bytes[c->request.offset+i]=
            static_cast<uint8_t>(c->request.words[0]);
    }
    out->outcome=BPL_REPORTED_SUCCESS;out->error=0;
    if(fault==2){out->outcome=BPL_REPORTED_FAILURE;out->error=-811;}
    if(fault==3){out->outcome=BPL_UNKNOWN;out->error=-812;}
    if(fault==4)out->sequence++;
    if(fault==5)out->identities[2]++;
    if(fault==6)out->operation++;
}
#endif
extern "C" int bno_native_pin_empty(bno_native_setup *out)
{
    if(!out)return BNO_ARGUMENT;
    if(creation_attempted)return BNO_ARGUMENT;
    creation_attempted=true;
    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BNO_PAGE;
    void *memory=IOMallocZero(sizeof(NativeAnchor));
    if(!memory)return BNO_REFUSED;
    anchor=new(memory) NativeAnchor;
    anchor->mutex=IOLockAlloc();
    if(!anchor->mutex)return BNO_REFUSED; /* permanent, empty anchor retained */
    setup_record={reinterpret_cast<uintptr_t>(anchor),1,
        {reinterpret_cast<uintptr_t>(anchor),sizeof *anchor},reinterpret_cast<uintptr_t>(anchor->mutex)};
    *out=setup_record;
    return BNO_OK;
}
extern "C" int bno_native_configure(const bno_native_setup *setup,const bpl_policy *policy,
                                    const bno_protection *protection)
{
    if(!anchor || !anchor->mutex || configured || !setup || !policy || !protection ||
        setup->identity!=setup_record.identity || setup->generation!=setup_record.generation ||
        setup->typed_anchor_cpu.base!=setup_record.typed_anchor_cpu.base ||
        setup->typed_anchor_cpu.bytes!=setup_record.typed_anchor_cpu.bytes ||
        setup->opaque_mutex_identity!=setup_record.opaque_mutex_identity)return BNO_ARGUMENT;
    if(PAGE_SIZE!=BPL_PAGE || page_size!=BPL_PAGE)return BNO_PAGE;
    bno_ops ops={anchor,take_lock,drop_lock,native_call,nullptr,0};
#ifdef BNO_TEST_DISPATCH
    ops.dispatch=test_dispatch;ops.effect_backend_available=1;
#endif
    int rc=bno_init(&anchor->manager,&ops,policy,protection,PAGE_SIZE,
        {reinterpret_cast<uintptr_t>(anchor),sizeof *anchor});
    if(!rc)configured=true;
    return rc;
}
extern "C" int bno_native_acquire(const bno_request *r,uint64_t i,uint32_t p,bno_token *t)
{ return configured?bno_acquire(&anchor->manager,r,i,p,t):BNO_ARGUMENT; }
extern "C" int bno_native_zero(const bno_token *t)
{ return configured?bno_zero_private(&anchor->manager,t):BNO_ARGUMENT; }
extern "C" int bno_native_copy(const bno_token *t,uint64_t o,const void *s,size_t n)
{ return configured?bno_copy_private_data(&anchor->manager,t,o,s,n):BNO_ARGUMENT; }
extern "C" int bno_native_retire(const bno_token *t)
{ return configured?bno_retire_private(&anchor->manager,t):BNO_ARGUMENT; }
extern "C" int bno_native_inspect(uint32_t slot,bno_snapshot *s)
{ return configured?bno_inspect(&anchor->manager,slot,s):BNO_ARGUMENT; }

extern "C" int bno_native_run(const bno_token *t,const bno_effect_request *q,bno_effect_result *r)
{ return configured?bno_run(&anchor->manager,t,q,r):BNO_ARGUMENT; }
