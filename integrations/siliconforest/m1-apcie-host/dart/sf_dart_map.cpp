// SPDX-License-Identifier: GPL-2.0
/*
 * Per-buffer DMA mapping behind the Apple T8020 DART "dart-apcie0", stream 1.
 *
 * Register layout, reset/attach sequence, TLB invalidation and page table format are
 * ported from Linux drivers/iommu/apple-dart.c and io-pgtable-dart.c (omacom/linux-aurora):
 *   Copyright (C) 2021 The Asahi Linux Contributors
 *   apple-dart.c based on arm/arm-smmu/arm-smmu.c and arm/arm-smmu-v3/arm-smmu-v3.c
 *     Copyright (C) 2013 ARM Limited, Copyright (C) 2015 ARM Limited
 *   and on exynos-iommu.c, Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 *   io-pgtable-dart.c based on io-pgtable-arm, Copyright (C) 2014 ARM Limited
 *
 * One L1 table behind TTBR0, L2 tables allocated on demand and never freed (DART1 format,
 * 16 KiB granule, 2048 entries per table, so one L2 table spans 32 MiB of IOVA). IOVAs come
 * from one window, a page at a time, next fit. See sf_dart_map.h for the TLB discipline.
 */
#include "sf_dart_map.h"

#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <libkern/OSByteOrder.h>
#include <mach/vm_param.h>
#include <sys/errno.h>

extern "C" {
/* com.apple.kpi.unsupported */
struct pmap;
extern ppnum_t pmap_find_phys(struct pmap *pmap, addr64_t va);
extern struct pmap *const kernel_pmap;
}

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
#define SF_DART_PAGE_SHIFT  SF_DART_MAP_PAGE_SHIFT
#define SF_DART_PAGE        SF_DART_MAP_PAGE_SIZE
#define SF_DART_PAGE_MASK   ((uint64_t)SF_DART_MAP_PAGE_MASK)
#define SF_DART_PTES        (SF_DART_PAGE / 8)  /* 2048 entries per table */
#define SF_DART_L2_SHIFT    (SF_DART_PAGE_SHIFT + 11)   /* one L2 table spans 32 MiB */
#define SF_DART_L1_ENTRIES  128                 /* 32-bit IOVA: one TTBR, 128 L1 entries */
#define SF_DART_PA_OK_MASK  0x0000000fffffc000ULL       /* 16 KiB aligned, below 2^36 */

/* The IOVA window. Override on the compiler command line; both must be 16 KiB multiples. */
#ifndef SF_DART_IOVA_BASE
#define SF_DART_IOVA_BASE   0x04000000ULL
#endif
#ifndef SF_DART_IOVA_SIZE
#define SF_DART_IOVA_SIZE   0x10000000ULL       /* 256 MiB: 16384 pages, at most 8 L2 tables */
#endif
#define SF_DART_MSI_DOORBELL 0xfffff000ULL      /* the window must end at or below this */
/* The device tree values of the J293, used for the check when the node has no property. */
#define SF_DART_DT_VM_BASE  0x00100000ULL
#define SF_DART_DT_VM_SIZE  0xbfe00000ULL

#define SF_DART_QUARANTINE  256                 /* unmapped ranges waiting for an invalidate */

struct SFDartMem {
    SFDartMem *next;
    IOBufferMemoryDescriptor *md;
    void *kva;
    uint64_t iova;
    size_t size;
};

static IOLock *gLock;
static bool gReady;
static IOMemoryMap *gRegMap;
static volatile uint8_t *gRegs;
static IOBufferMemoryDescriptor *gL1MD;
static uint64_t *gL1;
static uint64_t gL1pa;
static uint64_t *gL2[SF_DART_L1_ENTRIES];       /* kernel pointers to the L2 tables */
static unsigned gL2Count;

static uint64_t gBase, gEnd;                    /* the window, [gBase, gEnd) */
static uint8_t *gUsed;                          /* one byte per IOVA page: 0 free, 1 taken */
static size_t gPages, gHint;
static struct { uint32_t page, count; } gQuar[SF_DART_QUARANTINE];
static unsigned gQuarCount;
static volatile bool gMapDirty;                 /* mappings made since the last invalidate */
static SFDartMem *gMems;

static uint64_t gPagesNow, gPagesPeak, gMaps, gUnmaps, gFlushes;
static uint64_t gMapFailures, gUnmapErrors, gFlushTimeouts, gFaults;

