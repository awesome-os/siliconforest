/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * TI TAS5770L / TAS2770 speaker amplifier on an Apple I2C bus, as a TDM clock consumer
 * with current and voltage sense. Callers serialise the calls on each amplifier.
 *
 * The amplifier gain and the digital volume are written only by the owner of the
 * speaker policy (sf_speakers.cpp); this file has no opinion on their values.
 */
#ifndef _SF_TAS2770_H
#define _SF_TAS2770_H

#include "sf_i2c.h"

enum { kSFTasActive = 0, kSFTasMute = 1, kSFTasShutdown = 2 };

/* latched fault bits that mean the amplifier protected itself */
#define SF_TAS_FAULT0_MASK 0x03     /* over-current, over-temperature */
#define SF_TAS_FAULT1_MASK 0x0e     /* supply over-voltage, under-voltage, brown-out */

struct SFTas2770 {
	SFI2C      *bus;
	uint8_t     addr;
	const char *name;
	uint8_t     rev;        /* revision and PG id register */
	int         error;      /* errno of the first failed transfer since init(), 0 if none */

	SFTas2770() : bus(NULL), addr(0), name("?"), rev(0), error(0) {}

	int rd(uint8_t reg, uint8_t *value);
	int wr(uint8_t reg, uint8_t value);
	int upd(uint8_t reg, uint8_t mask, uint8_t value);

	/*
	 * Software reset, then the configuration Linux gives the chip before a stream:
	 * sense slots, the playback slot (32-bit slots), I2S with inverted clocks, the
	 * data-out pull-down when asked, gain register `gain`, volume register `volume`.
	 * The chip is left in software shutdown with both senses off (its reset state).
	 */
	int init(unsigned playSlot, unsigned vmonSlot, unsigned imonSlot, unsigned pdmSlot, bool idlePulldown,
	    uint8_t gain, uint8_t volume);
	int setRate(uint32_t rate);     /* 44100 or 48000, 32-bit samples */
	/*
	 * Power state, with current and voltage sense on. The sense bits are changed in
	 * shutdown and the state set afterwards, as the Linux driver's sense_event() does:
	 * without that trip the senses can stay dead.
	 */
	int power(unsigned state);
	int setVolume(uint8_t reg);     /* PLAY_CFG_REG2: 0 is 0 dB, 0.5 dB down per step, 0xc8 is -100 dB */
	/* Reads and thereby clears the latched interrupt registers. */
	int faults(uint8_t *lat0, uint8_t *lat1);
	/* Die temperature in millidegrees; ENODATA before the chip first left shutdown. */
	int dieTemp(int32_t *millicelsius);
};

#endif
