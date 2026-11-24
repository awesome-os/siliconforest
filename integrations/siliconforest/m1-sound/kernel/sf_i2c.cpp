// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple I2C controller, polled. Ported from Linux drivers/i2c/busses/
 * i2c-pasemi-core.c (pasemi_reset, pasemi_smb_clear, pasemi_smb_waitready,
 * pasemi_i2c_xfer_msg, pasemi_i2c_xfer) and i2c-pasemi-platform.c (the clock divider):
 *   Copyright (C) 2006-2007 PA Semi, Inc
 *   Copyright (C) 2021 The Asahi Linux Contributors
 */
#include "sf_i2c.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define LOG(fmt, ...) IOLog("SFI2C: " fmt "\n", ##__VA_ARGS__)

#define REG_MTXFIFO 0x00
#define REG_MRXFIFO 0x04
#define REG_XFSTA   0x0c
#define REG_SMSTA   0x14
#define REG_IMASK   0x18
#define REG_CTL     0x1c
#define REG_REV     0x28

#define MTXFIFO_READ   (1u << 10)
#define MTXFIFO_STOP   (1u << 9)
#define MTXFIFO_START  (1u << 8)
#define MRXFIFO_EMPTY  (1u << 8)

#define SMSTA_XIP  (1u << 28)
#define SMSTA_XEN  (1u << 27)
#define SMSTA_JMD  (1u << 25)
#define SMSTA_JAM  (1u << 24)
#define SMSTA_MTO  (1u << 23)
#define SMSTA_MTA  (1u << 22)
#define SMSTA_MTN  (1u << 21)
#define SMSTA_MRNE (1u << 19)
#define SMSTA_MTE  (1u << 16)
#define SMSTA_TOM  (1u << 6)

#define CTL_EN   (1u << 11)
#define CTL_MRR  (1u << 10)
#define CTL_MTR  (1u << 9)
#define CTL_UJM  (1u << 8)

/* The hardware gives up on a stretched clock after 25 ms; 100 ms is plenty. */
#define TRANSFER_TIMEOUT_MS 100
/* Reference clock of the controller (the device tree's clkref). */
#define I2C_REF_HZ 24000000u

static void
i2c_reset(SFI2C *bus)
{
	uint32_t val = CTL_MTR | CTL_MRR | CTL_UJM | (bus->clkDiv & 0xff);
	if (bus->hwRev >= 6)
		val |= CTL_EN;
	bus->regs.w32(REG_CTL, val);
}

static int
i2c_clear(SFI2C *bus)
{
	uint32_t status = 0;
	int i;

	/* wait for the bus to go idle */
	for (i = 0; i < TRANSFER_TIMEOUT_MS; i++) {
		status = bus->regs.r32(REG_SMSTA);
		if (!(status & (SMSTA_XIP | SMSTA_JAM)))
			break;
		IOSleep(1);
	}
	if (i == TRANSFER_TIMEOUT_MS) {
		LOG("bus is stuck (status 0x%08x xfstatus 0x%08x)", status, bus->regs.r32(REG_XFSTA));
		return EIO;
	}
	/* after any error, or with data left in the FIFOs, reset them */
	if ((status & (SMSTA_MRNE | SMSTA_JMD | SMSTA_MTO | SMSTA_TOM | SMSTA_MTN | SMSTA_MTA)) || !(status & SMSTA_MTE))
		i2c_reset(bus);
	bus->regs.w32(REG_SMSTA, status);
	return 0;
}