static inline uint32_t rd(uint32_t off) { uint32_t v = OSReadLittleInt32((volatile void *)gRegs, off); OSSynchronizeIO(); return v; }
static inline void wr(uint32_t off, uint32_t v) { OSWriteLittleInt32((volatile void *)gRegs, off, v); OSSynchronizeIO(); }
/* The DART reads the tables from RAM: make every table write visible before going on. */
static inline void tablesWritten(void) { __asm__ volatile("dsb sy" ::: "memory"); }

/* apple_dart_t8020_hw_stream_command(INVALIDATE) for our stream, bounded. */
static int invalidate(void)
{
    wr(DART_T8020_STREAM_SELECT, 1u << SF_DART_SID);
    wr(DART_T8020_STREAM_COMMAND, DART_T8020_STREAM_COMMAND_INVALIDATE);
    for (int i = 0; i < 1100; i++) {            /* 100 x 1 us like Linux, then 1000 x 100 us */
        if (!(rd(DART_T8020_STREAM_COMMAND) & DART_T8020_STREAM_COMMAND_BUSY))
            return 0;
        IODelay(i < 100 ? 1 : 100);
    }
    gFlushTimeouts++;
    DLOG("TLB invalidate timed out, command=0x%08x (%llu timeouts)", rd(DART_T8020_STREAM_COMMAND),
         (unsigned long long)gFlushTimeouts);
    return EIO;
}

/* Lock held. On success every mapping is live and the quarantined IOVAs are free again. */
static int flushLocked(void)
{
    tablesWritten();
    int err = invalidate();
    if (err)
        return err;
    for (unsigned i = 0; i < gQuarCount; i++)
        memset(gUsed + gQuar[i].page, 0, gQuar[i].count);
    gQuarCount = 0;
    gMapDirty = false;
    gFlushes++;
    return 0;
}

static IOBufferMemoryDescriptor *tablePage(uint64_t *pa)
{
    /* 16 KiB aligned, below 36 bits, single page so trivially contiguous. */
    IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, SF_DART_PAGE,
        SF_DART_PA_OK_MASK);
    if (!md)
        return NULL;
    md->prepare();
    bzero(md->getBytesNoCopy(), SF_DART_PAGE);
    *pa = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
    if (!*pa || (*pa & ~SF_DART_PA_OK_MASK)) {
        DLOG("table page has unusable physical address 0x%llx", (unsigned long long)*pa);
        md->complete();
        md->release();
        return NULL;
    }
    return md;
}

static inline uint64_t *pteFor(uint64_t iova)
{
    uint64_t *l2 = gL2[iova >> SF_DART_L2_SHIFT];
    return l2 ? &l2[(iova >> SF_DART_PAGE_SHIFT) & (SF_DART_PTES - 1)] : NULL;
}

/* Lock held. dart_install_table(): the zeroed table is visible before its L1 entry is. */
static uint64_t *l2For(uint64_t iova)
{
    size_t idx = iova >> SF_DART_L2_SHIFT;
    if (gL2[idx])
        return gL2[idx];
    uint64_t pa = 0;
    IOBufferMemoryDescriptor *md = tablePage(&pa);  /* kept for good */
    if (!md)
        return NULL;
    gL2[idx] = (uint64_t *)md->getBytesNoCopy();
    gL2Count++;
    tablesWritten();
    gL1[idx] = (pa & APPLE_DART1_PADDR_MASK) | APPLE_DART_PTE_VALID;
    tablesWritten();
    DLOG("L2 table %u for iova 0x%llx..0x%llx at pa 0x%llx", gL2Count,
         (unsigned long long)(idx << SF_DART_L2_SHIFT),
         (unsigned long long)(((idx + 1) << SF_DART_L2_SHIFT) - 1), (unsigned long long)pa);
    return gL2[idx];
}

/* Lock held. n consecutive free IOVA pages, next fit; the page index or -1. */
static long iovaAlloc(size_t n)
{
    for (int pass = 0; pass < 2; pass++) {
        size_t i = pass ? 0 : gHint;
        while (i + n <= gPages) {
            size_t k = 0;
            while (k < n && !gUsed[i + k])
                k++;
            if (k == n) {
                memset(gUsed + i, 1, n);
                gHint = i + n < gPages ? i + n : 0;
                return (long)i;
            }
            i += k + 1;
        }
    }
    return -1;
}

