// SPDX-License-Identifier: GPL-2.0
/*
 * T8103 USB1 paired DART domain. Adapted from m1-apcie-host/dart/sf_dart_map.cpp
 * and m1-dcp-dart's table code, using Linux apple-dart.c/io-pgtable-dart.c:
 * Copyright (C) 2021 The Asahi Linux Contributors
 * apple-dart.c based on ARM SMMU: Copyright (C) 2013, 2015 ARM Limited;
 * exynos-iommu: Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 * io-pgtable-dart.c based on io-pgtable-arm: Copyright (C) 2014 ARM Limited.
 */
#include "sf_usb_dart.h"
#include "sf_usb_iova.h"
#include <sys/errno.h>
#include <mach/vm_param.h>

#define LOG(fmt, ...) IOLog("SFUSBDart: " fmt "\n", ##__VA_ARGS__)
#define PARAMS1 0x00
#define STREAM_COMMAND 0x20
#define COMMAND_BUSY (1u << 2)
#define COMMAND_INVALIDATE (1u << 20)
#define STREAM_SELECT 0x34
#define ERROR 0x40
#define CONFIG 0x60
#define CONFIG_LOCK (1u << 15)
#define STREAMS_ENABLE 0xfc
#define TCR(sid) (0x100 + 4 * (sid))
#define TCR_TRANSLATE (1u << 7)
#define TTBR(sid, n) (0x200 + 16 * (sid) + 4 * (n))
#define TTBR_VALID (1u << 31)
#define PA_MASK 0x0000000fffffc000ULL
#define IOVA_LO 0x04000000ULL
#define IOVA_BYTES 0x10000000ULL
#define NPAGES (IOVA_BYTES / SF_DART_PAGE)

static void visible() { __asm__ volatile("dsb sy" ::: "memory"); }

SFUSBDart::SFUSBDart() : lock(NULL), ntables(0), busy(NULL), ready(false), poisoned(false)
{
    bzero(tables, sizeof(tables));
    bzero(&pt, sizeof(pt));
    pt.ops.ctx = this;
    pt.ops.table = table;
    pt.ops.alloc_table = allocTable;
}

uint64_t *SFUSBDart::table(void *ctx, uint64_t pa)
{
    SFUSBDart *self = (SFUSBDart *)ctx;
    for (unsigned i = 0; i < self->ntables; ++i)
        if (self->tables[i].pa == pa)
            return (uint64_t *)self->tables[i].md->getBytesNoCopy();
    return NULL;
}

uint64_t SFUSBDart::allocTable(void *ctx)
{
    SFUSBDart *self = (SFUSBDart *)ctx;
    if (self->ntables == sizeof(self->tables) / sizeof(self->tables[0])) return 0;
    IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(
        kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous, SF_DART_PAGE, PA_MASK);
    if (!md) return 0;
    uint64_t pa = md->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
    if (!pa || (pa & ~PA_MASK)) { md->release(); return 0; }
    bzero(md->getBytesNoCopy(), SF_DART_PAGE);
    self->tables[self->ntables++] = {md, pa};
    return pa;
}

int SFUSBDart::invalidate()
{
    visible();
    for (unsigned i = 0; i < 2; ++i) {
        regs[i].w32(STREAM_SELECT, 1u << i);
        regs[i].w32(STREAM_COMMAND, COMMAND_INVALIDATE);
        unsigned poll;
        for (poll = 0; poll < 10000; ++poll) {
            if (!(regs[i].r32(STREAM_COMMAND) & COMMAND_BUSY)) break;
            IODelay(1);
        }
        if (poll == 10000) {
            poisoned = true;
            LOG("bank %u stream %u invalidate timed out; mappings quarantined", i, i);
            return ETIMEDOUT;
        }
    }
    return 0;
}

int SFUSBDart::prepare()
{
    // Single USB1 owner calls prepare before xHCI or any allocation is published.
    if (!lock) lock = IOLockAlloc();
    if (!lock) return ENOMEM;
    IOLockLock(lock);
    int ret = ENXIO;
    if (poisoned) { IOLockUnlock(lock); return EIO; }
    if (PAGE_SIZE != SF_DART_PAGE) { IOLockUnlock(lock); return EINVAL; }
    if (!regs[0].va || !regs[1].va) {
        if (sf_pmgr_enable_node("/arm-io/dart-usb1", -1, 0, true) ||
            !regs[0].mapNode("/arm-io/dart-usb1", 0) ||
            !regs[1].mapNode("/arm-io/dart-usb1", 1)) {
            IOLockUnlock(lock);
            return ENXIO;
        }
    }
    // Validate both banks before changing either one.
    for (unsigned i = 0; i < 2; ++i) {
        if (regs[i].len < 0x240 || ((regs[i].r32(PARAMS1) >> 24) & 15) != 14 ||
            (regs[i].r32(CONFIG) & CONFIG_LOCK)) {
            LOG("bank %u not an unlocked 16KiB T8103 DART", i);
            IOLockUnlock(lock);
            return ENODEV;
        }
    }
    if (!busy) {
        busy = (uint8_t *)IOMalloc(NPAGES);
        if (busy) bzero(busy, NPAGES);
    }
    if (!pt.l1) {
        uint64_t pa = allocTable(this);
        if (pa) pt.l1 = table(this, pa);
    }
    if (!busy || !pt.l1) { IOLockUnlock(lock); return ENOMEM; }
    visible();
    // apple_dart_hw_reset/attach for only the two USB streams. Other stream
    // TCRs/TTBRs and every other DART are left alone. Table contents survive
    // controller reset and subsequent prepare calls.
    for (unsigned i = 0; i < 2; ++i) {
        regs[i].w32(TCR(i), 0);
        for (unsigned n = 0; n < 4; ++n) regs[i].w32(TTBR(i, n), 0);
        regs[i].w32(STREAMS_ENABLE, regs[i].r32(STREAMS_ENABLE) | (1u << i));
        regs[i].w32(ERROR, regs[i].r32(ERROR));
    }
    ret = invalidate();
    if (!ret) {
        for (unsigned i = 0; i < 2; ++i) {
            regs[i].w32(TTBR(i, 0), TTBR_VALID | (uint32_t)(tables[0].pa >> 12));
            regs[i].w32(TCR(i), TCR_TRANSLATE);
        }
        ret = invalidate();
    }
    ready = !ret;
    LOG("USB1 paired domain prepare=%d root=0x%llx (no controller DMA tested)", ret, tables[0].pa);
    IOLockUnlock(lock);
    return ret;
}

