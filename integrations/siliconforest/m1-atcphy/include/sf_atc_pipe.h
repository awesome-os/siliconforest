/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/* Copyright (C) The Asahi Linux Contributors; author Sven Peter. */
#ifndef SF_ATC_PIPE_H
#define SF_ATC_PIPE_H
#include "sf_atc_modes.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Externally serialized with mode/rate programming. Initialize only after
 * native reset_assert has established dummy, never from an assumed idle. */
struct sf_atc_pipe_state { enum sf_atc_pipe backend; bool dummy_pending; };
/* T8103's qualified USB3 workaround skips host BIST (atc.c). Caller has
 * already powered/calibrated the mode and initialized DWC3's host role. */
int sf_atc_pipe_route(const struct sf_atc_io *,struct sf_atc_pipe_state *,enum sf_atc_pipe);
/* stopped=true requires actual controller halt/reset and joined producers.
 * A stopped PIPE may lack a lock clock. A live USB3 handoff must ACK; if
 * completion fails after moving the mux, retain ownership and retry it. */
int sf_atc_pipe_park(const struct sf_atc_io *,struct sf_atc_pipe_state *,bool stopped);
/* Enable the dummy-PHY bit from Linux's m1n1-established baseline,
 * preserving all other PIPE fields, without releasing DWC3
 * reset. acio_off must be actual native power-state proof, held through
 * this exclusive broker transition; old DWC3 producers must be joined.
 * The function separately verifies actual dummy/ACK/reset/clamp state.
 * No direct host or NHI client may be published before it succeeds. */
int sf_atc_pipe_bootstrap_dummy(const struct sf_atc_io *,const struct sf_atc_pipe_state *,int acio_off);
/* Experimental T8103 cold AON baseline: accept only observed0x30 or the
 * m1n1+Linux held-reset/clamp baseline0x10. Requires a fresh native ACIO-off
 * proof and a drained/parked DWC3. Never releases DWC3 reset. Bit5's meaning
 * is not established; this reproduces the published whole-word bootstrap,
 * not a general bit5 control. Reject every other AON state without writes. */
int sf_atc_pipe_bootstrap_aon(const struct sf_atc_io *,const struct sf_atc_pipe_state *,int acio_off);
/* m1n1 usb.c initializes T8103 USB2 MISCTUNE to0x008c0813 before Linux.
 * Native steady state differs only at bit11. Reproduce that explicit cold
 * baseline while USB2 is isolated/reset/gated and DWC3/ACIO are stopped.
 * Accept only the two observed words plus both clock-gate bits; no guessed
 * bit function, other word normalization or live-PHY repair is supported. */
int sf_atc_usb2_bootstrap_t8103(const struct sf_atc_io *,const struct sf_atc_pipe_state *,int acio_off);
#ifdef __cplusplus
}
#endif
#endif
