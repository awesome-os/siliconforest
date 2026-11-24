// SPDX-License-Identifier: GPL-2.0-only
/*
 * One ADMAC channel, polled. See sf_admac.h.
 *
 * Ported from the Omacom Linux tree, drivers/dma/apple-admac.c (channel registers,
 * descriptor and report rings, SRAM carve-out, FIFO thresholds, start/stop order):
 *   Driver for Audio DMA Controller (ADMAC) on t8103 (M1) and other Apple chips
 *   Copyright (C) The Asahi Linux Contributors
 */
#include "sf_admac.h"

#include <IOKit/IOLib.h>

#define RING_FULL               (1u << 9)
#define RING_EMPTY              (1u << 8)
#define RING_ERR                (1u << 10)
#define STATUS_DESC_DONE        (1u << 0)
#define STATUS_ERR              (1u << 6)
#define FLAG_DESC_NOTIFY        (1u << 16)
#define REG_TX_START            0x0000
#define REG_TX_STOP             0x0004
#define REG_RX_START            0x0008
#define REG_RX_STOP             0x000c
#define REG_TX_SRAM_SIZE        0x0094
#define REG_RX_SRAM_SIZE        0x0098
#define REG_CHAN_BASE(ch)       (0x8000 + (ch) * 0x200)
#define REG_CHAN_CTL(ch)        (REG_CHAN_BASE(ch) + 0x00)
#define REG_CHAN_CTL_RST_RINGS  (1u << 0)
#define REG_CHAN_INTSTATUS(ch, idx) (REG_CHAN_BASE(ch) + 0x10 + (idx) * 4)
#define REG_CHAN_INTMASK(ch, idx)   (REG_CHAN_BASE(ch) + 0x20 + (idx) * 4)
#define REG_BUS_WIDTH(ch)       (REG_CHAN_BASE(ch) + 0x40)
#define BUS_WIDTH_WORD_SIZE     0x0fu
#define BUS_WIDTH_FRAME_SIZE    0xf0u
#define BUS_WIDTH_8BIT          0x00
#define BUS_WIDTH_16BIT         0x01
#define BUS_WIDTH_32BIT         0x02
#define BUS_WIDTH_FRAME_2_WORDS 0x10
#define BUS_WIDTH_FRAME_4_WORDS 0x20
#define REG_CHAN_SRAM_CARVEOUT(ch) (REG_CHAN_BASE(ch) + 0x50)
#define REG_CHAN_FIFOCTL(ch)    (REG_CHAN_BASE(ch) + 0x54)
#define REG_RESIDUE(ch)         (REG_CHAN_BASE(ch) + 0x64)
#define REG_DESC_RING(ch)       (REG_CHAN_BASE(ch) + 0x70)
#define REG_REPORT_RING(ch)     (REG_CHAN_BASE(ch) + 0x74)
#define REG_DESC_WRITE(ch)      (0x10000 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)
#define REG_REPORT_READ(ch)     (0x10100 + ((ch) / 2) * 0x4 + ((ch) & 1) * 0x4000)
#define SRAM_BLOCK              2048
#define ADMAC_STOP_TIMEOUT_US   100000

void
SFAdmacChan::allocResources(unsigned sramBlock)
{
	admac->w32(REG_CHAN_SRAM_CARVEOUT(ch), (SRAM_BLOCK << 16) | (sramBlock * SRAM_BLOCK));
}

void
SFAdmacChan::config(unsigned wordBytes, unsigned frameWords)
{
	uint32_t width = admac->r32(REG_BUS_WIDTH(ch)) & ~(BUS_WIDTH_WORD_SIZE | BUS_WIDTH_FRAME_SIZE);

	width |= wordBytes == 1 ? BUS_WIDTH_8BIT : wordBytes == 2 ? BUS_WIDTH_16BIT : BUS_WIDTH_32BIT;
	if (frameWords == 2)
		width |= BUS_WIDTH_FRAME_2_WORDS;
	else if (frameWords == 4)
		width |= BUS_WIDTH_FRAME_4_WORDS;
	admac->w32(REG_BUS_WIDTH(ch), width);
	admac->w32(REG_CHAN_FIFOCTL(ch), ((0x30u * wordBytes) << 16) | (0x18u * wordBytes));
}

