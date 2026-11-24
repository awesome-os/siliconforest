/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/* Copyright (C) The Asahi Linux Contributors
 * T8103 mode/orientation configuration from Linux atc.c, Sven Peter.
 */
#ifndef SF_ATC_MODES_H
#define SF_ATC_MODES_H
#include "sf_atc_usb2.h"
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
enum sf_atc_mode { SF_ATC_OFF, SF_ATC_MODE_USB2, SF_ATC_MODE_USB3,
    SF_ATC_MODE_USB3_DP, SF_ATC_MODE_TBT, SF_ATC_MODE_USB4, SF_ATC_MODE_DP };
enum sf_atc_pipe { SF_ATC_DUMMY, SF_ATC_USB3_BACKEND, SF_ATC_USB4_BACKEND };
struct sf_atc_program {
    uint32_t lanes, crossbar;
    unsigned dp_lanes, usb3_lanes, usb4_lanes;
    bool swap, aux, reversed;
    enum sf_atc_pipe pipe;
};
/* Both calibration callbacks are mandatory for modes with SS lanes. Validate
 * all required common/lane blobs before power/MMIO; apply executes after
 * common-a/AXI2AF/common-b, before post-tunable/lanes/reset release. */
struct sf_atc_calibration {
    void *ctx;
    int (*validate)(void *,const struct sf_atc_program *);
    int (*apply)(void *,const struct sf_atc_program *);
};
/* All functions/callbacks here return 0 or a positive errno. */
int sf_atc_mode_plan(enum sf_atc_mode,bool reversed,struct sf_atc_program *);
int sf_atc_mode_start(const struct sf_atc_io *,enum sf_atc_mode,bool reversed,
                       const struct sf_atc_calibration *);
/* Shared register program; normally called by mode_start/usb2_start. The
 * owner drains previous USB/DP/TB users and holds DWC3 reset before entry.
 * It must stop any previous tunnel clock before changing the mode. */
int sf_atc_program_start(const struct sf_atc_io *,const struct sf_atc_program *,
                         const struct sf_atc_calibration *,
                         int (*aux_start)(const struct sf_atc_io *,unsigned));
#ifdef __cplusplus
}
#endif
#endif
