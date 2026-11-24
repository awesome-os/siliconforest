/* SPDX-License-Identifier: MIT */
/* Copyright The Asahi Linux Contributors
 * Lane property maps follow m1n1 src/kboot_atc.c; see NOTICE.
 */
#ifndef SF_ATC_CALIBRATION_H
#define SF_ATC_CALIBRATION_H
#include "sf_atc_modes.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
struct sf_atc_cal_entry { const void *raw;size_t bytes;uint32_t base; };
struct sf_atc_cal_bundle {
    const struct sf_atc_io *io;
    void *adt_ctx;
    /* Returned bytes remain immutable and alive through apply. The native
     * adapter retains their OSData or borrows permanent original ADT data. */
    const void *(*getprop)(void *,const char *path,const char *name,size_t *bytes);
    struct sf_atc_program program;
    struct sf_atc_cal_entry entries[8];
    unsigned count;
    bool checked;
};
struct sf_atc_calibration sf_atc_calibration_callbacks(struct sf_atc_cal_bundle *);
#ifdef __cplusplus
}
#endif
#endif
