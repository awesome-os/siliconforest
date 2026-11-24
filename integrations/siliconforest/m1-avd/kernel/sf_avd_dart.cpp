// SPDX-License-Identifier: GPL-2.0
/*
 * IOVA space behind stream 0 of the video decoder's DART.
 *
 * Register layout, stream reset/attach sequence and page table format are ported from
 * Linux drivers/iommu/apple-dart.c and io-pgtable-dart.c (omacom/linux-aurora):
 *   Copyright (C) 2021 The Asahi Linux Contributors
 *   apple-dart.c based on arm/arm-smmu/arm-smmu.c and arm/arm-smmu-v3/arm-smmu-v3.c
 *     Copyright (C) 2013 ARM Limited, Copyright (C) 2015 ARM Limited
 *   and on exynos-iommu.c, Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 *   io-pgtable-dart.c based on io-pgtable-arm, Copyright (C) 2014 ARM Limited
 * by way of ../../m1-sound/kernel/sf_dart_fixed.cpp.
 */
#include "sf_avd_dart.h"
#include "../include/sfavd/sfavd_import_pages.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define DLOG(fmt, ...) IOLog("SFAvdDart: " fmt "\n", ##__VA_ARGS__)

#define DART_PARAMS1                         0x00
#define DART_PARAMS1_PAGE_SHIFT(v)           (((v) >> 24) & 0xf)
#define DART_T8020_STREAM_COMMAND            0x20
#define DART_T8020_STREAM_COMMAND_BUSY       (1u << 2)
#define DART_T8020_STREAM_COMMAND_INVALIDATE (1u << 20)
#define DART_T8020_STREAM_SELECT             0x34
#define DART_T8020_ERROR                     0x40
#define DART_T8020_ERROR_FLAG                (1u << 31)
#define DART_T8020_ERROR_ADDR_LO             0x50
#define DART_T8020_ERROR_ADDR_HI             0x54
#define DART_T8020_CONFIG                    0x60
#define DART_T8020_CONFIG_LOCK               (1u << 15)
#define DART_T8020_STREAMS_ENABLE            0xfc
#define DART_T8020_TCR(sid)                  (0x100 + 4 * (sid))
#define DART_T8020_TCR_TRANSLATE_ENABLE      (1u << 7)
#define DART_T8020_TTBR(sid, idx)            (0x200 + 16 * (sid) + 4 * (idx))
#define DART_T8020_TTBR_VALID                (1u << 31)
#define DART_T8020_TTBR_SHIFT                12

#define APPLE_DART1_PADDR_MASK               0x0000000ffffff000ULL
#define APPLE_DART1_PTE_PROT_SP_DIS          (1ULL << 1)
#define APPLE_DART_PTE_VALID                 (1ULL << 0)

#define PAGE_SHIFT_16K  14
#define PAGE_16K        (1UL << PAGE_SHIFT_16K)
#define PTES            (PAGE_16K / 8)
#define L2_SPAN         ((uint64_t)PTES << PAGE_SHIFT_16K)     /* 32 MiB per second-level table */

static int
invalidate(const SFMmio &regs, uint32_t sidmask)
{
	regs.w32(DART_T8020_STREAM_SELECT, sidmask);
	regs.w32(DART_T8020_STREAM_COMMAND, DART_T8020_STREAM_COMMAND_INVALIDATE);
	for (int i = 0; i < 10000; i++) {
		if (!(regs.r32(DART_T8020_STREAM_COMMAND) & DART_T8020_STREAM_COMMAND_BUSY))
			return 0;
		IODelay(10);
	}
	DLOG("TLB invalidate timed out, command=0x%08x", regs.r32(DART_T8020_STREAM_COMMAND));
	return ETIMEDOUT;
}

/* Wired, zeroed memory below 2^36 whose pages are 16 KiB aligned. */
static IOBufferMemoryDescriptor *
dmaMemory(size_t bytes, bool contiguous)
{
	IOBufferMemoryDescriptor *md = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task,
	    kIODirectionInOut | (contiguous ? kIOMemoryPhysicallyContiguous : 0), bytes, 0x0000000fffffc000ULL);
	if (md != NULL) {
		if (md->prepare() != kIOReturnSuccess) {
			md->release();
			return NULL;
		}
		bzero(md->getBytesNoCopy(), bytes);
	}
	return md;
}

