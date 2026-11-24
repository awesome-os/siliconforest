/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SF_TB_NHI_H
#define SF_TB_NHI_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
/* Native owner supplies mapped, powered ACIO1 resources and resolved AIC
 * handles. This interface does not map registers or acquire a PHY lease.
 * All resources/context must remain valid until reboot, including on errors.
 * irq_free must mask and JOIN its actual native handler before returning. */
struct sf_tb_nhi_config {
 void *ctx,*device,*nhi_base,*pdf_base;
 int rx_irq[12],tx_irq[12];
 bool (*powered)(void *);
 int (*irq_request)(int,int (*)(void *),void *,const char *);
 void (*irq_enable)(int);
 void (*irq_free)(int);
 /* Optional all-or-none native PCIe backend. prepare reserves ownership
  * and prerequisites WITHOUT PCIe-C MMIO. activated queues cold-init/link
  * and enumeration only after both real tunnel adapters are enabled,
  * OUTSIDE the domain lock; deactivate joins real children/DMA before
  * releasing ownership. Callback success is not itself PCI/link evidence.
  * Uninstalled callbacks refuse activation/authorization. */
 int (*pci_prepare)(void *);
 int (*pci_activated)(void *);
 int (*pci_deactivate)(void *);
 /* Optional all-or-none T8103 APPLE_DPIN_CHANGED handoff, not the M2
  * pre/post flow. Called with the real CM domain lock held. changed must
  * publish/revoke the actual generation and retire powered DP-IN access
  * synchronously on false, then queue provider/DCP work OUTSIDE that lock.
  * No DCP provider may be called synchronously from these callbacks.
  * waiting reports actual pipeline-capacity state, never a success stub.
  * Owner/context/resources remain retained through teardown and failures. */
 int (*dp_changed)(void *,unsigned dpin,uint32_t port,uint64_t generation,bool active);
 bool (*dp_waiting)(void *,unsigned dpin);
};
#ifdef __cplusplus
extern "C" {
#endif
int sf_tb_nhi_device_create(void *dt_entry,const void *raw_drom,size_t length,void **device);
int sf_tb_nhi_bus_init(void);
int sf_tb_nhi_create(const struct sf_tb_nhi_config *,void **);
int sf_tb_nhi_start(void *,uint32_t cable_info);
/* Bounded read-only diagnostic, never an authorization or link-state cache.
 * Public model IDs only; no UID, DROM bytes or DMA/physical addresses. */
struct sf_tb_nhi_port_status {
 uint32_t port,state,error,remote,remote_vendor,remote_device;
 uint32_t cap_phy,flags_valid,lane_disabled,adapter_locked;
 uint32_t remote_authorized;
 uint32_t phy[2],header[8];
 uint32_t cap_usb4,usb4_controls_valid,usb4_controls_error;
 uint32_t usb4_controls[2]; // PORT_CS_18/19 only; no sideband data window.
};
struct sf_tb_nhi_status {
 uint32_t root_vendor,root_device,max_port,port_count,cable_info;
 uint32_t root_header[5];
 uint32_t root_header_valid;
 uint32_t root_controls[3]; // ROUTER_CS_5/6/26; exclude UID7/8 and op data9..24.
 uint32_t root_controls_valid,root_controls_error;
 uint32_t irq_count[24];
 struct sf_tb_nhi_port_status ports[16];
};
int sf_tb_nhi_snapshot(void *,struct sf_tb_nhi_status *);
/* Explicit native administrative caller only; no user-facing API yet.
 * Real domain lock, actual direct TS3 model/route and backend required.
 * No arbitrary route, key enrollment, downstream dock or DMA admission.
 * Native owner must pin its physical cable lease through this call. */
int sf_tb_nhi_authorize_ts3(void *);
/* Failure retains context, DMA and controller ownership. No timeout-free
 * retirement is implied: Linux domain teardown joins its actual workers. */
int sf_tb_nhi_stop(void *);
/* Called by the private Linux ring engine with its NHI/ring locks held,
 * after disabling IRQ/VALID and BEFORE clearing descriptor addresses. */
int sf_tb_nhi_check_stopped(void *ring);
#ifdef __cplusplus
}
#endif
#endif
