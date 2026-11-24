/* Original fixed-path genuine IOKit reader; no object/API execution in builds. */
#if !defined(KERNEL) || !defined(__aarch64__) || !defined(__APPLE__)
#error genuine ARM64 Darwin kernel compilation required; no host success stub
#endif
#define IOKIT_ENABLE_SHARED_PTR 1
#define IOKIT_ENABLE_EXPERIMENTAL_SHARED_PTR_IN_API 1
#include <pexpert/pexpert.h>
#include <pexpert/arm64/boot.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <libkern/c++/OSData.h>
#include "policy_reader_internal.h"
static_assert(__cplusplus>=201703L,"genuine shared-pointer API requires C++17");
static_assert(offsetof(boot_args,physBase)==16 && offsetof(boot_args,memSize)==24 &&
              offsetof(boot_args,topOfKernelData)==32,"source-proven common prefix");
static_assert(NPR_COMMON_PREFIX_BYTES==40,"never read Video or tail/full boot_args");
namespace {
struct NativeContext {
    OSSharedPtr<IORegistryEntry> chosen;
    OSSharedPtr<OSObject> property[3];
};
int begin(void *opaque)
{
    auto *c=static_cast<NativeContext *>(opaque);
    c->chosen=IORegistryEntry::fromPath("/chosen",gIODTPlane);
    if(!c->chosen)return NPR_NOT_READY;
    c->property[0]=c->chosen->copyProperty("dram-base");
    c->property[1]=c->chosen->copyProperty("dram-size");
    c->property[2]=c->chosen->copyProperty("darwin-lab-platform-facts");
    for(unsigned i=0;i<3;i++) {
        if(!c->property[i])return NPR_NOT_READY;
        if(!OSDynamicCast(OSData,c->property[i].get()))return NPR_FORMAT;
    }
    return NPR_OK;
}
int boot(void *,struct npr_boot_view *out)
{
    /* Accessibility/quiescence is an explicit external precondition, not
     * established by this pointer test or PE_state.initialized. */
    const auto *p=static_cast<const uint8_t *>(PE_state.bootArgs);
    *out={p,NPR_COMMON_PREFIX_BYTES,reinterpret_cast<uintptr_t>(p),PE_state.initialized?1u:0u};
    return NPR_OK;
}
int view(void *opaque,enum npr_source_id source,struct npr_view *out)
{
    auto *c=static_cast<NativeContext *>(opaque);
    if(static_cast<unsigned>(source)>=3)return NPR_ARGUMENT;
    auto *data=OSDynamicCast(OSData,c->property[source].get());
    if(!data)return NPR_FORMAT;
    unsigned length=data->getLength();
    if(source<NPR_FACTS_DATA ? length!=8 : (length<NPS_HEADER_BYTES || length>NPS_WIRE_MAX))return NPR_FORMAT;
    *out={static_cast<const uint8_t *>(data->getBytesNoCopy(0,length)),length,reinterpret_cast<uintptr_t>(data)};
    return NPR_OK;
}
int finish(void *opaque)
{
    auto *c=static_cast<NativeContext *>(opaque);
    for(unsigned i=3;i;i--)c->property[i-1].reset();
    c->chosen.reset();return NPR_OK;
}
}
extern "C" int npr_read_native(const struct nps_expected *expected,
                               struct npr_workspace *workspace,struct nps_result *out)
{
    NativeContext context;
    const struct npr_ops ops={&context,begin,boot,view,finish};
    return npr_read_with_ops(&ops,expected,workspace,out);
}
