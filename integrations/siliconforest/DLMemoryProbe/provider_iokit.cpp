/* Original Darwin Lab code. License selection pending.
 * CANDIDATE XNU code: reviewed against pinned declarations, not kernel-built.
 * No DMA, IOMMU programming or device access. The bridge rejects initial
 * executable mapping requests; later mprotect policy remains unverified.
 */
#include "provider_iokit.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLib.h>
#include <kern/task.h>
#include <mach/vm_param.h>
#include <libkern/libkern.h>

namespace {
struct Allocation {
    IOBufferMemoryDescriptor *descriptor;
    uint64_t bytes;
    bool exported;
    bool completion_failed;
};
Allocation slots[DL_BUFFER_CAPACITY] = {};

void *allocate(void *, uint64_t bytes, uint64_t alignment) {
    if (!bytes || bytes > DLMP_MAX_ALLOCATION || alignment != PAGE_SIZE ||
        bytes % PAGE_SIZE) return nullptr;
    Allocation *slot = nullptr;
    for (auto &candidate : slots)
        if (!candidate.descriptor) { slot = &candidate; break; }
    if (!slot) return nullptr;
    // Kernel allocations are wired by default. No pageable/purgeable option.
    // The pinned default OSPtr API returns one caller-owned raw reference.
    IOBufferMemoryDescriptor *descriptor = IOBufferMemoryDescriptor::inTaskWithOptions(
        kernel_task, kIODirectionInOut | kIOMemoryKernelUserShared | kIOMemoryMapperNone,
        static_cast<vm_size_t>(bytes), static_cast<vm_offset_t>(alignment));
    if (!descriptor) return nullptr;
    if (descriptor->getLength() < bytes || !descriptor->getBytesNoCopy()) {
        descriptor->release();
        return nullptr;
    }
    if (descriptor->prepare(kIODirectionInOut) != kIOReturnSuccess) {
        descriptor->release();
        return nullptr;
    }
    // No fallible operations after successful prepare. Zero ALL rounded bytes.
    bzero(descriptor->getBytesNoCopy(), static_cast<size_t>(bytes));
    // Transfer exactly the factory reference to the persistent allocation slot.
    *slot = Allocation{descriptor, bytes, false, false};
    return slot;
}

bool release(void *, void *cookie, uint64_t bytes) {
    auto *slot = static_cast<Allocation *>(cookie);
    if (!slot || !slot->descriptor || bytes != slot->bytes) return false;
    // The pinned pager callback precedes memory_object_destroy. Until final VM
    // teardown is proven, NEVER recycle backing that has reached that pager.
    // No runtime flag or ioctl can bypass this conservative restriction.
    if (slot->exported || slot->completion_failed) return false;
    if (slot->descriptor->complete(kIODirectionInOut) != kIOReturnSuccess) {
        // Do not repeat complete on a descriptor with uncertain partial state.
        slot->completion_failed = true;
        return false;
    }
    slot->descriptor->release();
    *slot = Allocation{};
    return true;
}

int pages(void *, void *cookie, uint64_t offset, uint64_t bytes,
          uint64_t page_size, uint32_t *out, size_t count) {
    auto *slot = static_cast<Allocation *>(cookie);
    if (!slot || !slot->descriptor || !out || page_size != PAGE_SIZE ||
        offset > slot->bytes || bytes > slot->bytes - offset ||
        offset % page_size || bytes % page_size || count != bytes / page_size)
        return DL_INVALID;
    for (size_t i = 0; i < count; ++i) {
        IOByteCount contiguous = 0;
        // Explicitly request HOST physical addresses, not mapper/I/O addresses.
        auto physical = slot->descriptor->getPhysicalSegment(
            static_cast<IOByteCount>(offset + i * page_size), &contiguous,
            kIOMemoryMapperNone);
        if (contiguous < page_size || physical % page_size ||
            physical / page_size > UINT32_MAX) return DL_INVALID;
        out[i] = static_cast<uint32_t>(physical / page_size);
    }
    return DL_OK;
}
void exported(void *, void *cookie) {
    static_cast<Allocation *>(cookie)->exported = true;
}
}

dl_probe_ops dl_iokit_probe_ops(void) {
    // Deliberately NO DLMP_CAP_MAPPED_RECLAIM.
    return dl_probe_ops{{allocate, release}, pages, exported,
                        DLMP_CAP_RAM_ONLY | DLMP_CAP_KERNEL_BACKEND};
}