int SFUSBDart::map(uint64_t pa, size_t bytes, uint64_t *dva, size_t alignment)
{
    if (!dva || !pa || !bytes || bytes > IOVA_BYTES ||
        alignment < SF_DART_PAGE || alignment > IOVA_BYTES || (alignment & (alignment - 1)) ||
        (bytes & (SF_DART_PAGE - 1)) || (pa & ~PA_MASK) ||
        bytes - 1 > ((1ULL << 36) - 1) - pa) return EINVAL;
    if (!lock) return ENXIO;
    IOLockLock(lock);
    if (!ready || poisoned) { IOLockUnlock(lock); return EIO; }
    size_t count = bytes / SF_DART_PAGE;
    size_t first = sf_usb_iova_find(busy, NPAGES, IOVA_LO, SF_DART_PAGE, count, alignment);
    if (first == SIZE_MAX) { IOLockUnlock(lock); return ENOSPC; }
    uint64_t addr = IOVA_LO + first * SF_DART_PAGE;
    int ret = sf_dart_table_map(&pt, addr, pa, bytes, SF_DART_PROT_READ | SF_DART_PROT_WRITE);
    if (!ret) {
        memset(busy + first, 1, count);
        ret = invalidate();
        // The caller keeps its backing memory on a failed invalidate. Never
        // recycle the IOVA or backing while a bank may retain the mapping.
        if (!ret) *dva = addr;
    }
    IOLockUnlock(lock);
    return ret < 0 ? -ret : ret;
}

int SFUSBDart::unmap(uint64_t dva, size_t bytes)
{
    if (!lock || !bytes || (dva & (SF_DART_PAGE - 1)) ||
        (bytes & (SF_DART_PAGE - 1)) || dva < IOVA_LO ||
        dva >= IOVA_LO + IOVA_BYTES || bytes > IOVA_LO + IOVA_BYTES - dva) return EINVAL;
    IOLockLock(lock);
    if (!ready || poisoned) { IOLockUnlock(lock); return EIO; }
    int ret = 0;
    if (sf_dart_table_unmap(&pt, dva, bytes) != bytes) {
        poisoned = true;
        ret = EIO;
    } else {
        ret = invalidate();
        if (!ret) memset(busy + (dva - IOVA_LO) / SF_DART_PAGE, 0, bytes / SF_DART_PAGE);
    }
    IOLockUnlock(lock);
    return ret;
}

uint32_t SFUSBDart::faults()
{
    uint32_t value = 0;
    if (!lock) return 0;
    IOLockLock(lock);
    if (regs[0].va && regs[1].va)
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t e = regs[i].r32(ERROR);
            if (e & (1u << 31)) { LOG("bank %u fault %08x", i, e); value |= e; }
        }
    IOLockUnlock(lock);
    return value;
}

OSDefineMetaClassAndStructors(SFUSBDMABuffer, IOBufferMemoryDescriptor)

SFUSBDMABuffer *SFUSBDMABuffer::allocate(SFUSBDart *mapper, size_t bytes, size_t alignment)
{
    if (!mapper || !bytes || bytes > IOVA_BYTES || alignment < SF_DART_PAGE ||
        alignment > IOVA_BYTES || (alignment & (alignment - 1))) return NULL;
    size_t span = (bytes + SF_DART_PAGE - 1) & ~(SF_DART_PAGE - 1);
    SFUSBDMABuffer *mem = OSTypeAlloc(SFUSBDMABuffer);
    if (!mem) return NULL;
    if (!mem->initWithPhysicalMask(kernel_task, kIODirectionInOut | kIOMemoryPhysicallyContiguous,
                                  span, alignment, PA_MASK)) {
        mem->release();
        return NULL;
    }
    uint64_t pa = mem->getPhysicalSegment(0, NULL, kIOMemoryMapperNone), dva = 0;
    bzero(mem->getBytesNoCopy(), span);
    int ret = mapper->map(pa, span, &dva, alignment);
    if (ret) {
        // A failed map may be a TLB timeout after the entries were installed.
        // Retain the descriptor/backing on failure; it cannot reach hardware
        // through xHCI, and cannot be reallocated while a DART might cache it.
        LOG("DMA allocation map failed %d; backing quarantined", ret);
        return NULL;
    }
    mem->dart = mapper;
    mem->dva = dva;
    mem->span = span;
    return mem;
}

void SFUSBDMABuffer::free()
{
    if (dart && dart->unmap(dva, span)) {
        LOG("DMA buffer unmap failed; backing quarantined");
        return;
    }
    IOBufferMemoryDescriptor::free();
}

bool SFUSBDart::healthy()
{
    if (!lock) return false;
    IOLockLock(lock);
    bool ok=ready && !poisoned;
    IOLockUnlock(lock);
    return ok;
}
