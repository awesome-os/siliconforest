// SPDX-License-Identifier: GPL-2.0
/*
 * Linux functions the Asahi driver's G13 path (or a kernel-crate module it uses) calls as
 * `bindings::name` and that are macros, static inlines or absent in LinuxKPI. New file, no
 * counterpart in rust/helpers. Each helper says what, if anything, differs from Linux.
 */
#include <linux/capability.h>
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/shrinker.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <sf_linux_compat.h>	/* rust_fmt_argument() */
#include <sf_lkpi_extras.h>

/*
 * Shrinker (drivers/gpu/drm/asahi/mmu/residency.rs: one per VM, to release idle GPU
 * pages). LinuxKPI implements the API (linux_shrinker.c): registered shrinkers are called
 * from FreeBSD's vm_lowmem event. Nothing in this layer raises that event, so on
 * SiliconForest a registered shrinker is never called: count_objects/scan_objects do not run
 * and the driver never gives idle pages back under memory pressure. Registration,
 * unregistration and freeing behave as on Linux.
 *
 * shrinker_alloc() takes a format and arguments on Linux, for a debugfs name that does
 * not exist here; the helper takes the two fixed arguments the driver passes.
 */
__rust_helper struct shrinker *rust_helper_shrinker_alloc(unsigned int flags, const char *fmt)
{
	return shrinker_alloc(flags, fmt);
}

__rust_helper void rust_helper_shrinker_register(struct shrinker *shrinker)
{
	shrinker_register(shrinker);
}

__rust_helper void rust_helper_shrinker_free(struct shrinker *shrinker)
{
	shrinker_free(shrinker);
}

/*
 * alloc_workqueue() (drivers/gpu/drm/asahi/cleanup.rs). A format and arguments on Linux;
 * the driver passes a plain name. LinuxKPI ignores the flags; max_active 0 means its
 * default thread count, as on Linux it means the default limit.
 */
__rust_helper struct workqueue_struct *
rust_helper_alloc_workqueue_noprof(const char *name, unsigned int flags, int max_active)
{
	return alloc_workqueue(name, flags, max_active);
}

/*
 * capable() (drivers/gpu/drm/asahi/file.rs, CAP_SYS_NICE for a high-priority queue). A
 * static inline over priv_check() in LinuxKPI, which on this layer is "effective uid 0"
 * for every capability.
 */
__rust_helper bool rust_helper_capable(int cap)
{
	return capable(cap);
}

/*
 * read_sanitised_ftr_reg() (drivers/gpu/drm/asahi/pgtable.rs, for CTR_EL0.DminLine). Linux
 * returns the value merged over all CPUs; there is no such table here, so this reads the
 * register on the calling CPU. The driver wants the smallest D-cache line of any CPU. That
 * is the local value only if both T8103 core types report the same DminLine, which is
 * assumed here and has not been read on this machine (README.md, risks). Only CTR_EL0 is
 * answered; any other register is a BUG, as an unknown one is on Linux.
 */
__rust_helper u64 rust_helper_read_sanitised_ftr_reg(u32 id)
{
	u64 value;

	BUG_ON(id != SF_SYS_CTR_EL0);
	__asm__ volatile("mrs %0, ctr_el0" : "=r"(value));
	return value;
}

/*
 * seq_printf() for rust/kernel/seq_file.rs, which calls it with the one format "%pA" and
 * a core::fmt::Arguments. LinuxKPI's seq_printf() is a macro over the kernel printf,
 * which does not know %pA; rust_fmt_argument() (rust/kernel/print.rs) formats it. Other
 * formats go to LinuxKPI unchanged.
 */
__rust_helper void rust_helper_seq_printf(struct seq_file *m, const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (strcmp(fmt, "%pA") == 0) {
		const void *args = va_arg(ap, const void *);
		char stack[256];
		char *end;

		/* writes at most up to the given end, returns where the text would end */
		end = rust_fmt_argument(stack, stack + sizeof(stack), args);
		if (end <= stack + sizeof(stack)) {
			seq_write(m, stack, end - stack);
		} else {
			size_t len = end - stack;
			char *buf = kmalloc(len, GFP_KERNEL);

			if (buf != NULL) {
				end = rust_fmt_argument(buf, buf + len, args);
				seq_write(m, buf, min_t(size_t, len, end - buf));
				kfree(buf);
			}
		}
	} else {
		seq_vprintf(m, fmt, ap);
	}
	va_end(ap);
}

/*
 * Module parameters (rust/kernel/module_param.rs as patched, rust/patches/1001). There is
 * no modprobe or sysfs here; a parameter `p` of module `m` is the boot-arg `m.p=value`,
 * the spelling Linux uses for a built-in module's parameter on the kernel command line.
 * Returns the value's text, NUL terminated, or false when the boot-arg is absent.
 * PE_parse_boot_arg_str() is xnu's (pexpert/pexpert.h); unlike PE_parse_boot_argn() it
 * does not convert a numeric value itself, so 0x.., 0o.., 0b.. and negative numbers reach
 * the kernel crate's parser as on Linux.
 */
extern int PE_parse_boot_arg_str(const char *arg_string, char *arg_ptr, int size);

__rust_helper bool rust_helper_sf_boot_arg_str(const char *name, char *buf, int size)
{
	if (size <= 0)
		return false;
	buf[0] = '\0';
	return PE_parse_boot_arg_str(name, buf, size) != 0;
}