static int
i2c_wait(SFI2C *bus)
{
	uint32_t status = 0;
	int i;

	/* a byte at 100 kHz takes 90 us: spin briefly, then sleep */
	for (i = 0; i < 200 + TRANSFER_TIMEOUT_MS; i++) {
		status = bus->regs.r32(REG_SMSTA);
		if (status & (SMSTA_XEN | SMSTA_MTN | SMSTA_TOM | SMSTA_MTO | SMSTA_MTA))
			break;
		if (i < 200)
			IODelay(50);
		else
			IOSleep(1);
	}
	if (!(status & SMSTA_XEN) && !(status & (SMSTA_MTN | SMSTA_TOM | SMSTA_MTO | SMSTA_MTA)))
		return ETIMEDOUT;
	if (status & SMSTA_TOM)
		return EIO;
	if (status & SMSTA_MTO)
		return ETIMEDOUT;
	if (status & SMSTA_MTA)
		return EBUSY;
	if (status & SMSTA_MTN)
		return ENXIO;       /* no acknowledge */
	if (status & SMSTA_XIP)
		return EIO;
	bus->regs.w32(REG_SMSTA, SMSTA_XEN);
	return 0;
}

static int
i2c_msg(SFI2C *bus, const SFI2CMsg *msg, bool stop)
{
	int err = 0;

	bus->regs.w32(REG_MTXFIFO, MTXFIFO_START | (uint32_t)(msg->addr << 1) | (msg->read ? 1 : 0));
	if (msg->read) {
		bus->regs.w32(REG_MTXFIFO, msg->len | MTXFIFO_READ | (stop ? MTXFIFO_STOP : 0));
		err = i2c_wait(bus);
		for (unsigned i = 0; err == 0 && i < msg->len; i++) {
			uint32_t rd = bus->regs.r32(REG_MRXFIFO);
			if (rd & MRXFIFO_EMPTY)
				err = ENODATA;
			else
				msg->buf[i] = (uint8_t)rd;
		}
	} else {
		for (unsigned i = 0; i + 1 < msg->len; i++)
			bus->regs.w32(REG_MTXFIFO, msg->buf[i]);
		bus->regs.w32(REG_MTXFIFO, msg->buf[msg->len - 1] | (stop ? MTXFIFO_STOP : 0));
		if (stop)
			err = i2c_wait(bus);
	}
	if (err != 0)
		i2c_reset(bus);
	return err;
}

bool
SFI2C::attach(const char *path, uint32_t hz)
{
	if (lock == NULL)
		lock = IOLockAlloc();
	if (lock == NULL || hz == 0)
		return false;
	if (sf_pmgr_enable_node(path, -1, 0, true) != 0)
		return false;
	if (!regs.mapNode(path, 0))
		return false;
	clkDiv = (I2C_REF_HZ + 16 * hz - 1) / (16 * hz);
	if (clkDiv < 4 || clkDiv > 0xff) {
		LOG("%s: %u Hz is out of range", path, hz);
		return false;
	}
	hwRev = regs.r32(REG_REV);
	regs.w32(REG_IMASK, 0);
	i2c_reset(this);
	LOG("%s at 0x%llx: revision %u, divider %u, status 0x%08x", path, regs.phys, hwRev, clkDiv, regs.r32(REG_SMSTA));
	return true;
}

int
SFI2C::transfer(SFI2CMsg *msgs, unsigned count)
{
	if (regs.va == NULL || count == 0)
		return ENXIO;
	for (unsigned i = 0; i < count; i++)
		if (msgs[i].len == 0 || msgs[i].len > 255 || msgs[i].buf == NULL || msgs[i].addr > 0x7f)
			return EINVAL;
	IOLockLock(lock);
	int err = i2c_clear(this);
	for (unsigned i = 0; err == 0 && i < count; i++)
		err = i2c_msg(this, &msgs[i], i == count - 1);
	transfers++;
	if (err != 0)
		errors++;
	IOLockUnlock(lock);
	return err;
}

int
SFI2C::readReg(uint8_t addr, uint8_t reg, uint8_t *value)
{
	SFI2CMsg msgs[2] = { { addr, false, &reg, 1 }, { addr, true, value, 1 } };
	return transfer(msgs, 2);
}

int
SFI2C::writeReg(uint8_t addr, uint8_t reg, uint8_t value)
{
	uint8_t buf[2] = { reg, value };
	SFI2CMsg msg = { addr, false, buf, 2 };
	return transfer(&msg, 1);
}
