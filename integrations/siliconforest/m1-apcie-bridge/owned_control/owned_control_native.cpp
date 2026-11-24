/* Original read-only CPU translation adapter; no module initializer. */
#include "owned_pmap_bridge.h"
#include "owned_control.h"
#include <IOKit/IOLocks.h>
#include <mach/vm_param.h>
static_assert(sizeof(void*)==8 && sizeof(uintptr_t)==8,"exact ARM64 pointers");
static_assert(sizeof(lck_mtx_t)==16 && alignof(lck_mtx_t)==8,"exact pinned private mutex type");
static_assert(sizeof(ppnum_t)==4 && sizeof(addr64_t)==8,"exact pinned physical API");
static_assert(__is_same(decltype(&IOLockGetMachLock),lck_mtx_t*(*)(IOLock*)),"exact getter signature");
static_assert(__is_same(decltype(&pmap_find_phys),ppnum_t(*)(pmap_t,addr64_t)),"exact pmap signature");
static bool pin_attempted;
static int pin(void *,bno_native_setup *setup)
{
    if(pin_attempted)return BOC_NATIVE;
    pin_attempted=true;
    return bno_native_pin_empty(setup);
}
static int observe(void *,uint64_t opaque,boc_source *out)
{
    if(!opaque || !kernel_pmap)return BOC_PROFILE;
    lck_mtx_t *mutex=IOLockGetMachLock(reinterpret_cast<IOLock *>(static_cast<uintptr_t>(opaque)));
    if(!mutex || reinterpret_cast<uintptr_t>(mutex)!=opaque)return BOC_PROFILE;
    *out={reinterpret_cast<uintptr_t>(kernel_pmap),reinterpret_cast<uintptr_t>(mutex),
        static_cast<uint32_t>(PAGE_SHIFT),static_cast<uint32_t>(page_size),
        static_cast<uint32_t>(PAGE_SIZE),sizeof(lck_mtx_t),alignof(lck_mtx_t)};
    return BOC_OK;
}
static int lookup(void *,uint64_t identity,uint64_t va,uint32_t *out)
{
    if(!out || !kernel_pmap || reinterpret_cast<uintptr_t>(kernel_pmap)!=identity ||
        PAGE_SHIFT!=14 || page_size!=BPL_PAGE || PAGE_SIZE!=BPL_PAGE || (va&(BPL_PAGE-1)))return BOC_PROFILE;
    ppnum_t ppn=pmap_find_phys(kernel_pmap,static_cast<addr64_t>(va));
    if(reinterpret_cast<uintptr_t>(kernel_pmap)!=identity || PAGE_SHIFT!=14 || page_size!=BPL_PAGE || PAGE_SIZE!=BPL_PAGE)return BOC_CHANGED;
    *out=ppn;return BOC_OK;
}
static int configure(void *,const bno_native_setup *s,const bpl_policy *p,const bno_protection *protection)
{return bno_native_configure(s,p,protection);}
extern "C" int boc_native_derive_and_configure(const bpl_policy *p,boc_workspace *w,boc_receipt *out)
{
    const boc_boot_ops ops={{nullptr,observe,lookup},pin,configure};
    return boc_boot_configure(p,&ops,w,out);
}
