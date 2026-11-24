// SPDX-License-Identifier: GPL-2.0
/*
 * Linux functions the kernel crate calls as `bindings::name` that are exported functions on
 * Linux but macros or static inlines in LinuxKPI, so bindgen cannot see them. Each gets a
 * rust_helper_ wrapper; rust/bindings/lib.rs then exposes it under the plain name, exactly
 * as it does for Linux's own helpers.
 */
#include <linux/completion.h>
#include <linux/kernel.h>
#include <linux/kstrtox.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>

__rust_helper void rust_helper_mutex_unlock(struct mutex *lock)
{
	mutex_unlock(lock);
}

__rust_helper void rust_helper_complete(struct completion *x)
{
	complete(x);
}

__rust_helper void rust_helper_complete_all(struct completion *x)
{
	complete_all(x);
}

__rust_helper void rust_helper_wait_for_completion(struct completion *x)
{
	wait_for_completion(x);
}

__rust_helper unsigned long rust_helper_wait_for_completion_timeout(struct completion *x,
								    unsigned long timeout)
{
	return wait_for_completion_timeout(x, timeout);
}

__rust_helper bool rust_helper_try_wait_for_completion(struct completion *x)
{
	return try_wait_for_completion(x);
}

__rust_helper void rust_helper_synchronize_rcu(void)
{
	synchronize_rcu();
}

__rust_helper int rust_helper_kstrtobool(const char *s, bool *res)
{
	return kstrtobool(s, res);
}

/*
 * rust/kernel/drm/gem/mod.rs puts noop_llseek into the driver's file_operations. LinuxKPI's
 * is a static inline that reads file->_file->f_offset, and a /dev/dri file on SiliconForest has
 * no _file (sf_drm_cdev.c); SFDRMDri never calls llseek. (From m1-drm72/rust-helpers.)
 */
#include <linux/fs.h>

__rust_helper loff_t rust_helper_noop_llseek(struct file *file, loff_t offset, int whence)
{
	return 0;
}

/* static inlines in LinuxKPI's linux/device.h, exported functions on Linux */
#include <linux/device.h>

__rust_helper struct device *rust_helper_get_device(struct device *dev)
{
	return get_device(dev);
}

__rust_helper void rust_helper_put_device(struct device *dev)
{
	put_device(dev);
}
