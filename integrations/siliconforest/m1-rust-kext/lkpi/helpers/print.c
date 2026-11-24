// SPDX-License-Identifier: GPL-2.0
/*
 * _printk for the kernel crate (new file, no Linux counterpart in rust/helpers).
 *
 * rust/kernel/print.rs calls _printk() with one of nine fixed format strings:
 *     SOH level "%s: %pA"      module name, then a core::fmt::Arguments
 *     SOH 'c'   "%pA"          pr_cont
 * On Linux lib/vsprintf.c implements %pA by calling rust_fmt_argument(). The kernel printf
 * here knows neither the SOH level prefix nor %pA, so this formats the line itself and
 * hands the finished text to the layer's console functions (sf_haiku_kernel.cpp):
 * kprintf(), which always reaches the kernel log (sfos_log -> IOLog), or for KERN_DEBUG
 * dprintf(), which only does with the fbsdkpi_debug boot-arg. Note the difference from C
 * code in this layer: LinuxKPI's printk() is printf(), i.e. dprintf(), for every level.
 */
#include <linux/kernel.h>
#include <linux/string.h>
#include <sf_linux_compat.h>

void kprintf(const char *, ...);
void dprintf(const char *, ...);

#define SF_RUST_PRINTK_LINE 512

__rust_helper int rust_helper__printk(const char *fmt, ...)
{
	char line[SF_RUST_PRINTK_LINE];
	char *p = line, *end = line + sizeof(line) - 1;
	va_list ap;

	char level = '\0';

	va_start(ap, fmt);
	if (fmt[0] == '\001' && fmt[1] != '\0') {
		level = fmt[1];
		fmt += 2;
	}
	if (strncmp(fmt, "%s: ", 4) == 0) {
		const char *module = va_arg(ap, const char *);

		p += scnprintf(p, end - p, "%s: ", module);
		fmt += 4;
	}
	if (strncmp(fmt, "%pA", 3) == 0) {
		/* writes at most up to end, returns where the text would have ended */
		p = rust_fmt_argument(p, end, va_arg(ap, const void *));
		if (p > end)
			p = end;
	}
	va_end(ap);
	*p = '\0';
	if (level == '7')
		dprintf("%s", line);
	else
		kprintf("%s", line);
	return p - line;
}
