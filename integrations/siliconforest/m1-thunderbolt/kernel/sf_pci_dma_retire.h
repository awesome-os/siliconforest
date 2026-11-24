/* SPDX-License-Identifier: MIT */
#ifndef SF_PCI_DMA_RETIRE_H
#define SF_PCI_DMA_RETIRE_H
#include <stdint.h>
/* These callbacks must invoke the real DMA command complete, inactive clear
 * and owned-backing release. This helper orders those calls and propagates
 * errors; it does not prove device/IRQ/work drain or mapper TLB completion.
 * A failed callback retains all remaining ownership until reboot. */
struct sf_pci_dma_retire_ops {
 void *ctx;
 uint32_t (*complete)(void *);
 uint32_t (*clear_inactive)(void *);
 void (*release_owned)(void *);
};
static inline uint32_t sf_pci_dma_retire(const struct sf_pci_dma_retire_ops *io)
{
 if(!io || !io->ctx || !io->complete || !io->clear_inactive || !io->release_owned)return 22;
 uint32_t ret=io->complete(io->ctx);if(ret)return ret;
 ret=io->clear_inactive(io->ctx);if(ret)return ret;
 io->release_owned(io->ctx);return 0;
}
#endif
