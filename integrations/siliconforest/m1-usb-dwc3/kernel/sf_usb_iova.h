/* SPDX-License-Identifier: MIT */
#ifndef SF_USB_IOVA_H
#define SF_USB_IOVA_H
#include <stdint.h>
#include <stddef.h>
/* Search only; caller serializes allocation and commits after table mapping.
 * IOVA alignment is independent of physical alignment (xHCI 64KiB TRBs).
 * SIZE_MAX means no aligned free extent. */
static inline size_t sf_usb_iova_find(const uint8_t *busy, size_t pages,
                                     uint64_t base, size_t page,
                                     size_t count, size_t alignment)
{
    if (!busy || !page || (page & (page - 1)) || !count || count > pages ||
        alignment < page || (alignment & (alignment - 1)) || (base & (page - 1)) ||
        pages > (UINT64_MAX - base) / page) return SIZE_MAX;
    size_t run = 0;
    for (size_t i = 0; i < pages; ++i) {
        if (busy[i]) { run = 0; continue; }
        if (!run && ((base + i * page) & (alignment - 1))) continue;
        if (++run == count) return i + 1 - count;
    }
    return SIZE_MAX;
}
#endif