/* Lock held. Page i comes from pa[i], or from run + i pages when pa is NULL. */
static int mapLocked(const uint64_t *pa, uint64_t run, size_t n, uint64_t *iova)
{
    if (!gReady || !n || !iova)
        return EINVAL;
    for (size_t i = 0; i < n; i++) {
        uint64_t p = pa ? pa[i] : run + i * SF_DART_PAGE;
        if (!p || (p & ~SF_DART_PA_OK_MASK)) {
            if (gMapFailures++ % 256 == 0)
                DLOG("refusing physical address 0x%llx (page %zu of %zu): not 16 KiB aligned below 2^36",
                     (unsigned long long)p, i, n);
            return EFAULT;
        }
    }
    long first = iovaAlloc(n);
    if (first < 0 && gQuarCount && flushLocked() == 0)
        first = iovaAlloc(n);
    if (first < 0) {
        if (gMapFailures++ % 256 == 0)
            DLOG("no %zu consecutive IOVA pages (%llu of %zu pages mapped)", n,
                 (unsigned long long)gPagesNow, gPages);
        return ENOSPC;
    }
    uint64_t base = gBase + ((uint64_t)first << SF_DART_PAGE_SHIFT);
    int err = 0;
    size_t done = 0;
    for (; done < n; done++) {
        uint64_t va = base + done * SF_DART_PAGE;
        uint64_t *pte = l2For(va) ? pteFor(va) : NULL;
        if (!pte) {
            err = ENOMEM;
            break;
        }
        if (*pte & APPLE_DART_PTE_VALID) {      /* io-pgtable-dart: "we require an unmap first" */
            DLOG("iova 0x%llx is already mapped (pte 0x%llx)", (unsigned long long)va,
                 (unsigned long long)*pte);
            err = EEXIST;
            break;
        }
        uint64_t p = pa ? pa[done] : run + done * SF_DART_PAGE;
        *pte = (p & APPLE_DART1_PADDR_MASK) | APPLE_DART1_PTE_PROT_SP_DIS | APPLE_DART_PTE_VALID;
    }
    if (err) {
        /* nobody was told these addresses, so they can go back at once */
        for (size_t i = 0; i < done; i++)
            *pteFor(base + i * SF_DART_PAGE) = 0;
        tablesWritten();
        memset(gUsed + first, 0, n);
        gMapFailures++;
        return err;
    }
    tablesWritten();
    gMapDirty = true;
    gMaps++;
    gPagesNow += n;
    if (gPagesNow > gPagesPeak)
        gPagesPeak = gPagesNow;
    *iova = base;
#ifdef SF_DART_STRICT
    (void)flushLocked();
#endif
    return 0;
}

/* Lock held. With now the IOVAs are invalidated and free again before this returns. */
static int unmapLocked(uint64_t dva, size_t size, bool now)
{
    uint64_t first = dva & ~SF_DART_PAGE_MASK;
    uint64_t end = (dva + size + SF_DART_PAGE_MASK) & ~SF_DART_PAGE_MASK;

    if (!gReady || !size || first < gBase || end > gEnd || end <= first) {
        gUnmapErrors++;
        DLOG("unmap of 0x%llx+0x%zx outside the window refused", (unsigned long long)dva, size);
        return EINVAL;
    }
    size_t n = (end - first) >> SF_DART_PAGE_SHIFT, cleared = 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t *pte = pteFor(first + i * SF_DART_PAGE);
        if (pte && (*pte & APPLE_DART_PTE_VALID)) {
            *pte = 0;
            cleared++;
        }
    }
    tablesWritten();
    gUnmaps++;
    gPagesNow -= cleared;
    if (cleared != n) {
        /* not ours to free: leave the allocator alone */
        gUnmapErrors++;
        DLOG("unmap of 0x%llx+0x%zx: only %zu of %zu pages were mapped", (unsigned long long)dva,
             size, cleared, n);
        return EINVAL;
    }
    if (gQuarCount == SF_DART_QUARANTINE && flushLocked() != 0) {
        /* cannot invalidate and cannot remember it: the range stays out of use */
        gUnmapErrors++;
        return EIO;
    }
    gQuar[gQuarCount].page = (uint32_t)((first - gBase) >> SF_DART_PAGE_SHIFT);
    gQuar[gQuarCount].count = (uint32_t)n;
    gQuarCount++;
#ifdef SF_DART_STRICT
    now = true;
#endif
    return now ? flushLocked() : 0;
}

static bool readProp(IORegistryEntry *node, const char *name, uint64_t *out)
{
    OSData *d = OSDynamicCast(OSData, node->getProperty(name));
    if (!d || d->getLength() < 4)
        return false;
    const void *p = d->getBytesNoCopy();
    *out = d->getLength() >= 8 ? OSReadLittleInt64(p, 0) : OSReadLittleInt32(p, 0);
    return true;
}

