/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * One channel of an Apple audio DMA controller (ADMAC), polled: the channel code of
 * ../../kernel/sf_mca.cpp (SFAdmacChan, written for /arm-io/admac-sio) as a file of its
 * own, with the interrupt output index a member (admac-aop-audio uses output 2, admac-sio
 * output 1) and the set-up split into the steps Linux takes at different times
 * (allocate the SRAM carve-out, configure the bus, start a cyclic transfer).
 *
 * Callers serialise the calls on each object; nothing here runs in interrupt context.
 */
#ifndef _SF_ADMAC_H
#define _SF_ADMAC_H

#include "sf_soc.h"

struct SFAdmacChan {
	const SFMmio *admac;
	unsigned      ch;        /* even numbers transmit, odd ones receive */
	unsigned      irqIndex;  /* which of the four interrupt outputs carries the status */

	SFAdmacChan() : admac(NULL), ch(0), irqIndex(0) {}

	/* admac_alloc_chan_resources(): block `sramBlock` (2 KiB) of the direction's SRAM. */
	void allocResources(unsigned sramBlock);
	/* admac_device_config(): `wordBytes` 1, 2 or 4; `frameWords` 0/1, 2 or 4. */
	void config(unsigned wordBytes, unsigned frameWords);
	void resetRings(void);
	bool descRingFull(void) const;
	bool descRingEmpty(void) const;
	void submit(uint64_t iova, uint32_t bytes);
	/* admac_start_chan(), without unmasking the interrupt: the rings are polled. */
	void start(void);
	/* admac_stop_chan() and admac_wait_stopped(); false when it is still enabled after 100 ms. */
	bool stop(void);
	/* admac_drain_reports(): the number taken; *counter is the last one's 24 MHz timestamp. */
	unsigned drainReports(uint64_t *counter);
	/* admac_handle_status_err(); true when an error was latched. */
	bool clearError(void);
	bool enabled(void) const;
	uint32_t txSramSize(void) const;
	uint32_t rxSramSize(void) const;
	/* descriptor ring, report ring, channel status, residue */
	void status(uint32_t out[4]) const;
};

#endif
