// SPDX-License-Identifier: GPL-2.0
/*
 * SiliconForest replacement for rust/helpers/irq.c, plus the interrupt calls kernel::irq makes
 * that are static inlines over lkpi_request_irq() and friends in LinuxKPI. For a platform
 * device that ends in sf_platform_request_irq() (pd/lkpi/sf_linux_platform.c). LinuxKPI
 * knows IRQF_SHARED only; the other IRQF_* bits (sf_linux_compat.h, Linux's values) are
 * accepted and ignored.
 */
#include <linux/interrupt.h>
#include <linux/hardirq.h>

__rust_helper int rust_helper_request_irq(unsigned int irq, irq_handler_t handler,
					  unsigned long flags, const char *name, void *dev)
{
	return request_irq(irq, handler, flags, name, dev);
}

__rust_helper int rust_helper_request_threaded_irq(unsigned int irq, irq_handler_t handler,
						   irq_handler_t thread_fn, unsigned long flags,
						   const char *name, void *dev)
{
	return request_threaded_irq(irq, handler, thread_fn, flags, name, dev);
}

__rust_helper const void *rust_helper_free_irq(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
	return NULL;
}

__rust_helper void rust_helper_synchronize_irq(unsigned int irq)
{
	synchronize_irq(irq);
}

__rust_helper void rust_helper_disable_irq_nosync(unsigned int irq)
{
	disable_irq_nosync(irq);
}

__rust_helper void rust_helper_enable_irq(unsigned int irq)
{
	enable_irq(irq);
}
