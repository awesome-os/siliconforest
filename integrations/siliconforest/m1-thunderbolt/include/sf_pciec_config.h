/* SPDX-License-Identifier: GPL-2.0 */
/* Native PCIe-C ECAM adaptation of pcie-apple.c and SFApcieT8103Host.
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2021 Google LLC
 * Copyright (C) 2021 Corellium LLC
 * Copyright (C) 2021 Mark Kettenis <kettenis@openbsd.org>
 */
#ifndef SF_PCIEC_CONFIG_H
#define SF_PCIEC_CONFIG_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_pciec_config_view {
    uint64_t bytes;
    unsigned first_bus,last_bus;
    bool admitted;
};
/* Pure validation only: caller serializes the live view, pins the actual
 * controller power/lease, joins accesses before unmapping and checks link
 * access independently. Returns negative errno and leaves output unchanged
 * on refusal. This function does not touch MMIO or establish quiescence. */
int sf_pciec_config_offset(const struct sf_pciec_config_view *,unsigned bus,
                          unsigned device,unsigned function,unsigned reg,
                          unsigned width,uint64_t *offset);
/* Until the real per-device mapper/IRQ/lifetime path is admitted, strip
 * PCI_COMMAND_MASTER from writes to the command register. This is a helper
 * for the future native bridge, not an installed endpoint-DMA guard. */
int sf_pciec_config_write_filter(unsigned reg,unsigned width,uint32_t value,
                                bool dma_admitted,uint32_t *filtered);
#ifdef __cplusplus
}
#endif
#endif
