/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cirrus Logic CS42L83 headphone codec (the CS42L42 with other reset defaults) on an
 * Apple I2C bus: playback path only, as an I2S clock consumer.
 *
 * Two callers at most: the playback path (configure, powerUp, setVolume, mute,
 * powerDown, status, inputLocked), serialised by its owner, and the jack path
 * (jackStart, jackService, jackSelfTest), serialised by jackLock. Register access is
 * atomic (regLock covers the page register) and the two paths write different
 * registers, as the Linux driver's irq_lock and DAPM do.
 */
#ifndef _SF_CS42L83_H
#define _SF_CS42L83_H

#include "sf_gpio.h"
#include "sf_i2c.h"

enum { kSFJackNone = 0, kSFJackHeadphone = 1, kSFJackHeadset = 2 };
/* headset buttons, as Linux reports them: A play/pause, B volume up, C volume down, D voice */
enum { kSFJackButtonA = 1, kSFJackButtonB = 2, kSFJackButtonC = 4, kSFJackButtonD = 8 };

struct SFCs42l83Status {
	uint8_t pllLock;        /* bit 0: locked */
	uint8_t aspRx;          /* sticky: 0 no LRCK, 1 early, 2 late, 3 error, 4 overload */
	uint8_t src;            /* sticky sample-rate converter lock/unlock events */
	uint8_t det1;           /* detect status 1 */
	uint8_t oscSwitch;      /* which clock the digital core runs from */
	uint8_t pwr1, hpCtl, volA, volB, aspClk, mclkSrc, pllCtl1;
	int     error;          /* errno of the first failed read, 0 when all were read */
};

struct SFCs42l83 {
	SFI2C   *bus;
	uint8_t  addr;
	int      page;          /* page register as last written, -1 unknown */
	uint32_t devid;
	uint8_t  revid;
	bool     ready;         /* identified and initialised */
	bool     onSclk;        /* running from the I2S bit clock */
	unsigned pll;           /* entry of the PLL table in use */
	IOLock  *regLock, *jackLock;

	/* jack state, under jackLock */
	uint8_t  plugState;     /* tip sense: 3 plugged, 0 unplugged, 1 moving */
	uint8_t  hsType;        /* 0 CTIA headset, 1 OMTP headset, 2 headphone, 3 none or invalid */
	uint8_t  jack;          /* kSFJack... as reported */
	uint8_t  buttons;       /* kSFJackButton... held now */
	bool     remoteActive;  /* Apple inline remote receiver in use */
	uint8_t  remoteSaved[9];
	uint32_t irqServices;   /* jackService() calls that found the chip readable */
	uint32_t plugEvents, unplugEvents, buttonEvents, detectsDone;
	/* called under jackLock when `jack` or `buttons` changed */
	void   (*changed)(SFCs42l83 *codec, uint8_t pressed);

	SFCs42l83() : bus(NULL), addr(0), page(-1), devid(0), revid(0), ready(false), onSclk(false), pll(0),
	    regLock(NULL), jackLock(NULL), plugState(0), hsType(3), jack(0), buttons(0), remoteActive(false),
	    irqServices(0), plugEvents(0), unplugEvents(0), buttonEvents(0), detectsDone(0), changed(NULL) {}

	int read(uint16_t reg, uint8_t *value);
	int write(uint16_t reg, uint8_t value);
	int update(uint16_t reg, uint8_t mask, uint8_t value);

	/*
	 * Pulses the reset pin, checks the device ID and leaves the chip initialised, all
	 * blocks but the detection logic powered down, headphone output muted.
	 */
	int probe(SFI2C *bus, uint8_t addr, SFGpio *resetGpio, unsigned resetPin);
	/* I2S, two 32-bit slots, 64 bit clocks per frame at `rate` (44100 or 48000). */
	int configure(uint32_t rate);
	/*
	 * Powers the playback path and moves the chip onto the bit clock, which must be
	 * running. The output stays muted. attenuation: 0..62 dB below full scale.
	 */
	int powerUp(unsigned attenuation);
	int setVolume(unsigned attenuation);
	int mute(bool on);
	/* Mutes, returns to the internal oscillator (the bit clock must still run) and powers down. */
	int powerDown(void);
	void status(SFCs42l83Status *out);
	/*
	 * True once the input sample-rate converter has locked to the incoming frames
	 * (reads the sticky converter status, which clears it).
	 */
	bool inputLocked(void);
	/*
	 * The headset microphone path: the converter and the serial output, two 32-bit
	 * slots carrying the one channel. Only while the chip runs from the bit clock
	 * (after powerUp); powerDown turns it off as well.
	 */
	int capture(bool on);

	/*
	 * Unmasks the jack interrupts (cs42l42_set_interrupt_masks) and, when a plug is
	 * already in, starts its type detection. With `selfTest`, the power-down-done
	 * interrupt is left unmasked until the first jackService(): the chip then raises
	 * its interrupt line at once, which proves the line without a plug.
	 */
	int jackStart(bool selfTest);
	/*
	 * The interrupt thread of the Linux driver (cs42l42_irq_thread): reads and clears
	 * the sticky status, follows plug, unplug, type detection and buttons. Sleeps up
	 * to about a second (type detection, the Apple remote handshake).
	 */
	void jackService(void);
	/* Runs the type detection once although no plug event came; false if a plug is in. */
	bool jackSelfTest(void);

private:
	void manualTypeDetect(void);
	void processTypeDetect(void);
	void initTypeDetect(void);
	void cancelTypeDetect(void);
	uint8_t buttonPress(void);
	int  remoteSetDetect(uint8_t value);
	void remoteStart(void);
	void remoteRestore(bool settle);
	void remoteButtons(uint8_t int1, uint8_t int2);
	void report(uint8_t newJack, uint8_t newButtons, uint8_t buttonMask);
};

#endif
