// SPDX-License-Identifier: GPL-2.0
/*
 * Static DMA bounce pool behind the Apple T8020 DART "dart-apcie0", stream 1.
 *
 * Register layout, reset/attach sequence and page table format are ported from
 * Linux drivers/iommu/apple-dart.c and io-pgtable-dart.c (omacom/linux-aurora):
 *   Copyright (C) 2021 The Asahi Linux Contributors
 *   apple-dart.c based on arm/arm-smmu/arm-smmu.c and arm/arm-smmu-v3/arm-smmu-v3.c
 *     Copyright (C) 2013 ARM Limited, Copyright (C) 2015 ARM Limited
 *   and on exynos-iommu.c, Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 *   io-pgtable-dart.c based on io-pgtable-arm, Copyright (C) 2014 ARM Limited
 *
 * One fixed mapping: a wired kernel buffer is mapped page by page at a fixed
 * IOVA. Drivers carve DMA memory out of it and bounce packet data through it.
 * Nothing is ever unmapped while the system runs.
 */
#include "sf_dart_pool.h"
#include "sf_dart_map.h"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>

#define DLOG(fmt, ...) IOLog("SFDart: " fmt "\n", ##__VA_ARGS__)

/* apple-dart.c, T8020 */
#define DART_PARAMS1                        0x00
#define DART_PARAMS1_PAGE_SHIFT(v)          (((v) >> 24) & 0xf)
#define DART_PARAMS2                        0x04
#define DART_T8020_STREAM_COMMAND           0x20
#define DART_T8020_STREAM_COMMAND_BUSY      (1u << 2)
#define DART_T8020_STREAM_COMMAND_INVALIDATE (1u << 20)
#define DART_T8020_STREAM_SELECT            0x34
#define DART_T8020_ERROR                    0x40
#define DART_T8020_ERROR_FLAG               (1u << 31)
#define DART_T8020_ERROR_ADDR_LO            0x50
#define DART_T8020_ERROR_ADDR_HI            0x54
#define DART_T8020_CONFIG                   0x60
#define DART_T8020_CONFIG_LOCK              (1u << 15)
#define DART_T8020_STREAMS_ENABLE           0xfc
#define DART_T8020_TCR(sid)                 (0x100 + 4 * (sid))
#define DART_T8020_TCR_TRANSLATE_ENABLE     (1u << 7)
#define DART_T8020_TTBR(sid, idx)           (0x200 + 16 * (sid) + 4 * (idx))
#define DART_T8020_TTBR_VALID               (1u << 31)
#define DART_T8020_TTBR_SHIFT               12

/* io-pgtable-dart.c, DART1 format */
#define APPLE_DART1_PADDR_MASK              0x0000000ffffff000ULL
#define APPLE_DART1_PTE_PROT_SP_DIS         (1ULL << 1)
#define APPLE_DART_PTE_VALID                (1ULL << 0)

#define SF_DART_SID         1                   /* Linux iommu-map: RID 0x100 -> stream 1 */
#define SF_DART_PAGE_SHIFT  14
#define SF_DART_PAGE        (1UL << SF_DART_PAGE_SHIFT)
#define SF_DART_PTES        (SF_DART_PAGE / 8)  /* 2048 entries per table */
#define SF_DART_L2_SPAN     ((uint64_t)SF_DART_PTES << SF_DART_PAGE_SHIFT) /* 32 MiB */
#define SF_POOL_IOVA        0x04000000ULL       /* inside vm-base/vm-size, below the MSI doorbell */
#define SF_POOL_SIZE        (16UL << 20)        /* one L2 table covers it */
#define SF_POOL_UNIT        256                 /* allocation granule */

static IOLock *gLock;
static bool gReady;
static IOMemoryMap *gRegMap;
static volatile uint8_t *gRegs;
static IOBufferMemoryDescriptor *gPoolMD, *gL1MD, *gL2MD;
static uint8_t *gPool;
static uint8_t *gUsed;          /* one byte per granule: 0 free, 1 used */
static size_t gUnits;

static inline uint32_t rd(uint32_t off) { uint32_t v = OSReadLittleInt32((volatile void *)gRegs, off); OSSynchronizeIO(); return v; }
static inline void wr(uint32_t off, uint32_t v) { OSWriteLittleInt32((volatile void *)gRegs, off, v); OSSynchronizeIO(); }

