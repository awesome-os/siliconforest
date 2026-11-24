// SPDX-License-Identifier: GPL-2.0
/*
 * T8103 ACIO1 single-stream DART domain. Adapted from m1-apcie-host/dart/sf_dart_map.cpp
 * and m1-dcp-dart's table code, using Linux apple-dart.c/io-pgtable-dart.c:
 * Copyright (C) 2021 The Asahi Linux Contributors
 * apple-dart.c based on ARM SMMU: Copyright (C) 2013, 2015 ARM Limited;
 * exynos-iommu: Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 * io-pgtable-dart.c based on io-pgtable-arm: Copyright (C) 2014 ARM Limited.
 */
#include "sf_cio_dart.h"
#include "sf_usb_iova.h"
#include <sys/errno.h>
#include <mach/vm_param.h>
#include "sf_cio_dart_reset.h"

#define LOG(fmt, ...) IOLog("SFCIODart: " fmt "\n", ##__VA_ARGS__)
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

void SFCIODart::snapshot(const char *stage)
{
    // Read only this powered NHI DART. No error acknowledgement, stream
    // reassignment or polling: preserve the first fault for diagnosis.
    LOG("%s params=%08x/%08x config=%08x enable=%08x error=%08x va=%08x:%08x remap=%08x/%08x/%08x/%08x",
        stage, regs[0].r32(PARAMS1), regs[0].r32(4), regs[0].r32(CONFIG),
        regs[0].r32(STREAMS_ENABLE), regs[0].r32(ERROR),
        regs[0].r32(0x54), regs[0].r32(0x50),
        regs[0].r32(0x80), regs[0].r32(0x84), regs[0].r32(0x88), regs[0].r32(0x8c));
    for (unsigned sid = 0; sid < 16; ++sid)
        LOG("%s SID%u tcr=%08x ttbr=%08x/%08x/%08x/%08x", stage, sid,
            regs[0].r32(TCR(sid)), regs[0].r32(TTBR(sid, 0)),
            regs[0].r32(TTBR(sid, 1)), regs[0].r32(TTBR(sid, 2)),
            regs[0].r32(TTBR(sid, 3)));
}

SFCIODart::SFCIODart() : lock(NULL), ntables(0), busy(NULL), ready(false), poisoned(false)
{
    bzero(tables, sizeof(tables));
    bzero(&pt, sizeof(pt));
    pt.ops.ctx = this;
    pt.ops.table = table;
    pt.ops.alloc_table = allocTable;
}

uint64_t *SFCIODart::table(void *ctx, uint64_t pa)
{
    SFCIODart *self = (SFCIODart *)ctx;
    for (unsigned i = 0; i < self->ntables; ++i)
        if (self->tables[i].pa == pa)
            return (uint64_t *)self->tables[i].md->getBytesNoCopy();
    return NULL;
}

