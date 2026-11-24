/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Pins of an Apple GPIO block ("gpio,t8101": /arm-io/gpio, /arm-io/nub-gpio, ...), by
 * device tree node and pin number. Register layout from Linux
 * drivers/pinctrl/pinctrl-apple-gpio.c. Pin interrupts are delivered by whoever owns the
 * block's AIC interrupt (see irqSetup).
 *
 * Each call is one read-modify-write of the pin's own register; callers that share a
 * pin serialise among themselves.
 */
#ifndef _SF_GPIO_H
#define _SF_GPIO_H

#include "sf_soc.h"

struct SFGpio {
	SFMmio   regs;
	uint32_t npins;

	SFGpio() : npins(0) {}

	bool attach(const char *path);          /* e.g. "/arm-io/nub-gpio" */
	uint32_t raw(unsigned pin) const;       /* the pin's configuration register */
	int  get(unsigned pin) const;           /* 0 or 1; -1 for a bad pin */
	void output(unsigned pin, int value);   /* GPIO function, driven */
	void input(unsigned pin);               /* GPIO function, input, interrupt off */
	void set(unsigned pin, int value);      /* value of a pin that is already an output */
	void function(unsigned pin, unsigned func); /* peripheral function 1..3 (0 is GPIO) */

	/*
	 * Pin interrupts. A pin is put into one of the block's interrupt groups (group g is the
	 * block's AIC interrupt number g). Linux uses group 0 for every pin; this copy takes the
	 * group as an argument because SFAppleSound owns the group 0 interrupt of /arm-io/gpio
	 * and an AIC interrupt has one owner. The owner calls irqPending() for its pins;
	 * for a level interrupt it masks the pin there and unmasks it once the device has
	 * been serviced. irqMask/irqAck/irqPending are safe in interrupt context.
	 */
	enum { kIrqLevelHigh = 2, kIrqLevelLow = 3, kIrqRising = 4, kIrqFalling = 5, kIrqAny = 6 };
	void irqSetup(unsigned pin, unsigned group); /* input, that group, masked */
	void irqUnmask(unsigned pin, unsigned type);
	void irqMask(unsigned pin);
	void irqAck(unsigned pin, unsigned group);
	bool irqPending(unsigned pin, unsigned group) const;
	void irqAckAll(unsigned group);             /* every latched pin of the group */
};

#endif
