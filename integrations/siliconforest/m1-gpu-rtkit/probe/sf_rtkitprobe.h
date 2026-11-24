/* SPDX-License-Identifier: GPL-2.0 */
/* SFRtkitProbe: what its IOKit class (SFRtkitProbe.cpp) calls on the LinuxKPI side. */
#ifndef SF_RTKITPROBE_H
#define SF_RTKITPROBE_H

#ifdef __cplusplus
extern "C" {
#endif

/* glue.c: sf_fbsd_init(). 0 when the layer is up. */
int sf_rtkitprobe_layer_up(void);

/*
 * probe.c, mode 2 after the ASC CPU was started: apple_rtkit_init() and
 * apple_rtkit_boot() on /arm-io/gfx-asc as the Asahi driver does them, every
 * step logged. Returns what apple_rtkit_boot() returned (0: IOP and AP power
 * states ON), or the error of an earlier step. The RTKit instance is left
 * alive so that later firmware messages are still acknowledged and a crash is
 * reported.
 */
int sf_rtkitprobe_rtkit_boot(void);

/*
 * probe.c, DCP mode (sfrtkitprobe=3), after the caller has seen CPU_CONTROL.RUN set on
 * /arm-io/dcp: apple_rtkit_init() and apple_rtkit_wake() as Linux's dcp_comp_bind() does
 * them, with every received mailbox message traced. No endpoint >= 0x20 is started, no
 * buffer request is answered, nothing is shut down or put to sleep. Returns what
 * apple_rtkit_wake() returned, or the error of an earlier step.
 */
int sf_rtkitprobe_dcp_wake(void);

/*
 * probe.c, DCP mode 4 (sfrtkitprobe=4, SF-H item H2): the same, but buffer requests are
 * served through the dcp DART (sf_dcp_dart.h, which the caller has opened): a buffer the
 * firmware placed is translated and mapped for reading, one it asks the AP for is
 * allocated, mapped and answered. serve = 0 behaves exactly as sf_rtkitprobe_dcp_wake().
 * serve = 2 (mode 5): the same requests are served by the pin's dcp_rtk_shmem_setup() code
 * through Linux's IOMMU/DMA API (../../m1-dcp-dart/include/sf_dcp_iommu.h) instead, and a
 * completed wake is followed by one pass over the other calls the driver makes on the dcp
 * stream.
 * quiesce != 0: after a completed wake, apple_rtkit_quiesce() (AP then IOP power state
 * 0x10, the state m1n1 hands over); otherwise the coprocessor is left awake.
 */
int sf_rtkitprobe_dcp_wake_serving(int serve, int quiesce);
/* mode 5: 1 = hand out addresses in m1n1's form (| asc-dram-mask) instead of Linux's */
void sf_rtkitprobe_set_dva_mask(int on);

#ifdef __cplusplus
}
#endif

#endif
