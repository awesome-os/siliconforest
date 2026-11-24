/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/* Copyright (C) The Asahi Linux Contributors
 * T8103 DP backend for the directly ported atc.c register program.
 */
#ifndef SF_ATC_DP_H
#define SF_ATC_DP_H
#include "sf_atc_usb2.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_atc_dp {
    const struct sf_atc_io *io;
    unsigned lane_mask; // physical PMA mask: 1 or 2 = two DP lanes, 3 = four
    int rate;          // 1620/2700/5400/8100, -1 when hardware has no known rate
    bool mode_dirty, pipe_pending, tunnel_clock_on, tunnel_saved;
};
/* Caller owns the PHY lock and has configured powered DP/USB3_DP mode,
 * calibration and lane routing; this API never steals another mode's PLL.
 * This is a register backend, not an attached DCP endpoint or HPD provider. */
int sf_atc_dp_aux_start(struct sf_atc_dp *dp);
int sf_atc_dp_aux_stop(struct sf_atc_dp *dp);
/* Zero acknowledges the upstream no-op notification: no writes, no cached
 * rate change and no clock/power-off proof. AUX/mode teardown stays separate. */
int sf_atc_dp_set_rate(struct sf_atc_dp *dp, unsigned mbps);
/* DP-over-Thunderbolt uses a separate T8103 pixel-clock sequence. The
 * caller holds the same PHY lock, owns a TBT/USB4 session and drains DCP
 * users before stopping. A failed shutdown retains clock_on ownership. */
struct sf_atc_tunnel {
    const struct sf_atc_io *io;
    bool configured, mode_dirty, pipe_pending, clock_on;
    uint8_t rate; // DP link rate code 06/0a/14/1e; 0 = stopped/unknown
};
int sf_atc_tunnel_set_rate(struct sf_atc_tunnel *tunnel, uint8_t rate);
#ifdef __cplusplus
}
#endif
#endif
