// SPDX-License-Identifier: GPL-2.0-only
/*
 * Apple GPIO pins. Ported from Linux drivers/pinctrl/pinctrl-apple-gpio.c
 * (apple_gpio_get, apple_gpio_set, apple_gpio_direction_input/output,
 * apple_gpio_pinmux_set, apple_gpio_irq_startup/mask/unmask/ack, apple_gpio_irq_handler):
 *   Copyright (C) The Asahi Linux Contributors
 *   Copyright (C) 2020 Corellium LLC
 *   Based on: pinctrl-pistachio.c, Copyright (C) 2014 Imagination Technologies Ltd.,
 *   Copyright (C) 2014 Google, Inc.
 */
#include "sf_gpio.h"

#include <IOKit/IOLib.h>

#define REG_GPIO(x)            (4 * (x))
#define REG_GPIOx_DATA         (1u << 0)
#define REG_GPIOx_MODE         (7u << 1)
#define REG_GPIOx_OUT          1
#define REG_GPIOx_IN_IRQ_OFF   7
#define REG_GPIOx_PERIPH       (3u << 5)
#define REG_GPIOx_INPUT_ENABLE (1u << 9)
#define REG_GPIOx_GRP          (7u << 16)
#define REG_IRQ(g, x)          (0x800 + 0x40 * (g) + 4 * ((x) >> 5))

bool
SFGpio::attach(const char *path)
{
	if (!sf_adt_u32(path, "#gpio-pins", 0, &npins) || npins == 0 || npins > 1024) {
		IOLog("SFGpio: %s has no usable #gpio-pins\n", path);
		return false;
	}
	return regs.mapNode(path, 0);
}

uint32_t
SFGpio::raw(unsigned pin) const
{
	return pin < npins && regs.va ? regs.r32(REG_GPIO(pin)) : 0;
}

int
SFGpio::get(unsigned pin) const
{
	if (pin >= npins || regs.va == NULL)
		return -1;
	return (regs.r32(REG_GPIO(pin)) & REG_GPIOx_DATA) ? 1 : 0;
}

void
SFGpio::output(unsigned pin, int value)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_PERIPH | REG_GPIOx_MODE | REG_GPIOx_DATA,
	    (REG_GPIOx_OUT << 1) | (value ? REG_GPIOx_DATA : 0));
}

void
SFGpio::input(unsigned pin)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_PERIPH | REG_GPIOx_MODE | REG_GPIOx_DATA | REG_GPIOx_INPUT_ENABLE,
	    (REG_GPIOx_IN_IRQ_OFF << 1) | REG_GPIOx_INPUT_ENABLE);
}

void
SFGpio::set(unsigned pin, int value)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_DATA, value ? REG_GPIOx_DATA : 0);
}

void
SFGpio::function(unsigned pin, unsigned func)
{
	if (pin >= npins || regs.va == NULL || func > 3)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_PERIPH | REG_GPIOx_INPUT_ENABLE, (func << 5) | REG_GPIOx_INPUT_ENABLE);
}

void
SFGpio::irqSetup(unsigned pin)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_GRP, 0);
	input(pin);
}

void
SFGpio::irqUnmask(unsigned pin, unsigned type)
{
	if (pin >= npins || regs.va == NULL || type < kIrqLevelHigh || type > kIrqAny)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_MODE, type << 1);
}

void
SFGpio::irqMask(unsigned pin)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.mask32(REG_GPIO(pin), REG_GPIOx_MODE, REG_GPIOx_IN_IRQ_OFF << 1);
}

void
SFGpio::irqAck(unsigned pin)
{
	if (pin >= npins || regs.va == NULL)
		return;
	regs.w32(REG_IRQ(0, pin), 1u << (pin % 32));
}

bool
SFGpio::irqPending(unsigned pin) const
{
	if (pin >= npins || regs.va == NULL)
		return false;
	return (regs.r32(REG_IRQ(0, pin)) & (1u << (pin % 32))) != 0;
}
