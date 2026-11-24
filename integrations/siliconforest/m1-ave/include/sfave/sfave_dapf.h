/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef SFAVE_DAPF_H
#define SFAVE_DAPF_H
#include "sfave_preload.h"
/* Non-posted device access, with real 64-bit accesses for range registers.
 * Caller has powered both AVE DARTs, kept the CPU stopped, and serialized
 * ownership. No reset, mapping, power or DMA allocation is done here. */
struct sfave_dapf_io {
 void *context;
 int (*read32)(void *,unsigned,uint32_t,uint32_t *);
 int (*write32)(void *,unsigned,uint32_t,uint32_t);
 int (*read64)(void *,unsigned,uint32_t,uint64_t *);
 int (*write64)(void *,unsigned,uint32_t,uint64_t);
};
enum {SFAVE_DAPF_CPUDART=0,SFAVE_DAPF_FILTER=1};
/* window comes from live dart-ave filter-data-instance-0. No per-machine
 * physical firmware address or extracted Acis data is compiled into the port.
 * Failure after a write requires platform quarantine, not DMA release/retry. */
int sfave_program_dapf(const struct sfave_preload *,uint64_t window_start,
 uint64_t window_end,int cpu_stopped,const struct sfave_dapf_io *);
#endif
