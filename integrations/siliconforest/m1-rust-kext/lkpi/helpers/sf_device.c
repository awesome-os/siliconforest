// SPDX-License-Identifier: GPL-2.0
/*
 * _dev_printk for kernel::device (new file). Like _printk (print.c): the Rust side passes a
 * level string and the fixed format "%pA"; lib/vsprintf.c is not here to interpret it. The
 * line is "<driver name> <device name>: <text>", the device name being the kobject's (for a platform device
 * the device-tree path, as sf_linux_platform.c names it).
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <sf_linux_compat.h>

void kprintf(const char *, ...);
void dprintf(const char *, ...);

__rust_helper void rust_helper__dev_printk(const char *level, const struct device *dev,
					   const char *fmt, ...)
{
	char line[512];
	char *p = line, *end = line + sizeof(line) - 1;
	const char *name = dev != NULL ? kobject_name(&dev->kobj) : NULL;
	va_list ap;

	/* "<driver> <device>: ", as Linux's dev_printk() prints it */
	p += scnprintf(p, end - p, "%s %s: ",
	    dev != NULL && dev->driver != NULL && dev->driver->name != NULL ? dev->driver->name : "(no driver)",
	    name != NULL ? name : "(no device)");
	va_start(ap, fmt);
	if (strncmp(fmt, "%pA", 3) == 0) {
		p = rust_fmt_argument(p, end, va_arg(ap, const void *));
		if (p > end)
			p = end;
	}
	va_end(ap);
	/* a format that is not "%pA" (only C callers have one) is printed as it is */
	if (strncmp(fmt, "%pA", 3) != 0)
		p += scnprintf(p, end - p, "%s", fmt);
	*p = '\0';
	if (level != NULL && level[0] == '\001' && level[1] == '7')
		dprintf("%s", line);
	else
		kprintf("%s", line);
}

/*
 * One line through the helper above from C, for a kext that wants its boot log to show
 * that the dev_info! path prints (SFAgx). Default visibility so the prelinked Rust object
 * can export it; the helpers themselves are hidden.
 */
__attribute__((visibility("default"))) void sf_dev_printk_selftest(const struct device *dev)
{
	rust_helper__dev_printk("\0016", dev, "self-test\n");
}