void
SFAdmacChan::resetRings(void)
{
	admac->w32(REG_CHAN_CTL(ch), REG_CHAN_CTL_RST_RINGS);
	admac->w32(REG_CHAN_CTL(ch), 0);
}

bool
SFAdmacChan::descRingFull(void) const
{
	return (admac->r32(REG_DESC_RING(ch)) & RING_FULL) != 0;
}

bool
SFAdmacChan::descRingEmpty(void) const
{
	return (admac->r32(REG_DESC_RING(ch)) & RING_EMPTY) != 0;
}

void
SFAdmacChan::submit(uint64_t iova, uint32_t bytes)
{
	admac->w32(REG_DESC_WRITE(ch), (uint32_t)iova);
	admac->w32(REG_DESC_WRITE(ch), (uint32_t)(iova >> 32));
	admac->w32(REG_DESC_WRITE(ch), bytes);
	admac->w32(REG_DESC_WRITE(ch), FLAG_DESC_NOTIFY);
}

void
SFAdmacChan::start(void)
{
	/* the status bits are polled; the interrupt stays masked */
	admac->w32(REG_CHAN_INTSTATUS(ch, irqIndex), STATUS_DESC_DONE | STATUS_ERR);
	admac->w32(REG_CHAN_INTMASK(ch, irqIndex), 0);
	admac->w32((ch & 1) ? REG_RX_START : REG_TX_START, 1u << (ch / 2));
}

bool
SFAdmacChan::enabled(void) const
{
	return (admac->r32((ch & 1) ? REG_RX_START : REG_TX_START) & (1u << (ch / 2))) != 0;
}

bool
SFAdmacChan::stop(void)
{
	admac->w32((ch & 1) ? REG_RX_STOP : REG_TX_STOP, 1u << (ch / 2));
	for (int i = 0; i < ADMAC_STOP_TIMEOUT_US / 100 && enabled(); i++)
		IODelay(100);
	return !enabled();
}

unsigned
SFAdmacChan::drainReports(uint64_t *counter)
{
	unsigned n = 0;

	/* four words per report; the ring holds four */
	while (n < 4 && !(admac->r32(REG_REPORT_RING(ch)) & RING_EMPTY)) {
		uint32_t lo = admac->r32(REG_REPORT_READ(ch));
		uint32_t hi = admac->r32(REG_REPORT_READ(ch));
		(void)admac->r32(REG_REPORT_READ(ch));
		(void)admac->r32(REG_REPORT_READ(ch));
		if (counter != NULL)
			*counter = ((uint64_t)hi << 32) | lo;
		n++;
	}
	admac->w32(REG_CHAN_INTSTATUS(ch, irqIndex), STATUS_DESC_DONE);
	return n;
}

bool
SFAdmacChan::clearError(void)
{
	bool seen = false;

	if (admac->r32(REG_DESC_RING(ch)) & RING_ERR) {
		admac->w32(REG_DESC_RING(ch), RING_ERR);
		seen = true;
	}
	if (admac->r32(REG_REPORT_RING(ch)) & RING_ERR) {
		admac->w32(REG_REPORT_RING(ch), RING_ERR);
		seen = true;
	}
	if (admac->r32(REG_CHAN_INTSTATUS(ch, irqIndex)) & STATUS_ERR) {
		admac->w32(REG_CHAN_INTSTATUS(ch, irqIndex), STATUS_ERR);
		seen = true;
	}
	return seen;
}

uint32_t SFAdmacChan::txSramSize(void) const { return admac->r32(REG_TX_SRAM_SIZE); }
uint32_t SFAdmacChan::rxSramSize(void) const { return admac->r32(REG_RX_SRAM_SIZE); }

void
SFAdmacChan::status(uint32_t out[4]) const
{
	out[0] = admac->r32(REG_DESC_RING(ch));
	out[1] = admac->r32(REG_REPORT_RING(ch));
	out[2] = admac->r32(REG_CHAN_INTSTATUS(ch, irqIndex));
	out[3] = admac->r32(REG_RESIDUE(ch));
}
