/* SPDX-License-Identifier: GPL-2.0 */
/* Lifecycle order from Linux drivers/thunderbolt/apple.c (6a08a08e).
 * Copyright (c) Sven Peter <sven@kernel.org>
 */
#ifndef SF_CIO_LIFECYCLE_H
#define SF_CIO_LIFECYCLE_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_cio_ops {
    void *ctx;
    int (*phy_ready)(void *,uint32_t cable_info);
    int (*pcie_reset_unbound)(void *);
    int (*power_get)(void *);
    int (*reset_deassert)(void *);
    int (*rtkit_start)(void *);
    int (*firmware_ready)(void *);
    int (*apply_tunables)(void *);
    int (*children_add)(void *);
    int (*domain_ready)(void *);
    int (*set_cable)(void *,uint32_t cable_info);
    int (*children_remove_drain)(void *);
    int (*rtkit_stop_drain)(void *);
    int (*power_put)(void *);
};
struct sf_cio_session {
    uint32_t cable_info;
    uint64_t generation;
    bool power, rtkit, children, ready, closing;
};
/* Caller owns the ACIO lock and keeps ATC in the negotiated mode until stop
 * succeeds. Every callback is mandatory and must report a hardware/backend
 * fact. No MMIO, success placeholders or platform attachment are supplied.
 * Partial start flags are retained before callbacks so failures also unwind.
 * A failed child/RTKit drain keeps power and PHY ownership for a later retry. */
int sf_cio_start(struct sf_cio_session *,const struct sf_cio_ops *,uint32_t cable_info);
int sf_cio_stop(struct sf_cio_session *,const struct sf_cio_ops *);
#ifdef __cplusplus
}
#endif
#endif