static int invalidate(uint32_t sidmask)
{
    wr(DART_T8020_STREAM_SELECT, sidmask);
    wr(DART_T8020_STREAM_COMMAND, DART_T8020_STREAM_COMMAND_INVALIDATE);
    for (int i = 0; i < 1000; i++) {
        if (!(rd(DART_T8020_STREAM_COMMAND) & DART_T8020_STREAM_COMMAND_BUSY))
            return 0;
        IODelay(100);
    }
    DLOG("TLB invalidate timed out, command=0x%08x", rd(DART_T8020_STREAM_COMMAND));
    return -1;
}

static IOBufferMemoryDescriptor *tablePage(uint64_t *pa)
{
    /* 16 KiB aligned, below 36 bits, single page so trivially contiguous. */
    IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, SF_DART_PAGE,
        0x0000000fffffc000ULL);
    if (!md)
        return NULL;
    md->prepare();
    bzero(md->getBytesNoCopy(), SF_DART_PAGE);
    *pa = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
    return md;
}

uint32_t sf_dart_pool_check(const char *why)
{
    if (!gRegs)
        return sf_dart_check(why);  /* the pool is not in use: the per-buffer mapper may be */
    uint32_t err = rd(DART_T8020_ERROR);
    if (err & DART_T8020_ERROR_FLAG) {
        DLOG("FAULT (%s): error=0x%08x stream=%u code=0x%03x addr=0x%08x%08x", why ? why : "",
             err, (err >> 24) & 0xf, err & 0xfff, rd(DART_T8020_ERROR_ADDR_HI), rd(DART_T8020_ERROR_ADDR_LO));
        wr(DART_T8020_ERROR, err);
        return err;
    }
    return 0;
}

int sf_dart_pool_init(void)
{
    if (!gLock)
        gLock = IOLockAlloc();
    IOLockLock(gLock);
    if (gReady) {
        IOLockUnlock(gLock);
        return 0;
    }
    int ret = -1;
    uint64_t l1pa = 0, l2pa = 0;
    uint64_t *l1, *l2;
    uint32_t params1, config;

    IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/dart-apcie0", gIODTPlane);
    OSArray *regs = node ? IODTResolveAddressing(node, "reg", NULL) : NULL; /* registry-owned */
    IOMemoryDescriptor *reg = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
    if (!reg) {
        DLOG("no /arm-io/dart-apcie0 reg");
        goto out;
    }
    gRegMap = reg->map(kIOMapInhibitCache);
    if (!gRegMap) {
        DLOG("could not map DART registers");
        goto out;
    }
    gRegs = (volatile uint8_t *)gRegMap->getVirtualAddress();
    params1 = rd(DART_PARAMS1);
    config = rd(DART_T8020_CONFIG);
    DLOG("regs phys=0x%llx params1=0x%08x params2=0x%08x config=0x%08x tcr[%d]=0x%08x ttbr0=0x%08x error=0x%08x",
         (unsigned long long)reg->getPhysicalSegment(0, NULL, kIOMemoryMapperNone), params1,
         rd(DART_PARAMS2), config, SF_DART_SID, rd(DART_T8020_TCR(SF_DART_SID)),
         rd(DART_T8020_TTBR(SF_DART_SID, 0)), rd(DART_T8020_ERROR));
    if (DART_PARAMS1_PAGE_SHIFT(params1) != SF_DART_PAGE_SHIFT) {
        DLOG("unexpected DART page shift %u", DART_PARAMS1_PAGE_SHIFT(params1));
        goto out;
    }
    if (config & DART_T8020_CONFIG_LOCK) {
        DLOG("DART is locked; cannot program it");
        goto out;
    }

    gPoolMD = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut, SF_POOL_SIZE, 0x0000000fffffc000ULL);
    gL1MD = tablePage(&l1pa);
    gL2MD = tablePage(&l2pa);
    gUnits = SF_POOL_SIZE / SF_POOL_UNIT;
    gUsed = (uint8_t *)IOMalloc(gUnits);
    if (!gPoolMD || !gL1MD || !gL2MD || !gUsed) {
        DLOG("allocation failed pool=%p l1=%p l2=%p", gPoolMD, gL1MD, gL2MD);
        goto out;
    }
    gPoolMD->prepare();
    gPool = (uint8_t *)gPoolMD->getBytesNoCopy();
    bzero(gPool, SF_POOL_SIZE);
    bzero(gUsed, gUnits);
    l1 = (uint64_t *)gL1MD->getBytesNoCopy();
    l2 = (uint64_t *)gL2MD->getBytesNoCopy();

    for (size_t off = 0; off < SF_POOL_SIZE; off += SF_DART_PAGE) {
        uint64_t pa = gPoolMD->getPhysicalSegment(off, NULL, kIOMemoryMapperNone);
        if (!pa || (pa & (SF_DART_PAGE - 1)) || (pa & ~APPLE_DART1_PADDR_MASK)) {
            DLOG("pool page at 0x%zx has unusable physical address 0x%llx", off, (unsigned long long)pa);
            goto out;
        }
        size_t idx = ((SF_POOL_IOVA + off) & (SF_DART_L2_SPAN - 1)) >> SF_DART_PAGE_SHIFT;
        l2[idx] = (pa & APPLE_DART1_PADDR_MASK) | APPLE_DART1_PTE_PROT_SP_DIS | APPLE_DART_PTE_VALID;
    }
    l1[SF_POOL_IOVA / SF_DART_L2_SPAN] = (l2pa & APPLE_DART1_PADDR_MASK) | APPLE_DART_PTE_VALID;
    __asm__ volatile("dsb sy" ::: "memory");

    /* apple_dart_hw_reset() for our stream, then attach: TTBR, translate, flush. */
    wr(DART_T8020_TCR(SF_DART_SID), 0);
    for (int i = 0; i < 4; i++)
        wr(DART_T8020_TTBR(SF_DART_SID, i), 0);
    wr(DART_T8020_STREAMS_ENABLE, rd(DART_T8020_STREAMS_ENABLE) | (1u << SF_DART_SID));
    wr(DART_T8020_ERROR, rd(DART_T8020_ERROR));
    if (invalidate(1u << SF_DART_SID))
        goto out;
    wr(DART_T8020_TTBR(SF_DART_SID, 0), DART_T8020_TTBR_VALID | (uint32_t)(l1pa >> DART_T8020_TTBR_SHIFT));
    wr(DART_T8020_TCR(SF_DART_SID), DART_T8020_TCR_TRANSLATE_ENABLE);
    if (invalidate(1u << SF_DART_SID))
        goto out;

    DLOG("stream %d translating: iova 0x%llx..0x%llx -> pool kva %p (first page pa 0x%llx), l1 pa 0x%llx l2 pa 0x%llx tcr=0x%08x ttbr0=0x%08x",
         SF_DART_SID, SF_POOL_IOVA, SF_POOL_IOVA + SF_POOL_SIZE - 1, gPool,
         (unsigned long long)gPoolMD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone),
         (unsigned long long)l1pa, (unsigned long long)l2pa,
         rd(DART_T8020_TCR(SF_DART_SID)), rd(DART_T8020_TTBR(SF_DART_SID, 0)));
    gReady = true;
    ret = 0;
