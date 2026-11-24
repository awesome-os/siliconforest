/* SPDX-License-Identifier: GPL-2.0 */
/* USB1 DMA domain. Register sequence: Linux apple-dart.c, io-pgtable-dart.c.
 * Copyright (C) 2021 The Asahi Linux Contributors
 * apple-dart.c based on ARM SMMU: Copyright (C) 2013, 2015 ARM Limited;
 * exynos-iommu: Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 * io-pgtable-dart.c based on io-pgtable-arm: Copyright (C) 2014 ARM Limited.
 */
#ifndef SF_USB_DART_H
#define SF_USB_DART_H
#include "sf_soc.h"
#include "sf_dart_table.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>

/* A single IOVA allocator and table root shared by both USB DART paths:
 * reg 0/stream 0 and reg 1/stream 1. These are not the Wi-Fi DART.
 * The caller holds the USB controller in reset for prepare(), and halts
 * the controller before releasing any mapping that hardware could use.
 * Thread context only; no unload while a controller or mapping exists. */
class SFUSBDart {
    SFMmio regs[2];
    IOLock *lock;
    struct Table { IOBufferMemoryDescriptor *md; uint64_t pa; } tables[16];
    unsigned ntables;
    struct sf_dart_tables pt;
    uint8_t *busy;
    bool ready, poisoned;
    static uint64_t *table(void *ctx, uint64_t pa);
    static uint64_t allocTable(void *ctx);
    int invalidate();
public:
    SFUSBDart();
    int prepare();
    int map(uint64_t pa, size_t bytes, uint64_t *dva, size_t alignment = SF_DART_PAGE);
    int unmap(uint64_t dva, size_t bytes);
    uint32_t faults();
    bool healthy(); // after producer drain: reject a poisoned prior domain
};

/* Page-owned bounce/ring memory: physical pages are distinct from the DMA
 * address. xHCI must use dmaAddress(), never getPhysicalAddress(), for these.
 * All USB I/O initially bounces through this type, including MSC payloads. */
class SFUSBDMABuffer : public IOBufferMemoryDescriptor {
    OSDeclareDefaultStructors(SFUSBDMABuffer)
    SFUSBDart *dart;
    uint64_t dva;
    size_t span;
public:
    static SFUSBDMABuffer *allocate(SFUSBDart *dart, size_t size, size_t alignment = SF_DART_PAGE);
    uint64_t dmaAddress() const { return dva; }
    void free() override;
};
#endif
