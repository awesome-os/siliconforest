// SPDX-License-Identifier: GPL-2.0
/*
 * Fixed DMA window behind one stream of an Apple T8020-type DART.
 *
 * Register layout, stream reset/attach sequence and page table format are ported from
 * Linux drivers/iommu/apple-dart.c and io-pgtable-dart.c (omacom/linux-aurora):
 *   Copyright (C) 2021 The Asahi Linux Contributors
 *   apple-dart.c based on arm/arm-smmu/arm-smmu.c and arm/arm-smmu-v3/arm-smmu-v3.c
 *     Copyright (C) 2013 ARM Limited, Copyright (C) 2015 ARM Limited
 *   and on exynos-iommu.c, Copyright (c) 2011,2016 Samsung Electronics Co., Ltd.
 *   io-pgtable-dart.c based on io-pgtable-arm, Copyright (C) 2014 ARM Limited
 */
#include "sf_dart_fixed.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define DLOG(fmt, ...) IOLog("SFDartFixed: " fmt "\n", ##__VA_ARGS__)

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
	for (int i = 0; i < 1000; i++) {
		if (!(regs.r32(DART_T8020_STREAM_COMMAND) & DART_T8020_STREAM_COMMAND_BUSY))
			return 0;
		IODelay(100);
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
		md->prepare();
		bzero(md->getBytesNoCopy(), bytes);
	}
	return md;
}

int
SFDartFixed::attach(const char *path, uint32_t stream, uint64_t at, uint32_t bytes)
{
	uint64_t l1pa, l2pa;
	uint64_t *l1, *l2;
	uint32_t params1, config;

	if (bytes == 0 || (bytes & (PAGE_16K - 1)) || (at & (PAGE_16K - 1)) || stream > 15 ||
	    at / L2_SPAN != (at + bytes - 1) / L2_SPAN || at + bytes > (1ULL << 32))
		return EINVAL;
	if (sf_pmgr_enable_node(path, -1, 0, true) != 0)
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

	bufMD = dmaMemory(bytes, false);
	l1MD = dmaMemory(PAGE_16K, true);
	l2MD = dmaMemory(PAGE_16K, true);
	if (bufMD == NULL || l1MD == NULL || l2MD == NULL) {
		DLOG("allocation failed");
		return ENOMEM;
	}
	buf = (uint8_t *)bufMD->getBytesNoCopy();
	l1 = (uint64_t *)l1MD->getBytesNoCopy();
	l2 = (uint64_t *)l2MD->getBytesNoCopy();
	l1pa = l1MD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
	l2pa = l2MD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);

	for (uint32_t off = 0; off < bytes; off += PAGE_16K) {
		uint64_t pa = bufMD->getPhysicalSegment(off, NULL, kIOMemoryMapperNone);
		if (pa == 0 || (pa & (PAGE_16K - 1)) || (pa & ~APPLE_DART1_PADDR_MASK)) {
			DLOG("page at 0x%x has unusable physical address 0x%llx", off, pa);
			return EFAULT;
		}
		l2[((at + off) & (L2_SPAN - 1)) >> PAGE_SHIFT_16K] =
		    (pa & APPLE_DART1_PADDR_MASK) | APPLE_DART1_PTE_PROT_SP_DIS | APPLE_DART_PTE_VALID;
	}
	l1[at / L2_SPAN] = (l2pa & APPLE_DART1_PADDR_MASK) | APPLE_DART_PTE_VALID;
	__asm__ volatile("dsb sy" ::: "memory");

	/* apple_dart_hw_reset() for this stream, then attach: TTBR, translate, flush */
	regs.w32(DART_T8020_TCR(stream), 0);
	for (int i = 0; i < 4; i++)
		regs.w32(DART_T8020_TTBR(stream, i), 0);
	regs.w32(DART_T8020_STREAMS_ENABLE, regs.r32(DART_T8020_STREAMS_ENABLE) | (1u << stream));
	regs.w32(DART_T8020_ERROR, regs.r32(DART_T8020_ERROR));
	if (invalidate(regs, 1u << stream) != 0)
		return ETIMEDOUT;
	regs.w32(DART_T8020_TTBR(stream, 0), DART_T8020_TTBR_VALID | (uint32_t)(l1pa >> DART_T8020_TTBR_SHIFT));
	regs.w32(DART_T8020_TCR(stream), DART_T8020_TCR_TRANSLATE_ENABLE);
	if (invalidate(regs, 1u << stream) != 0)
		return ETIMEDOUT;

	iova = at;
	size = bytes;
	sid = stream;
	DLOG("stream %u translating: iova 0x%llx..0x%llx, first page pa 0x%llx, tcr=0x%08x ttbr0=0x%08x",
	    stream, at, at + bytes - 1, bufMD->getPhysicalSegment(0, NULL, kIOMemoryMapperNone),
	    regs.r32(DART_T8020_TCR(stream)), regs.r32(DART_T8020_TTBR(stream, 0)));
	return 0;
}

uint32_t
SFDartFixed::check(const char *why)
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
