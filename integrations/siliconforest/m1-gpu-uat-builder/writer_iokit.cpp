/* Original mutable bridge for owned PRIVATE RAM. No live UAT publication. */
#include "writer_internal.h"
#include "../m1-gpu-uat-private/provider_iokit.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <libkern/libkern.h>

enum uatb_code uatb_iokit_init(uatb_context *context) {
    const duat_ops ops = duat_iokit_ops();
    return uatb_init(context, &ops, nullptr, uatb_iokit_write, nullptr);
}

enum uatb_write_result uatb_iokit_write(void *, const duat_context *owner,
    duat_token token, size_t offset, const uint8_t *source, size_t length) {
    duat_view view; void *handle = nullptr;
    const uatb_span_result span = uatb_write_view(owner, token, offset, source, length, &view, &handle);
    if (span == UATB_SPAN_BAD) return UATB_FAILED_PRIVATE;
    if (span != UATB_SPAN_READY) return UATB_UNCERTAIN;
    auto *descriptor = static_cast<IOBufferMemoryDescriptor *>(handle);
    void *bytes = descriptor->getBytesNoCopy();
    if (bytes != view.bytes || descriptor->getLength() != DUAT_BYTES ||
        descriptor->getCapacity() != DUAT_BYTES) return UATB_UNCERTAIN;
    /* Explicitly admitted write to an exclusively owned, wired, private page.
     * A CPU fault is not recoverable by this function; no fault recovery claim. */
    memcpy(static_cast<uint8_t *>(bytes) + offset, source, length);
    return UATB_WRITTEN;
}