int
SFAvdDart::reprogram()
{
    if (poisoned) return ENXIO;
	uint64_t l1pa = l1MD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);

	/* apple_dart_hw_reset() for this stream, then attach: TTBR, translate, flush */
	regs.w32(DART_T8020_TCR(sid), 0);
	for (int i = 0; i < 4; i++)
		regs.w32(DART_T8020_TTBR(sid, i), 0);
	regs.w32(DART_T8020_STREAMS_ENABLE, regs.r32(DART_T8020_STREAMS_ENABLE) | (1u << sid));
	regs.w32(DART_T8020_ERROR, regs.r32(DART_T8020_ERROR));
	if (invalidate(regs, 1u << sid) != 0)
		return ETIMEDOUT;
	regs.w32(DART_T8020_TTBR(sid, 0), DART_T8020_TTBR_VALID | (uint32_t)(l1pa >> DART_T8020_TTBR_SHIFT));
	regs.w32(DART_T8020_TCR(sid), DART_T8020_TCR_TRANSLATE_ENABLE);
	return invalidate(regs, 1u << sid);
}

int
SFAvdDart::attach(const char *path, uint32_t stream)
{
	uint32_t params1, config;
	int ret;

	if (attached)
		return 0;
	if (stream > 15)
		return EINVAL;
	if (sf_adt_cells(path, "clock-gates") != 0 && sf_pmgr_enable_node(path, -1, 0, true) != 0)
		return ENXIO;
	if (!regs.mapNode(path, 0))
		return ENXIO;
	params1 = regs.r32(DART_PARAMS1);
	config = regs.r32(DART_T8020_CONFIG);
	DLOG("%s at 0x%llx: params1=0x%08x config=0x%08x streams=0x%08x tcr[%u]=0x%08x ttbr0=0x%08x error=0x%08x",
	    path, regs.phys, params1, config, regs.r32(DART_T8020_STREAMS_ENABLE), stream,
	    regs.r32(DART_T8020_TCR(stream)), regs.r32(DART_T8020_TTBR(stream, 0)), regs.r32(DART_T8020_ERROR));
	if (DART_PARAMS1_PAGE_SHIFT(params1) != PAGE_SHIFT_16K) {
		DLOG("unexpected page shift %u", DART_PARAMS1_PAGE_SHIFT(params1));
		return ENOTSUP;
	}
	if (config & DART_T8020_CONFIG_LOCK) {
		DLOG("the DART is locked; it cannot be programmed");
		return EPERM;
	}

	pages = (uint32_t)(SF_AVD_DART_SPAN >> PAGE_SHIFT_16K);
	lock = IOLockAlloc();
	bitmap = (uint8_t *)IOMalloc(pages / 8);
	l1MD = dmaMemory(PAGE_16K, true);
	if (lock == NULL || bitmap == NULL || l1MD == NULL) {
		DLOG("allocation failed");
		return ENOMEM;
	}
	bzero(bitmap, pages / 8);
	l1 = (uint64_t *)l1MD->getBytesNoCopy();
	sid = stream;
	ret = reprogram();
	if (ret != 0)
		return ret;
	attached = true;
	DLOG("stream %u translating: iova 0x%llx..0x%llx, tcr=0x%08x ttbr0=0x%08x", stream, SF_AVD_DART_BASE,
	    SF_AVD_DART_BASE + SF_AVD_DART_SPAN - 1, regs.r32(DART_T8020_TCR(stream)), regs.r32(DART_T8020_TTBR(stream, 0)));
	return 0;
}