int sf_dart_init(void)
{
    if (!gLock)
        gLock = IOLockAlloc();
    IOLockLock(gLock);
    if (gReady) {
        IOLockUnlock(gLock);
        return 0;
    }
    int ret = ENXIO;
    uint32_t params1, config;
    uint64_t vmBase = SF_DART_DT_VM_BASE, vmSize = SF_DART_DT_VM_SIZE;
    bool haveBase, haveSize;

    IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/dart-apcie0", gIODTPlane);
    OSArray *regs = node ? IODTResolveAddressing(node, "reg", NULL) : NULL; /* registry-owned */
    IOMemoryDescriptor *reg = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
    if (!reg) {
        DLOG("no /arm-io/dart-apcie0 reg");
        goto out;
    }
    if (PAGE_SIZE != SF_DART_PAGE) {
        DLOG("kernel page size 0x%llx is not the DART granule", (unsigned long long)PAGE_SIZE);
        goto out;
    }
    haveBase = readProp(node, "vm-base", &vmBase);
    haveSize = readProp(node, "vm-size", &vmSize);
    gBase = SF_DART_IOVA_BASE;
    gEnd = SF_DART_IOVA_BASE + SF_DART_IOVA_SIZE;
    if (!gBase || (gBase & SF_DART_PAGE_MASK) || (gEnd & SF_DART_PAGE_MASK) || gEnd <= gBase ||
        gEnd > SF_DART_MSI_DOORBELL || gEnd > ((uint64_t)SF_DART_L1_ENTRIES << SF_DART_L2_SHIFT) ||
        gBase < vmBase || gEnd > vmBase + vmSize) {
        DLOG("IOVA window 0x%llx..0x%llx does not fit vm-base 0x%llx vm-size 0x%llx below the MSI doorbell 0x%llx",
             (unsigned long long)gBase, (unsigned long long)gEnd - 1, (unsigned long long)vmBase,
             (unsigned long long)vmSize, SF_DART_MSI_DOORBELL);
        goto out;
    }
    if (!gRegMap)
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

    gPages = (size_t)((gEnd - gBase) >> SF_DART_PAGE_SHIFT);
    if (!gUsed)
        gUsed = (uint8_t *)IOMalloc(gPages);
    if (!gL1MD)
        gL1MD = tablePage(&gL1pa);
    if (!gUsed || !gL1MD) {
        DLOG("allocation failed map=%p l1=%p", gUsed, gL1MD);
        goto out;
    }
    bzero(gUsed, gPages);
    gHint = 0;
    gL1 = (uint64_t *)gL1MD->getBytesNoCopy();
    tablesWritten();

    /* apple_dart_hw_reset() for our stream, then attach: TTBR, translate, flush. */
    wr(DART_T8020_TCR(SF_DART_SID), 0);
    for (int i = 0; i < 4; i++)
        wr(DART_T8020_TTBR(SF_DART_SID, i), 0);
    wr(DART_T8020_STREAMS_ENABLE, rd(DART_T8020_STREAMS_ENABLE) | (1u << SF_DART_SID));
    wr(DART_T8020_ERROR, rd(DART_T8020_ERROR));
    if (invalidate())
        goto out;
    wr(DART_T8020_TTBR(SF_DART_SID, 0), DART_T8020_TTBR_VALID | (uint32_t)(gL1pa >> DART_T8020_TTBR_SHIFT));
    wr(DART_T8020_TCR(SF_DART_SID), DART_T8020_TCR_TRANSLATE_ENABLE);
    if (invalidate())
        goto out;

    DLOG("stream %d translating, per-buffer mapping: iova window 0x%llx..0x%llx (%zu pages), vm-base 0x%llx%s vm-size 0x%llx%s, l1 pa 0x%llx, l2 on demand, %s invalidate, tcr=0x%08x ttbr0=0x%08x",
         SF_DART_SID, (unsigned long long)gBase, (unsigned long long)gEnd - 1, gPages,
         (unsigned long long)vmBase, haveBase ? "" : " (assumed)",
         (unsigned long long)vmSize, haveSize ? "" : " (assumed)", (unsigned long long)gL1pa,
#ifdef SF_DART_STRICT
         "strict",
#else
         "batched",
#endif
         rd(DART_T8020_TCR(SF_DART_SID)), rd(DART_T8020_TTBR(SF_DART_SID, 0)));
    gReady = true;
    ret = 0;
out:
    OSSafeReleaseNULL(node);
    IOLockUnlock(gLock);
    return ret;
}

