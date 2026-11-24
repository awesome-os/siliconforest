// SPDX-License-Identifier: GPL-2.0
/*
 * Two helpers the unmodified 7.2 DRM wrappers call and ../m1-rust-kext/lkpi/helpers does
 * not have yet. They are not DRM API; they are here so that the wrappers can be measured
 * without waiting for that directory, and belong there (atomic.c, and a future fs.c).
 *
 *  atomic_sub    rust/kernel/drm/sched.rs (credit and score accounting when a job is
 *                cancelled). Linux's generated rust/helpers/atomic.c has it; the SiliconForest
 *                replacement only carries the operations the kernel crate's own atomics use.
 *  noop_llseek   rust/kernel/drm/gem/mod.rs puts it into the driver's file_operations
 *                (DRM_GEM_FOPS). It is a static inline in LinuxKPI's linux/fs.h, so bindgen
 *                emits nothing for it. LinuxKPI's version reads file->_file->f_offset, and a
 *                /dev/dri file on SiliconForest has no _file (sf_drm_cdev.c); SFDRMDri never
 *                calls llseek, so this one answers 0 instead of dereferencing it.
 */
#include <linux/atomic.h>
#include <linux/fs.h>

__rust_helper void rust_helper_atomic_sub(int i, atomic_t *v)
{
	atomic_sub(i, v);
}

__rust_helper loff_t rust_helper_noop_llseek(struct file *file, loff_t offset, int whence)
{
	return 0;
}
