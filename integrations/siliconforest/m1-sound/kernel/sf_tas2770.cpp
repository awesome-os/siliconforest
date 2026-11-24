// SPDX-License-Identifier: GPL-2.0-only
/*
 * TAS5770L / TAS2770 speaker amplifier.
 *
 * Ported from the Omacom Linux tree, sound/soc/codecs/tas2770.c and tas2770.h
 * (tas2770_reset, tas2770_codec_probe, tas2770_set_ivsense_transmit,
 * tas2770_set_pdm_transmit, tas2770_set_dai_tdm_slot, tas2770_set_dai_tdm_idle,
 * tas2770_set_fmt, tas2770_set_bitwidth, tas2770_set_samplerate,
 * tas2770_update_pwr_ctrl, sense_event, tas2770_read_die_temp):
 *   Copyright (C) 2016-2017 Texas Instruments Incorporated - https://www.ti.com/
 *   Authors: Tracy Yi <tracy-yi@ti.com>, Frank Shi <shifu0704@thundersoft.com>
 */
#include "sf_tas2770.h"

#include <IOKit/IOLib.h>
#include <sys/errno.h>

#define TAS2770_SW_RST          0x01
#define TAS2770_PWR_CTRL        0x02
#define TAS2770_PWR_CTRL_MASK   0x03
#define TAS2770_PWR_SENSE_OFF   0x0c    /* bit 3 current sense, bit 2 voltage sense: 1 is off */
#define TAS2770_PLAY_CFG_REG0   0x03    /* amplifier gain, bits 4:0 */
#define TAS2770_PLAY_CFG_REG2   0x05    /* digital volume */
#define TAS2770_TDM_CFG_REG0    0x0A
#define TAS2770_TDM_CFG_REG1    0x0B
#define TAS2770_TDM_CFG_REG2    0x0C
#define TAS2770_TDM_CFG_REG3    0x0D
#define TAS2770_TDM_CFG_REG5    0x0F
#define TAS2770_TDM_CFG_REG6    0x10
#define TAS2770_TDM_CFG_REG7    0x11
#define TAS2770_LAT_INT_REG0    0x24
#define TAS2770_LAT_INT_REG1    0x25
#define TAS2770_TEMP_MSB        0x29
#define TAS2770_TEMP_LSB        0x2A
#define TAS2770_DIN_PD          0x31
#define TAS2770_REV_AND_GPID    0x7D

int
SFTas2770::rd(uint8_t reg, uint8_t *value)
{
	int err = bus->readReg(addr, reg, value);
	if (err != 0 && error == 0)
		error = err;
	return err;
}

int
SFTas2770::wr(uint8_t reg, uint8_t value)
{
	int err = bus->writeReg(addr, reg, value);
	if (err != 0 && error == 0)
		error = err;
	return err;
}

int
SFTas2770::upd(uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t old = 0;
	int err = rd(reg, &old);
	uint8_t now = (uint8_t)((old & ~mask) | (value & mask));
	if (err == 0 && now != old)
		err = wr(reg, now);
	return err;
}

int
SFTas2770::init(unsigned playSlot, unsigned vmonSlot, unsigned imonSlot, unsigned pdmSlot, bool idlePulldown,
    uint8_t gain, uint8_t volume)
{
	int err;

	error = 0;
	if (playSlot > 15 || vmonSlot > 63 || imonSlot > 63 || pdmSlot > 63)
		return EINVAL;
	/* tas2770_reset() */
	err = wr(TAS2770_SW_RST, 0x01);
	IOSleep(2);
	if (err == 0)
		err = rd(TAS2770_REV_AND_GPID, &rev);
	/* tas2770_codec_probe(): where the sense words go in the frame */
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG5, 0x7f, (uint8_t)(0x40 | vmonSlot));
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG6, 0x7f, (uint8_t)(0x40 | imonSlot));
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG7, 0x7f, (uint8_t)(0x40 | pdmSlot));
	/* tas2770_set_dai_tdm_slot(): one slot for both channel selections, 32-bit slots */
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG3, 0xff, (uint8_t)(playSlot | (playSlot << 4)));
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG2, 0x03, 0x02);
	/* tas2770_set_dai_tdm_idle(pulldown): one amplifier per data line holds it low when idle */
	if (err == 0 && idlePulldown)
		err = upd(TAS2770_DIN_PD, 0x80, 0x80);
	/* "ASI1 Sel" = Left, as macaudio_set_speaker() forces it */
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG2, 0x30, 0x10);
	if (err == 0)
		err = upd(TAS2770_PLAY_CFG_REG0, 0x1f, gain);
	if (err == 0)
		err = wr(TAS2770_PLAY_CFG_REG2, volume);
	/* tas2770_set_fmt(I2S, inverted bit clock and frame): sample on the falling edge, one bit of offset */
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG1, 0x01, 0x01);
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG1, 0x3e, 1 << 1);
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG0, 0x01, 0x00);
	return err;
}

int
SFTas2770::setRate(uint32_t rate)
{
	int err;

	if (rate != 44100 && rate != 48000)
		return EINVAL;
	/* tas2770_set_bitwidth(S32), tas2770_set_samplerate() */
	err = upd(TAS2770_TDM_CFG_REG2, 0x0c, 0x0c);
	if (err == 0)
		err = upd(TAS2770_TDM_CFG_REG0, 0x20 | 0x0e, (uint8_t)((rate == 44100 ? 0x20 : 0x00) | 0x06));
	return err;
}

int
SFTas2770::power(unsigned state)
{
	int err;

	if (state > kSFTasShutdown)
		return EINVAL;
	err = upd(TAS2770_PWR_CTRL, TAS2770_PWR_CTRL_MASK, kSFTasShutdown);
	if (err == 0)
		err = upd(TAS2770_PWR_CTRL, TAS2770_PWR_SENSE_OFF, 0);
	if (err == 0 && state != kSFTasShutdown)
		err = upd(TAS2770_PWR_CTRL, TAS2770_PWR_CTRL_MASK, (uint8_t)state);
	return err;
}

int
SFTas2770::setVolume(uint8_t reg)
{
	return wr(TAS2770_PLAY_CFG_REG2, reg);
}

int
SFTas2770::faults(uint8_t *lat0, uint8_t *lat1)
{
	int err = rd(TAS2770_LAT_INT_REG0, lat0);
	if (err == 0)
		err = rd(TAS2770_LAT_INT_REG1, lat1);
	return err;
}

int
SFTas2770::dieTemp(int32_t *millicelsius)
{
	uint8_t msb = 0, lsb = 0;
	int err = rd(TAS2770_TEMP_MSB, &msb);
	int reading;

	if (err == 0)
		err = rd(TAS2770_TEMP_LSB, &lsb);
	if (err != 0)
		return err;
	reading = (msb << 4) | (lsb >> 4);
	if (reading == 0)
		return ENODATA;
	*millicelsius = (reading - 1488) * 1000 / 16;
	return 0;
}
