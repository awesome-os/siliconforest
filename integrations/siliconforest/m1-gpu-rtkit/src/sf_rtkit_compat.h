/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * sf_rtkit_compat.h - what mailbox.c / rtkit.c / rtkit-crashlog.c use from
 * Linux that SiliconForest's LinuxKPI layer (fbsdkpi) lacks or implements with
 * different behaviour. Included by those files after their Linux headers.
 * Each block names the gap; README.md has the evidence from the layer.
 */
#ifndef SF_RTKIT_COMPAT_H
#define SF_RTKIT_COMPAT_H

#include <linux/types.h>
#include <linux/compat.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/kfifo.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/workqueue.h>
#include <asm/barrier.h>

#include "sf_rtkit_platform.h"
#include "sf_rtkit_iomem.h"

/* LinuxKPI aliases this to ordinary memcpy, which may issue unaligned device
 * loads. ACIO SRAM SYSLOG records actually triggered that data abort. */
#undef memcpy_fromio
#define memcpy_fromio(dst,src,bytes) sf_rtkit_memcpy_fromio((dst),(src),(bytes))

/*
 * Logging. The layer's dev_err() is device_printf(dev->bsddev, ...), which
 * dereferences bsddev (NULL for a struct device nobody attached to newbus),
 * and its dev_dbg() is empty. Here every dev_*() of the port goes to one
 * printer that uses the kobject name and the platform's log line; dev_dbg()
 * prints when sf_rtkit_debug is set (boot-arg sfrtkit_debug=1, or the probe).
 * dev_*_ratelimited() expand to dev_*() and so come here too.
 */
