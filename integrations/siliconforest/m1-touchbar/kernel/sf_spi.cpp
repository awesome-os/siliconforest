// SPDX-License-Identifier: GPL-2.0
/*
 * Apple SoC SPI controller, polled. Ported from Linux drivers/spi/spi-apple.c as carried
 * in omacom/linux-aurora (apple_spi_init, apple_spi_prepare_message,
 * apple_spi_prep_transfer, apple_spi_tx, apple_spi_rx, apple_spi_transfer_one with the
 * polling branch of apple_spi_wait):
 *   Copyright The Asahi Linux Contributors
 *   Based on spi-sifive.c, Copyright 2018 SiFive, Inc.
 */
#include "sf_spi.h"

#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFSpi: " fmt "\n", ##__VA_ARGS__)

#define APPLE_SPI_CTRL                  0x000
#define APPLE_SPI_CTRL_RUN              (1u << 0)
#define APPLE_SPI_CTRL_TX_RESET         (1u << 2)
#define APPLE_SPI_CTRL_RX_RESET         (1u << 3)
#define APPLE_SPI_CFG                   0x004
#define APPLE_SPI_CFG_CPHA              (1u << 1)
#define APPLE_SPI_CFG_CPOL              (1u << 2)
#define APPLE_SPI_CFG_MODE_IRQ          (1u << 5)
#define APPLE_SPI_CFG_LSB_FIRST         (1u << 13)
#define APPLE_SPI_CFG_WORD_SIZE_8B      (0u << 15)
#define APPLE_SPI_CFG_FIFO_THRESH_8B    (0u << 17)
#define APPLE_SPI_PIN                   0x00c
#define APPLE_SPI_PIN_CS                (1u << 1)
#define APPLE_SPI_TXDATA                0x010
#define APPLE_SPI_RXDATA                0x020
#define APPLE_SPI_CLKDIV                0x030
#define APPLE_SPI_CLKDIV_MAX            0x7ff
#define APPLE_SPI_RXCNT                 0x034
#define APPLE_SPI_TXCNT                 0x04c
#define APPLE_SPI_FIFOSTAT              0x10c
#define APPLE_SPI_FIFOSTAT_LEVEL_TX(v)  (((v) >> 8) & 0xff)
#define APPLE_SPI_FIFOSTAT_LEVEL_RX(v)  (((v) >> 24) & 0xff)
#define APPLE_SPI_IE_XFER               0x130
#define APPLE_SPI_IF_XFER               0x134
#define APPLE_SPI_XFER_RXCOMPLETE       (1u << 0)
#define APPLE_SPI_XFER_TXCOMPLETE       (1u << 1)
#define APPLE_SPI_IE_FIFO               0x138
#define APPLE_SPI_IF_FIFO               0x13c
#define APPLE_SPI_FIFO_RXTHRESH         (1u << 4)
#define APPLE_SPI_FIFO_TXTHRESH         (1u << 5)
#define APPLE_SPI_FIFO_RXUNDERRUN       (1u << 16)
#define APPLE_SPI_FIFO_TXOVERFLOW       (1u << 17)
#define APPLE_SPI_SHIFTCFG              0x150
#define APPLE_SPI_SHIFTCFG_BITS(v)      ((uint32_t)(v) << 16)
#define APPLE_SPI_SHIFTCFG_BITS_MASK    (0x3fu << 16)
#define APPLE_SPI_SHIFTCFG_OVERRIDE_CS  (1u << 24)
#define APPLE_SPI_PINCFG                0x154
#define APPLE_SPI_PINCFG_KEEP_CS        (1u << 1)
#define APPLE_SPI_PINCFG_CS_IDLE_VAL    (1u << 9)
#define APPLE_SPI_DELAY_PRE             0x160
#define APPLE_SPI_DELAY_POST            0x168
#define APPLE_SPI_DELAY_POST_CYCLES     0x194
#define APPLE_SPI_FIFO_DEPTH            16
#define APPLE_SPI_TIMEOUT_MS            200