int sf_dart_resume(void)
{
    if (!gLock || !gReady)
        return 0;
    IOLockLock(gLock);
    int ret = 0;
    uint32_t want = DART_T8020_TTBR_VALID | (uint32_t)(gL1pa >> DART_T8020_TTBR_SHIFT);
    uint32_t tcr = rd(DART_T8020_TCR(SF_DART_SID)), ttbr = rd(DART_T8020_TTBR(SF_DART_SID, 0));

    if ((tcr & DART_T8020_TCR_TRANSLATE_ENABLE) && ttbr == want) {
        DLOG("resume: stream %d still translates (tcr=0x%08x ttbr0=0x%08x), nothing written", SF_DART_SID, tcr, ttbr);
    } else if (rd(DART_T8020_CONFIG) & DART_T8020_CONFIG_LOCK) {
        DLOG("resume: stream %d lost its state (tcr=0x%08x ttbr0=0x%08x) and the DART is locked", SF_DART_SID, tcr, ttbr);
        ret = EPERM;
    } else {
        /* the same sequence as sf_dart_init(), with the table that exists */
        tablesWritten();
        wr(DART_T8020_TCR(SF_DART_SID), 0);
        for (int i = 0; i < 4; i++)
            wr(DART_T8020_TTBR(SF_DART_SID, i), 0);
        wr(DART_T8020_STREAMS_ENABLE, rd(DART_T8020_STREAMS_ENABLE) | (1u << SF_DART_SID));
        wr(DART_T8020_ERROR, rd(DART_T8020_ERROR));
        ret = invalidate();
        if (ret == 0) {
            wr(DART_T8020_TTBR(SF_DART_SID, 0), want);
            wr(DART_T8020_TCR(SF_DART_SID), DART_T8020_TCR_TRANSLATE_ENABLE);
            ret = flushLocked();
        }
        DLOG("resume: stream %d had tcr=0x%08x ttbr0=0x%08x, programmed again: tcr=0x%08x ttbr0=0x%08x%s", SF_DART_SID,
             tcr, ttbr, rd(DART_T8020_TCR(SF_DART_SID)), rd(DART_T8020_TTBR(SF_DART_SID, 0)), ret ? " FAILED" : "");
    }
    IOLockUnlock(gLock);
    return ret;
}

int sf_dart_map_pages(const uint64_t *pa, unsigned npages, uint64_t *iova)
{
    if (!gReady || !pa)
        return EINVAL;
    IOLockLock(gLock);
    int err = mapLocked(pa, 0, npages, iova);
    IOLockUnlock(gLock);
    return err;
}

int sf_dart_map(uint64_t pa, size_t size, uint64_t *dva)
{
    uint64_t off = pa & SF_DART_PAGE_MASK, iova = 0;

    if (!gReady || !size || !dva)
        return EINVAL;
    IOLockLock(gLock);
    int err = mapLocked(NULL, pa - off, (size_t)((off + size + SF_DART_PAGE_MASK) >> SF_DART_PAGE_SHIFT), &iova);
    IOLockUnlock(gLock);
    if (!err)
        *dva = iova + off;
    return err;
}

int sf_dart_unmap(uint64_t dva, size_t size)
{
    if (!gReady)
        return EINVAL;
    IOLockLock(gLock);
    int err = unmapLocked(dva, size, false);
    IOLockUnlock(gLock);
    return err;
}

int sf_dart_sync(void)
{
    if (!gReady)
        return EINVAL;
    if (!gMapDirty)
        return 0;
    IOLockLock(gLock);
    int err = gMapDirty ? flushLocked() : 0;
    IOLockUnlock(gLock);
    return err;
}