IOBufferMemoryDescriptor *
SFAvdDart::alloc(size_t bytes, uint64_t *iovaOut)
{
    if (poisoned || !bytes || bytes > SF_AVD_DART_SPAN) return NULL;
    IOBufferMemoryDescriptor *md=dmaMemory((bytes+PAGE_16K-1)&~(PAGE_16K-1),false);
    if (!md) return NULL;
    if (mapMemory(md,bytes,iovaOut)) {
        if (poisoned) {
            /* Keep the prepared reference: stale translations may still name
             * these pages. This unrecoverable reservation lasts until reboot. */
            quarantinedBytes+=(bytes+PAGE_16K-1)&~(PAGE_16K-1);
            quarantinedDescriptors++;
        } else {md->complete();md->release();}
        return NULL;
    }
    return md;
}

bool SFAvdDart::aliasesMapped(const uint64_t *sortedPages,size_t count)
{
    bool found=false;
    if (poisoned) return true;
    IOLockLock(lock);
    for (unsigned t=0;t<SF_AVD_DART_L2S&&!found;t++) if (l2[t])
        for (unsigned p=0;p<PTES;p++) {
            uint64_t entry=l2[t][p];
            if ((entry&APPLE_DART_PTE_VALID) &&
                sfavd_pages_contains(sortedPages,count,entry&APPLE_DART1_PADDR_MASK)) {found=true;break;}
        }
    IOLockUnlock(lock);
    return found;
}

int
SFAvdDart::mapMemory(IOMemoryDescriptor *md, size_t bytes, uint64_t *iovaOut)
{
    if (poisoned) return ENXIO;
    if (!md || !iovaOut || !bytes || bytes > SF_AVD_DART_SPAN) return EINVAL;
    uint32_t need = (uint32_t)((bytes + PAGE_16K - 1) >> PAGE_SHIFT_16K);
    if (md->getLength() < ((IOByteCount)need << PAGE_SHIFT_16K)) return EINVAL;
	uint32_t start = 0, run = 0;
	bool found = false;

    if (!attached || need == 0 || need > pages) return ENXIO;

	IOLockLock(lock);
	/* first fit from the hint, then from the start */
	for (int pass = 0; pass < 2 && !found; pass++) {
		uint32_t from = pass == 0 ? hint : 0, to = pass == 0 ? pages : hint + need;
		if (to > pages)
			to = pages;
		run = 0;
		for (uint32_t p = from; p < to; p++) {
			if (bitmap[p / 8] & (1u << (p % 8))) {
				run = 0;
				continue;
			}
			if (run++ == 0)
				start = p;
			if (run == need) {
				found = true;
				break;
			}
		}
	}
	if (!found) {
		IOLockUnlock(lock);
		DLOG("no address space for %u pages (%llu KiB mapped)", need, mappedBytes / 1024);
		return ENOMEM;
	}
	for (uint32_t i = 0; i < need; i++) {
		uint32_t p = start + i;
		uint64_t iova = SF_AVD_DART_BASE + ((uint64_t)p << PAGE_SHIFT_16K);
		uint32_t t = (uint32_t)((iova - SF_AVD_DART_BASE) / L2_SPAN);
		IOByteCount segmentBytes=0;
        uint64_t pa = md->getPhysicalSegment((IOByteCount)i << PAGE_SHIFT_16K, &segmentBytes, kIOMemoryMapperNone);

		if (l2[t] == NULL) {
			l2MD[t] = dmaMemory(PAGE_16K, true);
			if (l2MD[t] != NULL) {
				l2[t] = (uint64_t *)l2MD[t]->getBytesNoCopy();
				uint64_t l2pa = l2MD[t]->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
				l1[iova / L2_SPAN] = (l2pa & APPLE_DART1_PADDR_MASK) | APPLE_DART_PTE_VALID;
			}
		}
		if (l2[t] == NULL || segmentBytes < PAGE_16K || pa == 0 || (pa & (PAGE_16K - 1)) || (pa & ~APPLE_DART1_PADDR_MASK)) {
			DLOG("page %u of a %u page buffer: table %p, physical address 0x%llx", i, need, l2[t], pa);
			/* undo what was entered */
			for (uint32_t j = 0; j < i; j++) {
				uint64_t v = SF_AVD_DART_BASE + ((uint64_t)(start + j) << PAGE_SHIFT_16K);
				l2[(v - SF_AVD_DART_BASE) / L2_SPAN][(v & (L2_SPAN - 1)) >> PAGE_SHIFT_16K] = 0;
				bitmap[(start + j) / 8] &= ~(1u << ((start + j) % 8));
			}
			__asm__ volatile("dsb sy" ::: "memory");
            int flushError=invalidate(regs,1u<<sid);
            if (flushError) {
                poisoned=true;
                /* Do not reuse any part of the selected IOVA range after a
                 * failed rollback flush. The caller must retain backing. */
                for(uint32_t j=0;j<need;j++) bitmap[(start+j)/8]|=1u<<((start+j)%8);
                mappedBytes+=(uint64_t)need<<PAGE_SHIFT_16K;
                *iovaOut=SF_AVD_DART_BASE+((uint64_t)start<<PAGE_SHIFT_16K);
            }
			IOLockUnlock(lock);
			return flushError?flushError:ENOMEM;
		}
		l2[t][(iova & (L2_SPAN - 1)) >> PAGE_SHIFT_16K] =
		    (pa & APPLE_DART1_PADDR_MASK) | APPLE_DART1_PTE_PROT_SP_DIS | APPLE_DART_PTE_VALID;
		bitmap[p / 8] |= 1u << (p % 8);
	}
	__asm__ volatile("dsb sy" ::: "memory");
	/* entries that were invalid are not cached by this DART; Linux flushes on unmap only */
	hint = start + need;
	if (hint >= pages)
		hint = 0;
	mappedBytes += (uint64_t)need << PAGE_SHIFT_16K;
	if (mappedBytes > peakBytes)
		peakBytes = mappedBytes;
	IOLockUnlock(lock);
	*iovaOut = SF_AVD_DART_BASE + ((uint64_t)start << PAGE_SHIFT_16K);
	return 0;
}

