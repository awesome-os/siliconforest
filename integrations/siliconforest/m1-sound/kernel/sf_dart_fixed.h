/* SPDX-License-Identifier: GPL-2.0 */
/*
 * One fixed DMA window behind one stream of an Apple T8020-type DART: a wired kernel
 * buffer mapped page by page at a fixed IOVA, for devices whose DMA memory is a ring
 * allocated once (audio DMA, small control structures). Per-buffer mapping for a DART
 * shared with other drivers is ../../m1-apcie-host/dart/sf_dart_map.cpp.
 *
 * Only the named stream is programmed; the DART's other streams are left alone.
 */
#ifndef _SF_DART_FIXED_H
#define _SF_DART_FIXED_H

#include "sf_soc.h"
#include <IOKit/IOBufferMemoryDescriptor.h>

struct SFDartFixed {
	SFMmio                    regs;
	IOBufferMemoryDescriptor *bufMD, *l1MD, *l2MD;
	uint8_t                  *buf;      /* kernel address of the window */
	uint64_t                  iova;
	uint32_t                  size;
	uint32_t                  sid;
	uint32_t                  faults;   /* faults seen by check() */

	SFDartFixed() : bufMD(NULL), l1MD(NULL), l2MD(NULL), buf(NULL), iova(0), size(0), sid(0), faults(0) {}

	/*
	 * Powers the DART (the node's clock-gates), allocates `size` bytes (a multiple of
	 * 16 KiB inside one 32 MiB span below 4 GiB) and maps them at `iova` for
	 * stream `sid`. 0 or an errno value.
	 */
	int attach(const char *path, uint32_t sid, uint64_t iova, uint32_t size);
	/* Logs and clears a pending fault; returns the raw error register (0: none). */
	uint32_t check(const char *why);
};

#endif
