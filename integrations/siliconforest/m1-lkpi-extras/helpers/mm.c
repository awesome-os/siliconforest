// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/mm.c, for the part of rust/kernel/mm.rs that is
 * built here (rust/patches/1002): Mm/MmWithUser reference counting and the RSS counters.
 *
 * LinuxKPI's mm_struct is two reference counts and a lock. On this layer every task's
 * `mm` is the one static linux_kernel_mm (pd/lkpi/sf_linux_current.c), which is never
 * freed, so the counts are bookkeeping only. There are no per-mm RSS counters and nothing
 * like the OOM killer that would read them: add_mm_counter() does nothing and
 * get_mm_counter_sum() reads 0.
 *
 * What is lost: the driver adds a client's held GPU memory to its creator's shmem RSS
 * (drivers/gpu/drm/asahi/memacct.rs, `oom_attribution`). Here that accounting is a no-op;
 * the driver's own limits (mem_limit_pct, client_mem_limit_pct) do not depend on it.
 */
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/sched.h>
#include <sf_lkpi_extras.h>

__rust_helper void rust_helper_mmgrab(struct mm_struct *mm)
{
	mmgrab(mm);
}

__rust_helper void rust_helper_mmdrop(struct mm_struct *mm)
{
	mmdrop(mm);
}

__rust_helper void rust_helper_mmget(struct mm_struct *mm)
{
	atomic_inc(&mm->mm_users);
}

__rust_helper bool rust_helper_mmget_not_zero(struct mm_struct *mm)
{
	return mmget_not_zero(mm);
}

/* an exported function on Linux, a static inline in LinuxKPI */
__rust_helper void rust_helper_mmput(struct mm_struct *mm)
{
	mmput(mm);
}

__rust_helper void rust_helper_add_mm_counter(struct mm_struct *mm, int member, long value)
{
}

__rust_helper unsigned long rust_helper_get_mm_counter_sum(struct mm_struct *mm, int member)
{
	return 0;
}
