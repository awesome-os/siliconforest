/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Linux names the kernel crate reads through `bindings::` that LinuxKPI does not define and
 * that have one obvious value on this platform. Anything needing real code is a helper
 * (lkpi/helpers), anything without an honest equivalent is left missing and the Rust that
 * uses it is compiled out.
 */
#ifndef SF_LINUX_COMPAT_H
#define SF_LINUX_COMPAT_H

#include <linux/types.h>

/* No freezer in LinuxKPI: a freezable sleep is an ordinary one. */
#ifndef TASK_FREEZABLE
#define TASK_FREEZABLE 0
#endif

/* One memory node. */
#ifndef MAX_NUMNODES
#define MAX_NUMNODES 1
#endif
#ifndef NUMA_NO_NODE
#define NUMA_NO_NODE (-1)
#endif

/* include/linux/delay.h: the longest udelay() before mdelay() must be used. */
#ifndef MAX_UDELAY_MS
#define MAX_UDELAY_MS 5
#endif

/*
 * Clock ids. CLOCK_MONOTONIC and CLOCK_REALTIME come from Haiku's time.h (as casts, which
 * bindgen cannot evaluate, hence the constants in sf_bindings_extra.h). The other two do not
 * exist there; the ids are only labels on the Rust side (kernel::time::ClockSource::ID).
 */
#ifndef CLOCK_BOOTTIME
#define CLOCK_BOOTTIME 7
#endif
#ifndef CLOCK_TAI
#define CLOCK_TAI 11
#endif

/*
 * include/linux/ioport.h. LinuxKPI's struct resource carries flags but defines few of them;
 * the values are Linux's (they are only stored and compared).
 */
#include <linux/ioport.h>
#include <linux/pci.h>	/* LinuxKPI defines IORESOURCE_MEM and IORESOURCE_IO there */
#ifndef IORESOURCE_IO
#define IORESOURCE_IO 0x00000100
#endif
#ifndef IORESOURCE_MEM
#define IORESOURCE_MEM 0x00000200
#endif
#ifndef IORESOURCE_MEM_NONPOSTED
#define IORESOURCE_MEM_NONPOSTED 0x00800000
#endif
#ifndef IORESOURCE_MUXED
#define IORESOURCE_MUXED 0x00400000
#endif
/* the roots Linux hangs every region under; here just the two descriptors (helpers/io.c) */
extern struct resource iomem_resource;
extern struct resource ioport_resource;

/*
 * include/linux/interrupt.h. LinuxKPI implements IRQF_SHARED; the rest are accepted and
 * ignored by lkpi_request_irq(). Linux's values.
 */
#include <linux/interrupt.h>
#ifndef IRQF_TRIGGER_NONE
#define IRQF_TRIGGER_NONE	0x00000000
#define IRQF_TRIGGER_RISING	0x00000001
#define IRQF_TRIGGER_FALLING	0x00000002
#define IRQF_TRIGGER_HIGH	0x00000004
#define IRQF_TRIGGER_LOW	0x00000008
#endif
#ifndef IRQF_ONESHOT
#define IRQF_PROBE_SHARED	0x00000100
#define IRQF_TIMER		0x00000200
#define IRQF_PERCPU		0x00000400
#define IRQF_IRQPOLL		0x00001000
#define IRQF_ONESHOT		0x00002000
#define IRQF_NO_SUSPEND		0x00004000
#define IRQF_FORCE_RESUME	0x00008000
#define IRQF_NO_THREAD		0x00010000
#define IRQF_EARLY_RESUME	0x00020000
#define IRQF_COND_SUSPEND	0x00040000
#define IRQF_NO_AUTOEN		0x00080000
#define IRQF_NO_DEBUG		0x00100000
#endif

/* include/linux/dma-mapping.h: 6.18's flag for mapping MMIO; no LinuxKPI caller looks at it */
#ifndef DMA_ATTR_MMIO
#define DMA_ATTR_MMIO (1UL << 10)
#endif

/* lib/vsprintf.c on Linux declares this for %pA; defined by rust/kernel/print.rs. */
char *rust_fmt_argument(char *buf, char *end, const void *ptr);

#endif
