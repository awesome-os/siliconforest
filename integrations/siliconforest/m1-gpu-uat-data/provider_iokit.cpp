/* Independent DATA allocation provider. Never casts/reuses table tokens. */
#include "data.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <kern/task.h>
#include <mach/vm_param.h>
#include <libkern/libkern.h>
#if IOKIT_ENABLE_SHARED_PTR
#error This reviewed provider requires the pinned raw OSPtr ownership ABI
#endif
namespace {
void *allocate(void *) {
    if (page_size != DUAT_BYTES) return nullptr;
    return IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
        kIODirectionInOut | kIOMemoryPhysicallyContiguous | kIOMemoryMapperNone,
        DUAT_BYTES, UINT64_C(0x0000000fffffc000));
}
int prepare(void *, void *h) {
    return static_cast<IOBufferMemoryDescriptor *>(h)->prepare(kIODirectionInOut) == kIOReturnSuccess ? 0 : -1;
}
int inspect(void *, void *h, udata_view *out) {
    auto *d = static_cast<IOBufferMemoryDescriptor *>(h); IOByteCount length = 0;
    const auto pa = d->getPhysicalSegment(0, &length, kIOMemoryMapperNone);
    *out = udata_view{static_cast<const uint8_t *>(d->getBytesNoCopy()), d->getLength(),
        d->getCapacity(), page_size, pa, length}; return 0;
}
udata_zero_result zero(void *, void *h, const udata_view *expected) {
    auto *d = static_cast<IOBufferMemoryDescriptor *>(h);
    void *bytes = d->getBytesNoCopy();
    udata_view now{};
    if (inspect(nullptr, h, &now) || bytes != expected->bytes ||
        now.bytes != expected->bytes || now.physical != expected->physical ||
        now.length != expected->length || now.capacity != expected->capacity ||
        now.segment != expected->segment || now.page_size != expected->page_size ||
        d->getLength() != DUAT_BYTES || d->getCapacity() != DUAT_BYTES)
        return UD_ZERO_UNCERTAIN;
    bzero(bytes, DUAT_BYTES); return UD_ZEROED;
}
int complete(void *, void *h) {
    return static_cast<IOBufferMemoryDescriptor *>(h)->complete(kIODirectionInOut) == kIOReturnSuccess ? 0 : -1;
}
void release(void *, void *h) { static_cast<IOBufferMemoryDescriptor *>(h)->release(); }
}
udata_code udata_iokit_init(udata_context *c, uatb_context *g, uatb_ticket ticket) {
    const udata_ops ops = {allocate, prepare, inspect, zero, complete, release};
    return udata_init(c, &ops, nullptr, g, ticket);
}