uint64_t SFCIODart::allocTable(void *ctx)
{
    SFCIODart *self = (SFCIODart *)ctx;
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

int SFCIODart::invalidate(uint32_t streams)
{
    visible();
    for (unsigned i = 0; i < 1; ++i) {
        regs[i].w32(STREAM_SELECT, streams);
        regs[i].w32(STREAM_COMMAND, COMMAND_INVALIDATE);
        unsigned poll;
        for (poll = 0; poll < 10000; ++poll) {
            if (!(regs[i].r32(STREAM_COMMAND) & COMMAND_BUSY)) break;
            IODelay(1);
        }
        if (poll == 10000) {
            __atomic_store_n(&poisoned,true,__ATOMIC_RELEASE);
            LOG("bank %u streams %04x invalidate timed out; mappings quarantined", i, streams);
            return ETIMEDOUT;
        }
    }
    return 0;
}

int SFCIODart::prepare()
{
    // ACIO owner has verified disabled NHI rings and joined all workers.
    if (!lock) lock = IOLockAlloc();
    if (!lock) return ENOMEM;
    IOLockLock(lock);
    int ret = ENXIO;
    if (poisoned) { IOLockUnlock(lock); return EIO; }
    if (ready) { IOLockUnlock(lock); return EBUSY; }
    if (PAGE_SIZE != SF_DART_PAGE) { IOLockUnlock(lock); return EINVAL; }
    if (!regs[0].va) {
        if (!regs[0].mapNode("/arm-io/dart-acio1", 0) ||
            regs[0].phys!=0x501a80000ULL) {
            IOLockUnlock(lock);return ENXIO;
        }
    }
    // Validate the sole ACIO1 bank before changing its NHI stream.
    for (unsigned i = 0; i < 1; ++i) {
        if (regs[i].len < 0x300 || ((regs[i].r32(PARAMS1) >> 24) & 15) != 14 ||
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
    snapshot("before-prepare");
    visible();
    // This owner has the entire NHI DART, as Linux does after RTKit boot.
    // CIO06 observed unused reset-state TCRs enabled with no TTBR, then a
    // SID15 fault before control-ring activation. Never map/bypass that stream.
    // All PCIe-C DART state remains untouched.
    sf_cio_dart_reset_io io={this,
        [](void *ctx,uint32_t off,uint32_t *v)->int {
            *v=((SFCIODart *)ctx)->regs[0].r32(off);return 0;
        },
        [](void *ctx,uint32_t off,uint32_t v)->int {
            ((SFCIODart *)ctx)->regs[0].w32(off,v);return 0;
        },
        [](void *ctx,uint32_t streams)->int {return ((SFCIODart *)ctx)->invalidate(streams);}
    };
    ret = sf_cio_dart_reset(&io);
    if (ret) __atomic_store_n(&poisoned,true,__ATOMIC_RELEASE); // retain bank/table ownership on uncertain reset
    if (!ret) {
        for (unsigned i = 0; i < 1; ++i) {
            regs[i].w32(TTBR(1, 0), TTBR_VALID | (uint32_t)(tables[0].pa >> 12));
            regs[i].w32(TCR(1), TCR_TRANSLATE);
        }
        ret = invalidate();
    }
    __atomic_store_n(&ready,!ret,__ATOMIC_RELEASE);
    snapshot("after-prepare");
    LOG("ACIO1 SID1 prepare=%d root=0x%llx (no NHI DMA tested)", ret, tables[0].pa);
    IOLockUnlock(lock);
    return ret;
}

int SFCIODart::map(uint64_t pa, size_t bytes, uint64_t *dva, size_t alignment)
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

int SFCIODart::unmap(uint64_t dva, size_t bytes)
{
    if (!lock || !bytes || (dva & (SF_DART_PAGE - 1)) ||
        (bytes & (SF_DART_PAGE - 1)) || dva < IOVA_LO ||
        dva >= IOVA_LO + IOVA_BYTES || bytes > IOVA_LO + IOVA_BYTES - dva) return EINVAL;
    IOLockLock(lock);
    if (!ready || poisoned) { IOLockUnlock(lock); return EIO; }
    int ret = 0;
    if (sf_dart_table_unmap(&pt, dva, bytes) != bytes) {
        __atomic_store_n(&poisoned,true,__ATOMIC_RELEASE);
        ret = EIO;
    } else {
        ret = invalidate();
        if (!ret) memset(busy + (dva - IOVA_LO) / SF_DART_PAGE, 0, bytes / SF_DART_PAGE);
    }
    IOLockUnlock(lock);
    return ret;
}

uint32_t SFCIODart::faults()
{
    uint32_t value = 0;
    if (!lock) return 0;
    IOLockLock(lock);
    if (regs[0].va)
        for (unsigned i = 0; i < 1; ++i) {
            uint32_t e = regs[i].r32(ERROR);
            if (e & (1u << 31)) {
                LOG("bank %u fault %08x SID%u code=%03x", i, e, (e >> 24) & 15, e & 0xfff);
                snapshot("fault");
                value |= e;
            }
        }
    IOLockUnlock(lock);
    return value;
}

OSDefineMetaClassAndStructors(SFCIODMABuffer, IOBufferMemoryDescriptor)

SFCIODMABuffer *SFCIODMABuffer::allocate(SFCIODart *mapper, size_t bytes, size_t alignment)
{
    if (!mapper || !bytes || bytes > IOVA_BYTES || alignment < SF_DART_PAGE ||
        alignment > IOVA_BYTES || (alignment & (alignment - 1))) return NULL;
    size_t span = (bytes + SF_DART_PAGE - 1) & ~(SF_DART_PAGE - 1);
    SFCIODMABuffer *mem = OSTypeAlloc(SFCIODMABuffer);
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
        // through NHI, and cannot be reallocated while a DART might cache it.
        LOG("DMA allocation map failed %d; backing quarantined", ret);
        return NULL;
    }
    mem->dart = mapper;
    mem->dva = dva;
    mem->span = span;
    return mem;
}

int SFCIODMABuffer::retire()
{
    if (!dart) return 0;
    int ret=dart->unmap(dva,span);
    if (!ret) { dart=NULL;dva=0;span=0; }
    return ret;
}
void SFCIODMABuffer::free()
{
    if (retire()) {
        LOG("DMA buffer unmap failed; backing quarantined");
        return;
    }
    IOBufferMemoryDescriptor::free();
}

bool SFCIODart::healthy()
{
    // Also reached by the real PCI MSI primary handler through CM power.
    // Every publication is atomic; never take a sleeping mutex here. This
    // remains a cached ownership predicate, not a new hardware fault read.
    return __atomic_load_n(&ready,__ATOMIC_ACQUIRE) &&
           !__atomic_load_n(&poisoned,__ATOMIC_ACQUIRE);
}

int SFCIODart::suspend()
{
    if (!lock) return ENXIO;
    IOLockLock(lock);
    if (poisoned) { IOLockUnlock(lock);return EIO; }
    if (!ready) { IOLockUnlock(lock);return 0; }
    for (size_t i=0;i<NPAGES;++i) {
        if (busy[i]) { IOLockUnlock(lock);return EBUSY; }
    }
    // Caller has already drained NHI. Retire translations before power-off;
    // retain all tables/backing if the invalidation cannot be established.
    __atomic_store_n(&ready,false,__ATOMIC_RELEASE);
    regs[0].w32(TCR(1),0);
    int ret=invalidate();
    IOLockUnlock(lock);return ret;
}