extern int sf_rtkit_debug;
void sf_rtkit_printk(const struct device *dev, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#undef dev_err
#undef dev_warn
#undef dev_info
#undef dev_dbg
#define	dev_err(dev, fmt, ...)	sf_rtkit_printk((dev), fmt, ##__VA_ARGS__)
#define	dev_warn(dev, fmt, ...)	sf_rtkit_printk((dev), fmt, ##__VA_ARGS__)
#define	dev_info(dev, fmt, ...)	sf_rtkit_printk((dev), fmt, ##__VA_ARGS__)
#define	dev_dbg(dev, fmt, ...)	do {					\
	if (sf_rtkit_debug)						\
		sf_rtkit_printk((dev), fmt, ##__VA_ARGS__);		\
} while (0)

/*
 * A struct device for something that is not a LinuxKPI platform device (the
 * mailbox provider, a test consumer): kobject name, devres list, parent, and
 * the of_node mirroring `dt_entry` (an IORegistryEntry, may be NULL).
 */
struct device *sf_rtkit_device_create(const char *name, void *dt_entry);

/*
 * Runtime PM. The layer's pm_runtime_resume_and_get() returns 1 (Linux: 0 on
 * success, negative errno), which mailbox.c would take for a failure. Devices
 * never runtime-suspend here; the power domain is the consumer's (SF-A A2).
 * pm_runtime_set_suspended(), pm_runtime_barrier() and
 * devm_pm_runtime_enable() do not exist in the layer.
 */
static inline int
sf_rtkit_pm_runtime_resume_and_get(struct device *dev)
{
	int ret = pm_runtime_resume_and_get(dev);

	return (ret < 0 ? ret : 0);
}
#define	pm_runtime_resume_and_get(dev)	sf_rtkit_pm_runtime_resume_and_get(dev)
#define	pm_runtime_barrier(dev)		((void)(dev))

/* linux/iopoll.h of the layer has read_poll_timeout_atomic() but not the readl form. */
#ifndef readl_poll_timeout_atomic
#define	readl_poll_timeout_atomic(addr, val, cond, delay_us, timeout_us) \
	read_poll_timeout_atomic(readl, val, cond, delay_us, timeout_us, false, addr)
#endif

/* Barriers between a mailbox MMIO access and the shared buffers. */
#ifndef dma_rmb
#define	dma_rmb()	rmb()
#endif
#ifndef dma_wmb
#define	dma_wmb()	wmb()
#endif

/*
 * Interrupts. In the layer enable_irq()/disable_irq() look the number up
 * among PCI devices and do nothing for anything else, disable_irq_nosync()
 * is disable_irq(), synchronize_irq() is an empty _intr_drain(), and there is
 * no IRQF_NO_AUTOEN. The mailbox interrupts therefore go through the platform
 * interface; the numbers mailbox.c holds are its handles.
 */
int sf_rtkit_request_irq(int irq, irq_handler_t handler, const char *name, void *data);
void sf_rtkit_free_irq(int irq, void *data);

#undef synchronize_irq
#define	enable_irq(irq)			sf_rtkit_platform_irq_enable(irq)
#define	disable_irq(irq)		sf_rtkit_platform_irq_disable(irq)
#define	disable_irq_nosync(irq)		sf_rtkit_platform_irq_disable_nosync(irq)
#define	synchronize_irq(irq)		sf_rtkit_platform_irq_synchronize(irq)

/*
 * of_machine_is_compatible() is absent. Apple's device tree has no
 * "apple,tXXXX" root compatible; the SoC shows as "arm-io,tXXXX" on /arm-io.
 */
bool sf_rtkit_of_machine_is_compatible(const char *compat);
#define	of_machine_is_compatible(c)	sf_rtkit_of_machine_is_compatible(c)

/*
 * alloc_ordered_workqueue() is a two-argument macro in the layer (no printf
 * arguments) and ignores its flags: WQ_HIGHPRI and WQ_MEM_RECLAIM have no
 * effect. One thread, in order, is what the RTKit receive path needs.
 */
#undef alloc_ordered_workqueue
#define	alloc_ordered_workqueue(fmt, flags, ...) \
	linux_create_workqueue_common("rtkit", 1)

/*
 * kfifo. Linux's is safe for one producer and one consumer without a lock,
 * which rtkit.c relies on (interrupt side puts, a work item gets). The
 * layer's keeps a shared `count` that both sides modify, and has no
 * kfifo_reset(). These use the same storage and leave one slot unused, so
 * the 64-entry acknowledgment ring holds 63.
 */
#undef kfifo_put
#undef kfifo_get
#define	kfifo_put(_kf, _e)						\
({									\
	size_t _last = (_kf)->last;					\
	size_t _next = _last + 1 >= (_kf)->total ? 0 : _last + 1;	\
	bool _rc = _next != READ_ONCE((_kf)->first);			\
									\
	if (_rc) {							\
		(_kf)->head[_last] = (_e);				\
		smp_wmb();						\
		WRITE_ONCE((_kf)->last, _next);				\
	}								\
	_rc;								\
})
#define	kfifo_get(_kf, _e)						\
({									\
	size_t _first = (_kf)->first;					\
	bool _rc = _first != READ_ONCE((_kf)->last);			\
									\
	if (_rc) {							\
		smp_rmb();						\
		*(_e) = (_kf)->head[_first];				\
		WRITE_ONCE((_kf)->first,				\
		    _first + 1 >= (_kf)->total ? 0 : _first + 1);	\
	}								\
	_rc;								\
})
/* Only with both sides stopped (rtkit.c: receive detached, ack work cancelled). */
#define	kfifo_reset(_kf)						\
({									\
	(_kf)->count = 0;						\
	(_kf)->first = 0;						\
	(_kf)->last = 0;						\
})

/*
 * Addition to the mailbox API (mailbox.c): names the mailbox of a consumer,
 * standing in for `mboxes = <&node>` at `index`. `path` is a device-tree path
 * ("/arm-io/gfx-asc") or a node name under /arm-io. Optional: without it
 * apple_mbox_get() asks sf_rtkit_platform_mbox_for_consumer().
 */
int sf_apple_mbox_bind(struct device *consumer, int index, const char *path);

/*
 * A thread that enters LinuxKPI code from outside (an IOKit start routine, the
 * platform's interrupt thread) needs its `current` set up first.
 */
#define	sf_rtkit_enter()	linux_set_current(curthread)

#endif
