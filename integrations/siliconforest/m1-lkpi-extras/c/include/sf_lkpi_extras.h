/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux names the Asahi driver's G13 path and the kernel-crate modules it uses read through
 * `bindings::`, which LinuxKPI does not define. Constants and declarations only: code is in
 * sf_xarray.c (layer) and ../helpers (rust_helper_*). Included by the bindgen input
 * (bindgen/sf_bindings_extra_add.h) and by the helpers.
 */
#ifndef SF_LKPI_EXTRAS_H
#define SF_LKPI_EXTRAS_H

#include <linux/types.h>
#include <linux/workqueue.h>
#include <linux/mm_types.h>
#include <linux/sf_xarray.h>

/*
 * include/linux/workqueue.h, enum wq_flags. LinuxKPI has WQ_UNBOUND and WQ_HIGHPRI (its own
 * values) and ignores every flag: a workqueue is a taskqueue with max_active threads.
 * WQ_MEM_RECLAIM (a rescuer thread, so the queue makes progress under memory pressure) has
 * no equivalent; Linux's value, accepted and ignored.
 */
#ifndef WQ_MEM_RECLAIM
#define WQ_MEM_RECLAIM	(1 << 3)
#endif

/*
 * include/linux/mm_types_task.h: the per-mm RSS counters. LinuxKPI's mm_struct has none
 * (and on this layer every task shares one mm_struct, sf_linux_current.c), so the helpers
 * add_mm_counter() and get_mm_counter_sum() do nothing and read 0. Linux's numbering.
 */
enum {
	MM_FILEPAGES,
	MM_ANONPAGES,
	MM_SWAPENTS,
	MM_SHMEMPAGES,
	NR_MM_COUNTERS
};

/*
 * arch/arm64/include/asm/sysreg.h: CTR_EL0 is sys_reg(3, 3, 0, 0, 1), DminLine is bits
 * 19:16. The driver builds the encoding from the five fields and asks
 * read_sanitised_ftr_reg() for it (drivers/gpu/drm/asahi/pgtable.rs, dcache_line_size()).
 */
#define SF_SYS_CTR_EL0_Op0	3
#define SF_SYS_CTR_EL0_Op1	3
#define SF_SYS_CTR_EL0_CRn	0
#define SF_SYS_CTR_EL0_CRm	0
#define SF_SYS_CTR_EL0_Op2	1
#define SF_CTR_EL0_DminLine_SHIFT	16
#define SF_SYS_CTR_EL0	((SF_SYS_CTR_EL0_Op0 << 19) | (SF_SYS_CTR_EL0_Op1 << 16) | \
			 (SF_SYS_CTR_EL0_CRn << 12) | (SF_SYS_CTR_EL0_CRm << 8) | \
			 (SF_SYS_CTR_EL0_Op2 << 5))

#endif