bool
SFSpi::attach(uint64_t phys, uint32_t hz)
{
	if (!regs.mapPhys(phys, 0x4000))
		return false;
	clockHz = hz;
	/* apple_spi_init(): CS high (inactive), no override, no automatic CS */
	regs.w32(APPLE_SPI_PIN, APPLE_SPI_PIN_CS);
	regs.mask32(APPLE_SPI_SHIFTCFG, APPLE_SPI_SHIFTCFG_OVERRIDE_CS, 0);
	regs.mask32(APPLE_SPI_PINCFG, APPLE_SPI_PINCFG_CS_IDLE_VAL, APPLE_SPI_PINCFG_KEEP_CS);
	regs.w32(APPLE_SPI_CTRL, APPLE_SPI_CTRL_RX_RESET | APPLE_SPI_CTRL_TX_RESET);
	regs.w32(APPLE_SPI_CFG, APPLE_SPI_CFG_FIFO_THRESH_8B | APPLE_SPI_CFG_MODE_IRQ | APPLE_SPI_CFG_WORD_SIZE_8B);
	regs.w32(APPLE_SPI_IE_FIFO, 0);
	regs.w32(APPLE_SPI_IE_XFER, 0);
	regs.w32(APPLE_SPI_DELAY_PRE, 0);
	regs.w32(APPLE_SPI_DELAY_POST, 0);
	regs.w32(APPLE_SPI_DELAY_POST_CYCLES, 0);
	/* apple_spi_prepare_message(): mode 0, most significant bit first */
	regs.mask32(APPLE_SPI_CFG, APPLE_SPI_CFG_CPHA | APPLE_SPI_CFG_CPOL | APPLE_SPI_CFG_LSB_FIRST, 0);
	return true;
}

static void
spi_tx(const SFMmio &regs, const uint8_t **tx, uint32_t *left, unsigned bytesPerWord)
{
	if (*tx == NULL)
		return;
	uint32_t inuse = APPLE_SPI_FIFOSTAT_LEVEL_TX(regs.r32(APPLE_SPI_FIFOSTAT));
	uint32_t words = inuse < APPLE_SPI_FIFO_DEPTH ? APPLE_SPI_FIFO_DEPTH - inuse : 0;
	if (words > *left)
		words = *left;
	*left -= words;
	while (words--) {
		const uint8_t *p = *tx;
		regs.w32(APPLE_SPI_TXDATA, bytesPerWord == 2 ? (uint32_t)(p[0] | p[1] << 8) : p[0]);
		*tx += bytesPerWord;
	}
}

static void
spi_rx(const SFMmio &regs, uint8_t **rx, uint32_t *left, unsigned bytesPerWord)
{
	if (*rx == NULL)
		return;
	uint32_t words = APPLE_SPI_FIFOSTAT_LEVEL_RX(regs.r32(APPLE_SPI_FIFOSTAT));
	if (words > *left)
		words = *left;
	*left -= words;
	while (words--) {
		uint32_t v = regs.r32(APPLE_SPI_RXDATA);
		uint8_t *p = *rx;
		p[0] = (uint8_t)v;
		if (bytesPerWord == 2)
			p[1] = (uint8_t)(v >> 8);
		*rx += bytesPerWord;
	}
}

