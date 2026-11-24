// SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause
/*
 * Apple Type-C PHY driver, T8103 common power/mode sequence.
 * Ported from Omacom linux-aurora 6a08a08e drivers/phy/apple/atc.c.
 * Copyright (C) The Asahi Linux Contributors
 * Author: Sven Peter <sven@kernel.org>
 *
 * SuperSpeed modes require a validated runtime calibration callback.
 */
#include "sf_atc_usb2.h"
#include "sf_atc_modes.h"
#include <errno.h>
#include <stddef.h>

static void mask(const struct sf_atc_io *io, enum sf_atc_window w,
                 uint32_t reg, uint32_t clear, uint32_t set)
{ io->write(io->ctx, w, reg, (io->read(io->ctx, w, reg) & ~clear) | set); }
static void step(const struct sf_atc_io *io, enum sf_atc_window w,
                 uint32_t reg, uint32_t clear, uint32_t set)
{ mask(io, w, reg, clear, set); io->delay_us(io->ctx, 10); }
static int poll(const struct sf_atc_io *io, enum sf_atc_window w, uint32_t reg,
                uint32_t bits, uint32_t wanted, unsigned interval, unsigned timeout)
{
    for (unsigned elapsed = 0; elapsed < timeout; elapsed += interval) {
        if ((io->read(io->ctx, w, reg) & bits) == wanted) return 0;
        io->delay_us(io->ctx, interval);
    }
    return ETIMEDOUT;
}
static void usb2_off(const struct sf_atc_io *io)
{
    step(io, SF_ATC_USB2, 0x00, 7, 4); // isolation, RUN cleared
    step(io, SF_ATC_USB2, 0x04, 0, 8); // SIDDQ
    step(io, SF_ATC_USB2, 0x04, 0, 2); // port reset
    step(io, SF_ATC_USB2, 0x04, 0, 1); // reset
    step(io, SF_ATC_USB2, 0x04, 4, 0); // APB reset
    mask(io, SF_ATC_USB2, 0x1c, 0, 3u << 29);
}
int sf_atc_usb2_reset_assert(const struct sf_atc_io *io)
{
    // Initial setup can select dummy while external reset is asserted.
    // A previously running backend must acknowledge its lock before a mux
    // change. USB2 sessions keep dummy selected throughout their lifetime.
    mask(io, SF_ATC_PIPE, 0x10, 1, 0);
    int ret = poll(io, SF_ATC_PIPE, 0x14, 1, 0, 10, 1000);
    if (ret) return ret;
    mask(io, SF_ATC_PIPE, 0x04, (1u << 1) | (1u << 2), 0);
    mask(io, SF_ATC_PIPE, 0x00, 0, (1u << 0) | (1u << 2));
    int lock = ((io->read(io->ctx, SF_ATC_PIPE, 0x0c) & 0x3f) != 0x22) &&
               (io->read(io->ctx, SF_ATC_PIPE, 0x1c) & 1);
    if (lock) {
        mask(io, SF_ATC_PIPE, 0x10, 0, 1);
        ret = poll(io, SF_ATC_PIPE, 0x14, 1, 1, 10, 1000);
        if (ret) { mask(io, SF_ATC_PIPE, 0x10, 1, 0); return ret; }
    }
    mask(io, SF_ATC_PIPE, 0x0c, 0x3f, 0x22); // dummy clock/data
    if (lock) {
        mask(io, SF_ATC_PIPE, 0x10, 1, 0);
        ret = poll(io, SF_ATC_PIPE, 0x14, 1, 0, 10, 1000);
        if (ret) return ret;
    }
    mask(io, SF_ATC_PIPE, 0x20, 0xf, 2);
    mask(io, SF_ATC_PIPE, 0x20, 0, 1u << 12); // native reset
    mask(io, SF_ATC_PIPE, 0x1c, 1, 0); // DWC3 reset
    mask(io, SF_ATC_PIPE, 0x1c, 0, 1u << 4); // clamp
    usb2_off(io);
    return 0;
}
int sf_atc_program_start(const struct sf_atc_io *io,const struct sf_atc_program *program,
                         const struct sf_atc_calibration *cal,
                         int (*aux_start)(const struct sf_atc_io *,unsigned))
{
    if (!io || !io->read || !io->write || !io->delay_us || !program ||
        (program->aux && !aux_start) || (cal && !cal->apply) ||
        program->lanes>0xfff || program->crossbar>0x3ffff ||
        program->dp_lanes>3 || program->usb3_lanes>3 || program->usb4_lanes>3 ||
        (program->dp_lanes & program->usb3_lanes) ||
        (program->dp_lanes & program->usb4_lanes) ||
        (program->usb3_lanes & program->usb4_lanes)) return EINVAL;
    // atcphy_usb2_power_on, without selecting HOST while DWC3 is in reset.
    step(io, SF_ATC_USB2, 0x08, 0, 0xf);
    step(io, SF_ATC_USB2, 0x04, 8, 0);
    step(io, SF_ATC_USB2, 0x04, 1, 0);
    step(io, SF_ATC_USB2, 0x04, 2, 0);
    step(io, SF_ATC_USB2, 0x04, 0, 4);
    mask(io, SF_ATC_USB2, 0x1c, 3u << 29, 0);
    io->write(io->ctx, SF_ATC_USB2, 0, 2);
    mask(io, SF_ATC_CORE, 0x20008, 0, 1);
    mask(io, SF_ATC_CORE, 0x20000, 0, 1);
    int ret = poll(io, SF_ATC_CORE, 0x20004, 1, 1, 100, 100000);
    if (ret) return ret;
    mask(io, SF_ATC_CORE, 0x20000, 0, 2);
    ret = poll(io, SF_ATC_CORE, 0x20004, 2, 2, 100, 100000);
    if (ret) return ret;
    mask(io, SF_ATC_CORE, 0x20000, 4, 0);
    mask(io, SF_ATC_CORE, 0x20000, 0, 8);
    if (io->common_tunables && (ret = io->common_tunables(io->ctx))) return ret;
    if (cal && (ret = cal->apply(cal->ctx,program))) return ret;
    // atcphy_configure_post_tunable_t8103
    mask(io, SF_ATC_CORE, 0x1014, 0, 0x1fe000);
    mask(io, SF_ATC_CORE, 0x2000, 0, 1u << 28);
    step(io, SF_ATC_CORE, 0x08, 0, 1u << 3);
    step(io, SF_ATC_CORE, 0x08, 0, 1u << 1);
    step(io, SF_ATC_CORE, 0x08, 0, 1u << 5);
    step(io, SF_ATC_CORE, 0x1b0, 3u << 6, 3u << 6);
    step(io, SF_ATC_CORE, 0x1b0, 3u << 2, 3u << 2);
    step(io, SF_ATC_CORE, 0x1b0, 3u << 10, 3u << 10);
    step(io, SF_ATC_CORE, 0x08, 3u << 12, 3u << 12);
    step(io, SF_ATC_CORE, 0x08, 3u << 8, 3u << 8);
    step(io, SF_ATC_CORE, 0x08, 3u << 16, 3u << 16);
    if (program->aux && (ret = aux_start(io,program->dp_lanes))) return ret;
    mask(io, SF_ATC_CORE, 0x2a00, 0, (1u << 1) | (1u << 5));
    mask(io, SF_ATC_CORE, 0x48, 0xfff, program->lanes);
    mask(io, SF_ATC_CORE, 0x4c, 0x3ffff, program->crossbar);
    mask(io, SF_ATC_CORE, 0x20008, 4, program->swap ? 4 : 0);
    for (unsigned lane = 0; lane < 2; ++lane) {
        uint32_t reg = lane ? 0x10010 : 0x9010;
        step(io, SF_ATC_CORE, reg, 1, program->dp_lanes & (1u << lane) ? 1 : 0);
        if (program->dp_lanes & (1u << lane)) mask(io, SF_ATC_CORE, reg, 1u << 9, 0);
    }
    mask(io, SF_ATC_CORE, 0x20000, 0, 1u << 4);
    return 0;
}
int sf_atc_usb2_start(const struct sf_atc_io *io, int reversed)
{
    // Keep the original USB2 API independent of the DP backend: the minimal
    // host bundle links this shared program without enabling other modes.
    const struct sf_atc_program p = { .lanes=0x6db, .crossbar=reversed?0xb:0xa };
    return sf_atc_program_start(io,&p,NULL,NULL);
}
void sf_atc_usb2_release_reset(const struct sf_atc_io *io)
{
    mask(io, SF_ATC_PIPE, 0x1c, 1u << 4, 0);
    mask(io, SF_ATC_PIPE, 0x1c, 0, 1);
}
void sf_atc_usb2_select_host(const struct sf_atc_io *io)
{ mask(io, SF_ATC_USB2, 8, 0, 7u << 12); }
int sf_atc_usb2_stop(const struct sf_atc_io *io)
{
    int ret = sf_atc_usb2_reset_assert(io);
    if (ret) return ret;
    // atcphy_disable_dp_aux (T8103), then atcphy_power_off.
    mask(io, SF_ATC_LPDPTX, 0x4000, 0, 0x10);
    mask(io, SF_ATC_LPDPTX, 0, 0, 1u << 4);
    mask(io, SF_ATC_LPDPTX, 0x4000, 0, 4);
    step(io, SF_ATC_LPDPTX, 0x4000, 1, 0);
    step(io, SF_ATC_LPDPTX, 0x4000, 2, 0);
    mask(io, SF_ATC_CORE, 0x7000, (1u << 2) | (7u << 13), 0);
    mask(io, SF_ATC_CORE, 0x20000, 1u << 4, 0);
    mask(io, SF_ATC_CORE, 0x20000, 0, 4);
    mask(io, SF_ATC_CORE, 0x20008, 5, 0);
    mask(io, SF_ATC_CORE, 0x20000, 8, 0);
    mask(io, SF_ATC_CORE, 0x20000, 2, 0);
    ret = poll(io, SF_ATC_CORE, 0x20004, 2, 0, 10, 1000);
    if (ret) return ret;
    mask(io, SF_ATC_CORE, 0x20000, 1, 0);
    return poll(io, SF_ATC_CORE, 0x20004, 1, 0, 10, 1000);
}
