/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * sf_rtkit_platform.h - what the Apple mailbox/RTKit port needs from Darwin and
 * cannot get from the LinuxKPI layer (fbsdkpi). Plain C types only: the C side
 * (mailbox.c, sf_rtkit_compat.c) never sees an xnu header and the C++ side
 * (SFRtkitPlatform.cpp) never sees a LinuxKPI one.
 *
 * Why each call exists is in README.md ("LinuxKPI gaps"). In short:
 *
 *   node lookup   Apple's device tree has no "mboxes" phandle from the consumer
 *                 to its mailbox and no "interrupt-names"; the mailbox is the
 *                 ASC wrapper node itself (/arm-io/gfx-asc for the GPU).
 *   ioremap       on-SoC registers must be mapped non-posted (kIOMapInhibitCache).
 *                 The layer's ioremap() happens to do that today; the port does
 *                 not rely on it.
 *   interrupts    the layer's enable_irq()/disable_irq() do nothing for a
 *                 device-tree interrupt and it has no IRQF_NO_AUTOEN. The
 *                 mailbox's send-empty line is level triggered and asserted
 *                 whenever the FIFO is empty, so it has to be maskable.
 *   power         read-only view of the PMGR power state of the block, so a
 *                 probe can refuse to touch an unpowered register window.
 *
 * Every function may sleep and must be called from thread context, except
 * where noted. One implementation exists: SFRtkitPlatform.cpp (IOKit).
 */
#ifndef SF_RTKIT_PLATFORM_H
#define SF_RTKIT_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Offsets inside an ASC wrapper ("iop,ascwrap-v4") register window, reg 0. */
#define SF_RTKIT_ASC_CPU_CONTROL	0x44	/* bit 4: RUN (m1n1 asc.c, Linux ASC_CPU_CONTROL) */
#define SF_RTKIT_ASC_CPU_CONTROL_RUN	0x10
#define SF_RTKIT_ASC_CPU_STATUS		0x48	/* m1n1 hw/asc.py: bit 1 STOPPED, bit 5 IDLE */
#define SF_RTKIT_ASC_MBOX_OFFSET	0x8000	/* Linux mbox@206408000 = gfx-asc reg 0 + 0x8000 */
#define SF_RTKIT_ASC_MBOX_SIZE		0x4000

#define SF_RTKIT_NAME_MAX		32
#define SF_RTKIT_PATH_MAX		96

/* One mailbox, as mailbox.c needs it described. */
struct sf_rtkit_mbox_res {
	char		path[SF_RTKIT_PATH_MAX];	/* device-tree path of the ASC node */
	char		name[SF_RTKIT_NAME_MAX];	/* its name, "gfx-asc" */
	void		*node;		/* IORegistryEntry in gIODTPlane, retained for the kext's life */
	/* Linux compatible: ASCv4 or ACIO M3v2 (direct reg0, without ASC offset). */
	const char	*linux_compatible;
	uint64_t	asc_phys;	/* reg 0, with the parents' ranges applied */
	uint64_t	asc_size;
	uint64_t	mbox_phys;	/* ASC: asc_phys + 0x8000; ACIO M3v2: asc_phys */
	uint64_t	mbox_size;
	/* AIC numbers in the order the node lists them, and by role (sorted: see the .cpp) */
	uint32_t	aic_listed[4];
	uint32_t	aic_send_empty;
	uint32_t	aic_send_not_empty;
	uint32_t	aic_recv_empty;
	uint32_t	aic_recv_not_empty;
	/*
	 * Interrupt handles for sf_rtkit_platform_irq_*(), >= 0. irq_send_empty is
	 * -1 when the platform polls instead of taking interrupts (boot-arg
	 * sfrtkit_irq=0): mailbox.c then polls the A2I control register, as it
	 * does on Linux for a mailbox without that line.
	 */
	int		irq_send_empty;
	int		irq_recv_not_empty;
};