int
SFSpi::transfer(const void *txbuf, void *rxbuf, uint32_t len, unsigned bits, uint32_t hz)
{
	const uint8_t *tx = (const uint8_t *)txbuf;
	uint8_t *rx = (uint8_t *)rxbuf;
	unsigned bytesPerWord = bits > 8 ? 2 : 1;
	uint32_t words = len / bytesPerWord;
	uint32_t remainingTx = tx ? words : 0, remainingRx = rx ? words : 0;
	uint32_t xferFlags = 0;
	int ret = 0;

	if (regs.va == NULL || hz == 0 || (bits != 8 && bits != 16) || len % bytesPerWord != 0)
		return EINVAL;
	if (words == 0)
		return 0;

	/* apple_spi_prep_transfer() */
	uint32_t cr = (clockHz + hz - 1) / hz;
	regs.w32(APPLE_SPI_CLKDIV, cr > APPLE_SPI_CLKDIV_MAX ? APPLE_SPI_CLKDIV_MAX : cr);
	regs.mask32(APPLE_SPI_SHIFTCFG, APPLE_SPI_SHIFTCFG_BITS_MASK, APPLE_SPI_SHIFTCFG_BITS(bits));

	uint64_t startNs, endNs;
	absolutetime_to_nanoseconds(mach_absolute_time(), &startNs);

	/* apple_spi_transfer_one() */
	regs.w32(APPLE_SPI_CTRL, APPLE_SPI_CTRL_RX_RESET | APPLE_SPI_CTRL_TX_RESET);
	regs.w32(APPLE_SPI_IF_XFER, ~0u);
	regs.w32(APPLE_SPI_IF_FIFO, ~0u);
	if (tx)
		xferFlags |= APPLE_SPI_XFER_TXCOMPLETE;
	if (rx)
		xferFlags |= APPLE_SPI_XFER_RXCOMPLETE;
	regs.w32(APPLE_SPI_TXCNT, remainingTx);
	regs.w32(APPLE_SPI_RXCNT, remainingRx);
	spi_tx(regs, &tx, &remainingTx, bytesPerWord);
	regs.w32(APPLE_SPI_CTRL, APPLE_SPI_CTRL_RUN);
	/* a few words are popped off at once */
	spi_tx(regs, &tx, &remainingTx, bytesPerWord);

	/*
	 * Linux waits APPLE_SPI_TIMEOUT_MS for each FIFO event. The flags are polled here and
	 * may stay set, so the time is counted from the last word moved instead.
	 */
	uint64_t deadline;
	clock_interval_to_deadline(APPLE_SPI_TIMEOUT_MS, kMillisecondScale, &deadline);
	while (xferFlags) {
		uint32_t fifoFlags = (remainingTx ? APPLE_SPI_FIFO_TXTHRESH : 0) | (remainingRx ? APPLE_SPI_FIFO_RXTHRESH : 0);
		uint32_t hadTx = remainingTx, hadRx = remainingRx;
		/* apple_spi_wait(), polling */
		for (;;) {
			uint32_t fifo = regs.r32(APPLE_SPI_IF_FIFO), xfer = regs.r32(APPLE_SPI_IF_XFER);
			if ((fifo & fifoFlags) || (xfer & xferFlags))
				break;
			if (mach_absolute_time() > deadline)
				break;
		}
		xferFlags &= ~regs.r32(APPLE_SPI_IF_XFER);
		spi_tx(regs, &tx, &remainingTx, bytesPerWord);
		spi_rx(regs, &rx, &remainingRx, bytesPerWord);
		if (remainingTx != hadTx || remainingRx != hadRx) {
			clock_interval_to_deadline(APPLE_SPI_TIMEOUT_MS, kMillisecondScale, &deadline);
		} else if (xferFlags && mach_absolute_time() > deadline) {
			ret = ETIMEDOUT;
			timeouts++;
			LOG("transfer timed out (%u tx, %u rx words left of %u, clkdiv %u)", remainingTx, remainingRx, words,
			    regs.r32(APPLE_SPI_CLKDIV));
			break;
		}
	}
	/* the transfer can complete before the last word is in the receive FIFO */
	for (int retries = 100; ret == 0 && remainingRx && retries > 0; retries--)
		spi_rx(regs, &rx, &remainingRx, bytesPerWord);
	if (ret == 0 && (remainingTx || remainingRx)) {
		LOG("transfer completed with %u tx, %u rx words left", remainingTx, remainingRx);
		ret = EIO;
	}
	uint32_t fifo = regs.r32(APPLE_SPI_IF_FIFO);
	if (fifo & (APPLE_SPI_FIFO_TXOVERFLOW | APPLE_SPI_FIFO_RXUNDERRUN)) {
		LOG("FIFO error flags 0x%x", fifo);
		if (ret == 0)
			ret = EIO;
	}
	regs.w32(APPLE_SPI_CTRL, 0);
	if (ret == 0) {
		absolutetime_to_nanoseconds(mach_absolute_time(), &endNs);
		bitsMoved += (uint64_t)words * bits;
		busyNs += endNs - startNs;
	}
	return ret;
}