int
SFAvdDart::unmapMemory(uint64_t iova,size_t bytes)
{
    if (poisoned) return ENXIO;
    if (!bytes || bytes>SF_AVD_DART_SPAN || (iova&(PAGE_16K-1)) ||
        iova<SF_AVD_DART_BASE || iova>=SF_AVD_DART_BASE+SF_AVD_DART_SPAN) return EINVAL;
    uint32_t need=(uint32_t)((bytes+PAGE_16K-1)>>PAGE_SHIFT_16K);
    uint32_t start=(uint32_t)((iova-SF_AVD_DART_BASE)>>PAGE_SHIFT_16K);
    if(need>pages-start)return EINVAL;
    IOLockLock(lock);
    for(uint32_t i=0;i<need;i++) if(!(bitmap[(start+i)/8]&(1u<<((start+i)%8)))) {
        IOLockUnlock(lock);return EINVAL;
    }
    for(uint32_t i=0;i<need;i++) {
        uint64_t v=iova+((uint64_t)i<<PAGE_SHIFT_16K);
        uint32_t t=(uint32_t)((v-SF_AVD_DART_BASE)/L2_SPAN);
        if(l2[t])l2[t][(v&(L2_SPAN-1))>>PAGE_SHIFT_16K]=0;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    int error=invalidate(regs,1u<<sid);
    if(error)poisoned=true;
    else {
        for(uint32_t i=0;i<need;i++)bitmap[(start+i)/8]&=~(1u<<((start+i)%8));
        mappedBytes-=(uint64_t)need<<PAGE_SHIFT_16K;
    }
    IOLockUnlock(lock);
    return error;
}

uint32_t
SFAvdDart::check(const char *why)
{
	if (regs.va == NULL)
		return 0;
	uint32_t err = regs.r32(DART_T8020_ERROR);
	if (!(err & DART_T8020_ERROR_FLAG))
		return 0;
	faults++;
	DLOG("FAULT (%s): error=0x%08x stream=%u code=0x%03x addr=0x%08x%08x", why ? why : "", err,
	    (err >> 24) & 0xf, err & 0xfff, regs.r32(DART_T8020_ERROR_ADDR_HI), regs.r32(DART_T8020_ERROR_ADDR_LO));
	regs.w32(DART_T8020_ERROR, err);
	return err;
}