/*
 * Must be called once, before anything else, by the kext's IOService: `host`
 * is that IOService. The mailbox interrupts are described on it and attached
 * to it (the device-tree node itself is a bare IOService nobody has started).
 * Returns 0 or a negative errno.
 */
int sf_rtkit_platform_init(void *host);

/* Log line prefix, e.g. "SFRtkitProbe: ". The string must stay valid. */
void sf_rtkit_platform_set_log_prefix(const char *prefix);
/* One complete line (no newline needed) to the kernel log, behind the prefix. Thread context. */
void sf_rtkit_platform_log(const char *line);

/*
 * Resolves a mailbox. `path` is a device-tree path ("/arm-io/gfx-asc") or a
 * bare node name under /arm-io ("gfx-asc"). Reads the node, checks that it is
 * an iop,ascwrap-v4 with 4 interrupts, and allocates the two interrupt
 * handles (nothing is registered with the interrupt controller until
 * sf_rtkit_platform_irq_request). No register is mapped or touched.
 * Returns 0, -ENOENT (no such node), -ENODEV (not an ASC mailbox this port
 * knows), -EINVAL (reg/interrupts not as expected).
 */
int sf_rtkit_platform_mbox_resolve(const char *path, struct sf_rtkit_mbox_res *res);

/*
 * The mailbox of a consumer device, standing in for Linux's
 * `mboxes = <&phandle>`: `consumer_node` is the consumer's IORegistryEntry
 * (struct device_node::sf_entry) or NULL, `index` the mboxes index. Writes a
 * path for sf_rtkit_platform_mbox_resolve. Interim rule, see README: a node
 * that is itself an ASC wrapper is its own mailbox; /arm-io/sgx and a
 * consumer without a node get /arm-io/gfx-asc for index 0.
 */
int sf_rtkit_platform_mbox_for_consumer(void *consumer_node, int index, char *path, size_t len);

/* Non-posted (device, nGnRnE) mapping of [phys, phys + size). NULL on failure. */
void *sf_rtkit_platform_ioremap(uint64_t phys, uint64_t size);
void sf_rtkit_platform_iounmap(void *va);

/*
 * Interrupts. A handle starts out without a handler and masked, as a Linux
 * interrupt requested with IRQF_NO_AUTOEN. enable/disable nest like Linux's
 * (a depth count, starting at 1). The handler runs on one kernel thread shared
 * by all handles of the kext (an IOWorkLoop), never at interrupt level and
 * never concurrently with another handler, with the line masked until it
 * returns. It returns nonzero when it handled the interrupt.
 */
typedef int (*sf_rtkit_irq_fn)(void *arg);
int sf_rtkit_platform_irq_request(int irq, sf_rtkit_irq_fn fn, void *arg, const char *name);
void sf_rtkit_platform_irq_free(int irq);
void sf_rtkit_platform_irq_enable(int irq);
/* Masks and returns at once; callable from the handler. */
void sf_rtkit_platform_irq_disable_nosync(int irq);
/* Masks and waits for a running handler, unless called from the handler thread. */
void sf_rtkit_platform_irq_disable(int irq);
/* Waits for a running handler, unless called from the handler thread. */
void sf_rtkit_platform_irq_synchronize(int irq);
/* Counters for diagnostics: interrupts taken at the controller, handler runs. */
void sf_rtkit_platform_irq_stats(int irq, uint64_t *taken, uint64_t *handled);

/*
 * Power state of the block the node sits in, read-only. T8103 only: the PMGR
 * power-state register of the GFX domain (Linux ps_gfx, pmgr + 0x3f8), which
 * the node's power-gates entry is a virtual child of. *raw gets the register;
 * returns 1 when its ACTUAL field (bits 7:4) is 0xf (powered, clocked), 0
 * when not, negative errno when the state cannot be determined on this
 * machine. Turning a domain on is SF-A item A2 and is not offered here.
 */
int sf_rtkit_platform_power_state(const char *path, uint32_t *raw);

/* An integer boot-arg; returns `def` when absent. */
int sf_rtkit_platform_bootarg(const char *name, int def);

#ifdef __cplusplus
}
#endif

#endif
