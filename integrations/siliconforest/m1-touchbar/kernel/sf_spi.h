/* SPDX-License-Identifier: GPL-2.0 */
/*
 * One Apple SPI controller ("spi-1,samsung": /arm-io/spi0 ...), polled, for a device with
 * a GPIO chip select. Ported from Linux drivers/spi/spi-apple.c (omacom/linux-aurora).
 * All calls from thread context, one at a time.
 */
#ifndef _SF_SPI_H
#define _SF_SPI_H

#include "sf_soc.h"

struct SFSpi {
	SFMmio   regs;
	uint32_t clockHz;       /* the controller's reference clock */
	uint32_t timeouts;
	uint64_t bitsMoved, busyNs;     /* of the transfers that completed: the bus's real rate */

	SFSpi() : clockHz(0), timeouts(0), bitsMoved(0), busyNs(0) {}
	/* kilobits per second over every completed transfer so far */
	uint32_t kbps() const { return busyNs ? (uint32_t)(bitsMoved * 1000000 / busyNs) : 0; }

	/* Maps the controller and puts it into its idle configuration (apple_spi_init). */
	bool attach(uint64_t phys, uint32_t clockHz);
	/*
	 * One transfer of `len` bytes at `hz`, SPI mode 0, most significant bit first, in
	 * words of `bits` (8 or 16; 16-bit words are taken from and stored to the buffers
	 * in little-endian order, as Linux does on this machine). tx or rx may be NULL.
	 * The chip select is the caller's. 0 or an errno value.
	 */
	int transfer(const void *tx, void *rx, uint32_t len, unsigned bits, uint32_t hz);
};

#endif
