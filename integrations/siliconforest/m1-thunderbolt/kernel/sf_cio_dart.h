/* SPDX-License-Identifier: GPL-2.0 */
/* ACIO1 NHI DMA domain. Register sequence: Linux apple-dart.c, io-pgtable-dart.c.
 * Copyright (C) 2021 The Asahi Linux Contributors
 * apple-dart.c based on ARM SMMU: Copyright (C) 2013, 2015 ARM Limited;
 * exynos-iommu: Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 * io-pgtable-dart.c based on io-pgtable-arm: Copyright (C) 2014 ARM Limited.
 */
#ifndef SF_CIO_DART_H
#define SF_CIO_DART_H
#include "sf_soc.h"
#include "sf_dart_table.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>

/* ACIO1 NHI DART ownership: /arm-io/dart-acio1, DMA mapped only on SID1.
 * Full-bank reset disables all unused streams before attaching SID1. This
 * is neither USB1's paired mapper nor dart-apciec1 for downstream PCI functions.
 * The ACIO owner must disable/stop all NHI rings and join workers before
 * prepare()/suspend(). Tables and quarantined backing live until reboot. */
class SFCIODart {
    SFMmio regs[1];
    IOLock *lock;
    struct Table { IOBufferMemoryDescriptor *md; uint64_t pa; } tables[16];
    unsigned ntables;
    struct sf_dart_tables pt;
    uint8_t *busy;
    bool ready, poisoned;
    static uint64_t *table(void *ctx, uint64_t pa);
    static uint64_t allocTable(void *ctx);
    int invalidate(uint32_t streams = 1u << 1);
    void snapshot(const char *stage); // bounded startup/fault evidence, caller holds lock
public:
    SFCIODart();
    int prepare();
    int suspend(); // refuses outstanding mappings or poison
    int map(uint64_t pa, size_t bytes, uint64_t *dva, size_t alignment = SF_DART_PAGE);
    int unmap(uint64_t dva, size_t bytes);
    uint32_t faults();
    bool healthy(); // IRQ-safe atomic ownership/poison snapshot; no mutex/MMIO
};

/* Page-owned bounce/ring memory: physical pages are distinct from the DMA
 * address. NHI must use dmaAddress(), never getPhysicalAddress(), for these.
 * All USB I/O initially bounces through this type, including control-frame payloads. */
class SFCIODMABuffer : public IOBufferMemoryDescriptor {
    OSDeclareDefaultStructors(SFCIODMABuffer)
    SFCIODart *dart;
    uint64_t dva;
    size_t span;
public:
    static SFCIODMABuffer *allocate(SFCIODart *dart, size_t size, size_t alignment = SF_DART_PAGE);
    uint64_t dmaAddress() const { return dva; }
    int retire(); // leaves the descriptor/backing retained on failure
    void free() override;
};
#endif
