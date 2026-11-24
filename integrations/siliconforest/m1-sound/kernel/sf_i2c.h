/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Apple (PA Semi core) I2C controller, polled master transfers. One instance per
 * device tree node ("/arm-io/i2c2", ...). Not tied to the audio driver.
 *
 * Transfers sleep (thread context only) and are serialised by the instance's lock,
 * so any number of client threads may share a bus.
 */
#ifndef _SF_I2C_H
#define _SF_I2C_H

#include "sf_soc.h"
#include <IOKit/IOLocks.h>

struct SFI2CMsg {
	uint8_t  addr;      /* 7-bit */
	bool     read;
	uint8_t *buf;
	unsigned len;       /* 1..255 */
};

struct SFI2C {
	SFMmio   regs;
	IOLock  *lock;
	uint32_t clkDiv;
	uint32_t hwRev;
	uint32_t errors;    /* transfers that failed */
	uint32_t transfers;

	SFI2C() : lock(NULL), clkDiv(0), hwRev(0), errors(0), transfers(0) {}

	/*
	 * Powers the controller (the node's clock-gates), maps it and resets it for
	 * `hz` on the bus. The pins' multiplexing is the caller's business.
	 */
	bool attach(const char *path, uint32_t hz);
	/* All messages as one transaction (repeated start between them). 0 or errno. */
	int transfer(SFI2CMsg *msgs, unsigned count);
	/* 8-bit register helpers */
	int readReg(uint8_t addr, uint8_t reg, uint8_t *value);
	int writeReg(uint8_t addr, uint8_t reg, uint8_t value);
};

#endif