void *sf_dart_alloc(size_t size, uint64_t *iova)
{
    if (!gReady || !size || !iova)
        return NULL;
    size_t bytes = (size + SF_DART_PAGE - 1) & ~(size_t)(SF_DART_PAGE - 1);
    size_t n = bytes >> SF_DART_PAGE_SHIFT;
    SFDartMem *mem = IONew(SFDartMem, 1);
    uint64_t *pa = IONew(uint64_t, n);
    IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut, bytes, SF_DART_PA_OK_MASK);
    bool prepared = false;
    int err = ENOMEM;

    if (!mem || !pa || !md)
        goto fail;
    if (md->prepare() != kIOReturnSuccess)
        goto fail;
    prepared = true;
    bzero(md->getBytesNoCopy(), bytes);
    for (size_t i = 0; i < n; i++)
        pa[i] = md->getPhysicalSegment(i * SF_DART_PAGE, NULL, kIOMemoryMapperNone);
    mem->md = md;
    mem->kva = md->getBytesNoCopy();
    mem->size = bytes;

    IOLockLock(gLock);
    err = mapLocked(pa, 0, n, &mem->iova);
    if (!err && (err = flushLocked()) != 0)
        (void)unmapLocked(mem->iova, bytes, false);
    if (!err) {
        mem->next = gMems;
        gMems = mem;
    }
    IOLockUnlock(gLock);
    if (err)
        goto fail;
    IODelete(pa, uint64_t, n);
    *iova = mem->iova;
    return mem->kva;
fail:
    DLOG("no %zu bytes of mapped memory (error %d)", size, err);
    if (md) {
        if (prepared)
            md->complete();
        md->release();
    }
    if (pa)
        IODelete(pa, uint64_t, n);
    if (mem)
        IODelete(mem, SFDartMem, 1);
    return NULL;
}

void sf_dart_free(void *kva)
{
    if (!gReady || !kva)
        return;
    SFDartMem *mem, **prev;
    int err = 0;

    IOLockLock(gLock);
    for (prev = &gMems; (mem = *prev) != NULL; prev = &mem->next) {
        if (mem->kva == kva) {
            *prev = mem->next;
            /* the pages go back to the kernel now, so the device must lose them now */
            err = unmapLocked(mem->iova, mem->size, true);
            break;
        }
    }
    IOLockUnlock(gLock);
    if (!mem) {
        DLOG("free of unknown memory %p", kva);
        return;
    }
    if (err) {
        /* still reachable by the device: leaking it is the safe choice */
        DLOG("could not unmap iova 0x%llx (error %d), leaking %zu bytes", (unsigned long long)mem->iova,
             err, mem->size);
    } else {
        mem->md->complete();
        mem->md->release();
    }
    IODelete(mem, SFDartMem, 1);
}

uint64_t sf_dart_kvtophys(const void *kva)
{
    ppnum_t pn = pmap_find_phys(kernel_pmap, (addr64_t)(uintptr_t)kva);

    return pn ? (ptoa_64(pn) | ((uint64_t)(uintptr_t)kva & PAGE_MASK)) : 0;
}

uint32_t sf_dart_check(const char *why)
{
    if (!gRegs)
        return 0;
    uint32_t err = rd(DART_T8020_ERROR);
    if (!(err & DART_T8020_ERROR_FLAG))
        return 0;
    uint32_t hi = rd(DART_T8020_ERROR_ADDR_HI), lo = rd(DART_T8020_ERROR_ADDR_LO);
    uint64_t addr = ((uint64_t)hi << 32) | lo, pte = 0;
    const char *where = "outside the window";

    /* tables are never freed, so looking without the lock is safe */
    if (addr >= gBase && addr < gEnd) {
        uint64_t *p = pteFor(addr);
        pte = p ? *p : 0;
        where = !p ? "in the window, no L2 table" :
            (pte & APPLE_DART_PTE_VALID) ? "in the window, mapped" : "in the window, not mapped";
    }
    gFaults++;
    DLOG("FAULT (%s): error=0x%08x stream=%u code=0x%03x addr=0x%08x%08x (%s, pte 0x%llx) fault %llu",
         why ? why : "", err, (err >> 24) & 0xf, err & 0xfff, hi, lo, where, (unsigned long long)pte,
         (unsigned long long)gFaults);
    wr(DART_T8020_ERROR, err);
    return err;
}

void sf_dart_report(const char *who)
{
    if (!gLock)
        return;
    IOLockLock(gLock);
    DLOG("%s: %llu of %zu pages mapped (peak %llu), %u L2 tables, %llu maps, %llu unmaps, %llu invalidates, %u ranges awaiting one, %llu map failures, %llu unmap errors, %llu invalidate timeouts, %llu faults",
         who ? who : "", (unsigned long long)gPagesNow, gPages, (unsigned long long)gPagesPeak, gL2Count,
         (unsigned long long)gMaps, (unsigned long long)gUnmaps, (unsigned long long)gFlushes, gQuarCount,
         (unsigned long long)gMapFailures, (unsigned long long)gUnmapErrors,
         (unsigned long long)gFlushTimeouts, (unsigned long long)gFaults);
    IOLockUnlock(gLock);
}