out:
    OSSafeReleaseNULL(node);
    IOLockUnlock(gLock);
    return ret;
}

void *sf_dart_pool_alloc(size_t size, size_t align, uint64_t *iova)
{
    if (!gReady || !size || !iova)
        return NULL;
    if (align < SF_POOL_UNIT)
        align = SF_POOL_UNIT;
    size_t need = (size + SF_POOL_UNIT - 1) / SF_POOL_UNIT;
    size_t step = align / SF_POOL_UNIT;
    void *result = NULL;

    IOLockLock(gLock);
    for (size_t start = 0; start + need <= gUnits; start += step) {
        size_t n = 0;
        while (n < need && !gUsed[start + n])
            n++;
        if (n == need) {
            memset(gUsed + start, 1, need);
            result = gPool + start * SF_POOL_UNIT;
            *iova = SF_POOL_IOVA + start * SF_POOL_UNIT;
            break;
        }
    }
    IOLockUnlock(gLock);
    if (result)
        bzero(result, need * SF_POOL_UNIT);
    else
        DLOG("pool exhausted for %zu bytes (align %zu)", size, align);
    return result;
}

void sf_dart_pool_free(void *kva, size_t size)
{
    if (!gReady || !kva || !size)
        return;
    size_t start = ((uint8_t *)kva - gPool) / SF_POOL_UNIT;
    size_t need = (size + SF_POOL_UNIT - 1) / SF_POOL_UNIT;
    if ((uint8_t *)kva < gPool || start + need > gUnits)
        return;
    IOLockLock(gLock);
    memset(gUsed + start, 0, need);
    IOLockUnlock(gLock);
}
