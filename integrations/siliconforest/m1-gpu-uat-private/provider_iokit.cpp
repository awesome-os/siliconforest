/* Original private allocation adapter; no table writes or GPU operations. */
#include "provider_iokit.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/task.h>
#include <mach/vm_param.h>

#if IOKIT_ENABLE_SHARED_PTR
#error This reviewed adapter requires the pinned default raw OSPtr ownership ABI
#endif
namespace {
void *allocate(void *) {
    if (page_size != DUAT_BYTES) return nullptr;
    return IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMemoryMapperNone,
        DUAT_BYTES, UINT64_C(0x0000000fffffc000));
}
int prepare(void *, void *p) {
    return static_cast<IOBufferMemoryDescriptor *>(p)->prepare(kIODirectionInOut)
        == kIOReturnSuccess ? 0 : -1;
}
int inspect(void *, void *p, duat_view *v) {
    auto *d = static_cast<IOBufferMemoryDescriptor *>(p);
    IOByteCount segment = 0;
    const auto pa = d->getPhysicalSegment(0, &segment, kIOMemoryMapperNone);
    *v = duat_view{static_cast<const uint8_t *>(d->getBytesNoCopy()),
        d->getLength(), d->getCapacity(), page_size, pa, segment};
    return 0;
}
int complete(void *, void *p) {
    return static_cast<IOBufferMemoryDescriptor *>(p)->complete(kIODirectionInOut)
        == kIOReturnSuccess ? 0 : -1;
}
void release(void *, void *p) {
    static_cast<IOBufferMemoryDescriptor *>(p)->release();
}
}
duat_ops duat_iokit_ops(void) {
    return duat_ops{allocate, prepare, inspect, complete, release};
}
