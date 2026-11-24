/* SPDX-License-Identifier: GPL-2.0 */
/*
 * The video decoder's own DART (dart-avd, an Apple T8020-type DART), stream 0: an IOVA
 * space of 2 GiB with first-fit allocation in 16 KiB pages, second-level tables made on
 * demand. Every buffer the decoder touches is mapped here: its instruction FIFO, the
 * codecs' work buffers, coded slices and the decoded pictures.
 *
 * ../../m1-sound/kernel/sf_dart_fixed.cpp is the one-window form of the same code.
 */
#ifndef _SF_AVD_DART_H
#define _SF_AVD_DART_H

#include "sf_soc.h"
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOLocks.h>

#define SF_AVD_DART_BASE   0x04000000ULL
#define SF_AVD_DART_SPAN   0x80000000ULL                      /* 2 GiB */
#define SF_AVD_DART_L2S    (SF_AVD_DART_SPAN >> 25)           /* 32 MiB per second-level table */

struct SFAvdDart {
	SFMmio                    regs;
	IOBufferMemoryDescriptor *l1MD, *l2MD[SF_AVD_DART_L2S];
	uint64_t                 *l1, *l2[SF_AVD_DART_L2S];
	uint8_t                  *bitmap;      /* one bit per 16 KiB page of the span */
	uint32_t                  pages, hint;
	IOLock                   *lock;
	uint32_t                  sid;
	uint32_t                  faults;
	uint64_t                  mappedBytes, peakBytes;
	bool                      attached;
    bool poisoned;
    uint64_t quarantinedBytes;
    uint32_t quarantinedDescriptors;

	SFAvdDart() : l1MD(NULL), l1(NULL), bitmap(NULL), pages(0), hint(0), lock(NULL), sid(0), faults(0), mappedBytes(0),
	    peakBytes(0), attached(false), poisoned(false), quarantinedBytes(0), quarantinedDescriptors(0)
	{
		for (unsigned i = 0; i < SF_AVD_DART_L2S; i++) {
			l2MD[i] = NULL;
			l2[i] = NULL;
		}
	}

	/* Powers the DART, resets stream `sid` and makes it translate through an empty table. */
	int attach(const char *path, uint32_t sid);
	/* Programs the stream again with the existing tables (after a reset of the block). */
	int reprogram();
	/*
	 * Wired zeroed memory of `bytes` (rounded up to 16 KiB pages) mapped for the device.
	 * NULL when memory or address space ran out.
	 */
	IOBufferMemoryDescriptor *alloc(size_t bytes, uint64_t *iova);
    /* Already-prepared memory, ownership stays with caller on failure. Success
     * maps unused IOVA; unmapMemory() must succeed before the caller releases its descriptor. */
    int mapMemory(IOMemoryDescriptor *md, size_t bytes, uint64_t *iova);
    bool aliasesMapped(const uint64_t *sortedPages, size_t count);
    /* Only removes translations. Caller releases backing AFTER success.
     * Invalidation failure poisons all further DMA until reboot. */
    int unmapMemory(uint64_t iova, size_t bytes);
	/* Logs and clears a pending fault; returns the raw error register (0: none). */
	uint32_t check(const char *why);
};

#endif
