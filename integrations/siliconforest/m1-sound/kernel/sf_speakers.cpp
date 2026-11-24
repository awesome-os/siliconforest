// SPDX-License-Identifier: GPL-2.0-only
/*
 * The built-in speakers of the J293 and the rules that keep them from being damaged.
 *
 * Hardware, as the Omacom Linux device tree (t8103-j293.dts) describes it: four
 * TAS5770L amplifiers, left front and left rear on i2c1 (0x32, 0x31) and the I2S port
 * of cluster 0, right front and right rear on i2c3 (0x35, 0x34) and the port of
 * cluster 1; one shutdown line for all (gpio pin 181, low = hardware shutdown). The
 * stream is Linux's "Secondary" front end: cluster 1 provides the clocks to both
 * ports, 8 slots of 32 bits (256 bit clocks per frame), one slot per amplifier in the
 * order left front, right front, left rear, right rear. Each amplifier returns its
 * current and voltage in 16-bit slots on its port's input line; cluster 2 captures
 * them as a clock consumer (Linux's "Speaker Sense" front end).
 *
 * What keeps the speakers safe is all in this file and in sf_speaker_model.c, in the
 * kernel; nothing in userspace can raise a limit:
 *
 *  1. The amplifier gain register is written by this file only, always SPK_AMP_GAIN
 *     (18.5 dBV, the cap of Linux's macaudio_j293_cfg). Nothing can change it.
 *  2. The digital volume of every amplifier is written by this file only. It is what
 *     the voice-coil temperature model allows (sf_speaker_model.c: the model of the
 *     Asahi speakersafetyd, fed with the amplifiers' current and voltage sense while
 *     they play), and -100 dB whenever the model may not be used: when it is switched
 *     off (kModelLiftsClamp, the boot argument sfspeaker_model=0), and after a fault.
 *     Linux has the same model in a daemon and holds -100 dB without it (macaudio.c,
 *     macaudio_vlimit_unlock); here there is no daemon and no interface to unlock.
 *  3. The model is only as good as the sense data, so that is checked while playing:
 *     every session starts SPK_PROBE_STEPS (20 dB) down and is released, half a dB per
 *     period, only once the sensed voltage has followed the played signal and the
 *     sensed current fits a speaker; afterwards a played signal that the voltage sense
 *     does not show, a voltage without current, a sense stream that stops, a mean power
 *     below zero (the reference's check) or a modelled temperature above the limit
 *     plus headroom shuts the amplifiers down.
 *  4. While the speakers play, the amplifiers' latched fault bits, their power state
 *     and the gain and volume registers are read back four times a second. A fault, a
 *     register that is not what was written, any failed I2C transfer, a DMA ring error
 *     or a ring that ran dry, or a DART fault shuts all four amplifiers down (software
 *     shutdown, then the hardware shutdown line) and stops the stream.
 *     After SPK_MAX_TRIPS shutdowns the speakers stay off until the next boot.
 *  5. Between sessions the amplifiers are in hardware shutdown; every session starts
 *     from a software reset. /dev/sfspeaker takes samples only; the sysctls are
 *     read-only apart from the test tone and the forced shutdown.
 *
 * Interfaces:
 *   /dev/sfspeaker          write-only, one writer: signed 16-bit little-endian stereo
 *                           at kern.sf_speaker.rate; left goes to both left
 *                           amplifiers, right to both right ones
 *   kern.sf_speaker.*       state, counters, sense statistics, the model's temperatures
 *                           and limit; tone=<seconds> plays the test tone; hold_probe=1
 *                           keeps the volume 20 dB down for quiet tests
 *
 * Locking: one mutex for everything here; one thread services the DMA rings every
 * 5 ms while playing. No interrupts.
 */
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <miscfs/devfs/devfs.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <pexpert/pexpert.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include "sf_i2c.h"
#include "sf_mca.h"
#include "sf_speaker_model.h"
#include "sf_speakers.h"
#include "sf_tas2770.h"

#define LOG(fmt, ...) IOLog("SFSpeakers: " fmt "\n", ##__VA_ARGS__)

#define PIN_SPEAKER_SDZ     181     /* /arm-io/gpio: high lets the amplifiers run */
#define SFSPK_SPEAKERS      SF_SPK_MODEL_SPEAKERS
#define SFSPK_SENSE_CHANNELS SF_SPK_MODEL_CHANNELS
#define SPK_AMP_GAIN        0x0f    /* 18.5 dBV; dBV = 11 + 0.5 * value */
#define SPK_STEPS_LOCKED    200     /* register 0xc8: -100 dB */
#define SPK_PROBE_STEPS     40      /* 20 dB down until the sense data is believed */
/*
 * Optional additional cap, disabled by default by the owner's 2026-11-16 decision:
 * the current speaker model passes its comparison tests without violations. The boot
 * argument sfspeaker_cap_steps=<n> enables a floor (25 = -12.5 dB, control176 on the
 * daemon's scale; 0 disables this extra cap; above200 is200). The model/probe limits
 * remain in force, and nothing at run time lowers an enabled cap.
 */
#define SPK_CAP_STEPS       0
#define SPK_VOLUME_REG(steps) ((uint8_t)(steps))       /* PLAY_CFG_REG2: half-dB steps below 0 dB */
/* a full-scale sample at 0 dB is 10^(18.5/20) V RMS * sqrt(2) = 11.9 V peak; the voltage sense reads 14 V at 32768 */
#define SPK_FULL_SCALE_COUNTS 27853
/*
 * The checks compare mean magnitudes over a period, not peaks: the capture starts with a
 * few stray samples (a full-scale one was seen), which a mean of 1024 samples hardly notices.
 * Idle sense noise has a mean magnitude of about 20.
 */
#define SPK_CONFIRM_COUNTS  100     /* expected mean voltage magnitude from which the sense is compared, of 32768 */
#define SPK_DEAD_COUNTS     300     /* sustained expected magnitude at which a silent voltage sense is a fault */
#define SPK_SETTLE_PERIODS  4       /* sense periods at the start of a session that are not judged */
/* current counts per voltage count for a 128 ohm load (14 / 3.75 / 128), in 1/4096: less current is no speaker */
#define SPK_MIN_CURRENT_Q12 119
#define SPK_HISTORY         12      /* periods of history for those checks: a quarter of a second */
#define SPK_FE_CLUSTER      1       /* Linux's Secondary front end */
#define SPK_PORTS           0x3     /* the I2S ports of clusters 0 and 1 */
#define SPK_SLOTS           8
#define SPK_SENSE_CLUSTER   2
#define SPK_SENSE_SLOTS     16
#define SPK_MAX_TRIPS       3

#define PERIOD_FRAMES       1024
#define PLAY_FRAME_BYTES    16      /* 4 x 32 bits */
#define SENSE_FRAME_BYTES   16      /* 8 x 16 bits */
#define PERIOD_BYTES        (PERIOD_FRAMES * 16)
#define RING_PERIODS        8
#define SPK_SENSE_LAG       4       /* played periods the model may be behind before the stream stops */
#define FIFO_FRAMES         16384   /* writer -> thread, stereo */
#define TRAILING_SILENCE    6
#define CHECK_TICKS         50      /* amplifier registers every 250 ms */
#define ADMAC_COUNTER_HZ    24000000ULL
#define MIN_TONE_DB         20      /* the test tone is never louder than this below full scale */

static const struct {
	const char *name;
	unsigned    bus;        /* 0: i2c1, 1: i2c3 */
	uint8_t     addr;
	uint8_t     slot, imon, vmon, pdm;
	bool        pulldown;   /* dai-tdm-idle-mode = "pulldown": one amplifier per data line */
} kAmps[SFSPK_SPEAKERS] = {
	{ "left front",  0, 0x32, 0, 0,  2,  4,  true },
	{ "right front", 1, 0x35, 1, 4,  6,  8,  true },
	{ "left rear",   0, 0x31, 2, 8,  10, 12, false },
	{ "right rear",  1, 0x34, 3, 12, 14, 16, false },
};

enum { kOff, kFailed, kIdle, kPlaying, kDisabled };
static const char *const kStateNames[] = { "off", "failed", "idle", "playing", "disabled after faults" };

static IOLock      *gLock;
static SFGpio      *gGpio;
static SFDartFixed *gDart;
static SFI2C        gBus[2];
static SFTas2770    gAmp[SFSPK_SPEAKERS];
static SFMca        gMca;
static SFMcaCapture gSense;
static uint8_t     *gPlayRing, *gSenseRing;
static uint64_t     gPlayIova, gSenseIova;
static int          gState = kOff;
static char         gFailure[96];
static uint32_t     gRate = 48000;

/* the limit */
static const bool kModelLiftsClamp = true;      /* false: the model only watches, the volumes stay at -100 dB */
static uint32_t gModelOn = 1;                   /* sfspeaker_model= */
static struct sf_spk_model gModel;
static bool     gModelValid;
static uint32_t gHoldProbe;                     /* kern.sf_speaker.hold_probe: never release above the probe volume */
static uint32_t gCapSteps = SPK_CAP_STEPS;      /* sfspeaker_cap_steps=: the volume never goes above this */
static uint64_t gIdleStamp;                     /* uptime of the last idle step of the model */
/* per side (0 left, 1 right: amplifier index & 1), since a signal may be on one side only */
static uint32_t gSteps[2] = { SPK_STEPS_LOCKED, SPK_STEPS_LOCKED };     /* half-dB steps below 0 dB in the amplifiers now */
static bool     gSenseTrusted[2];               /* this session's sense data has followed the signal */
static uint32_t gConfirmRun[2];
static int32_t  gExpect[2][SPK_HISTORY];        /* per played period: mean |sample| as voltage sense counts at 0 dB */
/* per sense period: mean voltage and current magnitude of the side's weaker speaker */
static int32_t  gSeenV[2][SPK_HISTORY], gSeenI[2][SPK_HISTORY];
static uint32_t gExpectAt, gSeenAt;
static uint32_t gAttenQ16[64];                  /* 10^(-steps / 40) */

/* writer and tone */
static int32_t *gFifo;                  /* stereo frames of int32 */
static uint32_t gFifoHead, gFifoCount;
static bool     gDevOpen;
static int      gFifoEvent;
static uint64_t gToneLeft;
static uint32_t gTonePhase, gToneHz = 1000, gToneDb = 30;

/* sense */
static int32_t  gSensePeak[SFSPK_SENSE_CHANNELS];       /* largest |sample| of the last period */
static int32_t  gSenseMean[SFSPK_SENSE_CHANNELS];       /* mean of the last period (offset) */
static int32_t  gSensePeakSession[SFSPK_SENSE_CHANNELS];

/* one session */
static uint32_t gNext, gSenseNext, gSilentRun, gTicks;
static uint64_t gSubmitted, gDone, gSenseDone, gFirstCounter, gLastCounter, gDoneAtFirst;
static uint64_t gFramesData, gFramesTone;
static uint32_t gLate, gRingErrors;
/* since boot */
static uint32_t gSessions, gTrips, gLastRateMilliHz;
static uint64_t gTotalDone, gTotalSense;
static char     gTripReason[96];
static uint8_t  gAmpRev[SFSPK_SPEAKERS], gLat0[SFSPK_SPEAKERS], gLat1[SFSPK_SPEAKERS], gPwr[SFSPK_SPEAKERS],
                gGainSeen[SFSPK_SPEAKERS], gVolSeen[SFSPK_SPEAKERS];
static int32_t  gDieMilli[SFSPK_SPEAKERS] = { INT32_MIN, INT32_MIN, INT32_MIN, INT32_MIN };

static int   gMajor = -1;
static void *gNode;

static uint64_t
uptime_ns(void)
{
	uint64_t abs = 0, ns = 0;

	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns;
}

/* ---- amplifiers ----------------------------------------------------------------------- */

/* Software shutdown where the bus still answers, then the hardware shutdown line. Never fails. */
static void
amps_off(void)
{
	for (unsigned i = 0; i < SFSPK_SPEAKERS; i++)
		gAmp[i].upd(0x02, 0x03, kSFTasShutdown);
	gGpio->output(PIN_SPEAKER_SDZ, 0);
}

/* Out of hardware shutdown and configured, in software shutdown, at the volume the lock allows. */
static int
amps_on(void)
{
	int err = 0;

	gGpio->output(PIN_SPEAKER_SDZ, 1);
	IOSleep(2);
	for (unsigned i = 0; i < SFSPK_SPEAKERS && err == 0; i++) {
		err = gAmp[i].init(kAmps[i].slot, kAmps[i].vmon, kAmps[i].imon, kAmps[i].pdm, kAmps[i].pulldown,
		    SPK_AMP_GAIN, SPK_VOLUME_REG(gSteps[i & 1]));
		gAmpRev[i] = gAmp[i].rev;
		if (err != 0)
			LOG("amplifier %s (0x%02x) did not take its configuration (error %d)", kAmps[i].name, kAmps[i].addr, err);
	}
	return err;
}

/* Reads back what protects the speakers. 0, or the index + 1 of the amplifier that is not as it must be. */
static unsigned
amps_check(char *why, size_t len)
{
	for (unsigned i = 0; i < SFSPK_SPEAKERS; i++) {
		SFTas2770 &a = gAmp[i];
		uint8_t want = SPK_VOLUME_REG(gSteps[i & 1]);

		if (a.faults(&gLat0[i], &gLat1[i]) != 0 || a.rd(0x02, &gPwr[i]) != 0 || a.rd(0x03, &gGainSeen[i]) != 0 ||
		    a.rd(0x05, &gVolSeen[i]) != 0) {
			snprintf(why, len, "%s: I2C error %d", kAmps[i].name, a.error);
			return i + 1;
		}
		a.dieTemp(&gDieMilli[i]);
		if ((gLat0[i] & SF_TAS_FAULT0_MASK) || (gLat1[i] & SF_TAS_FAULT1_MASK)) {
			snprintf(why, len, "%s: fault latched 0x%02x/0x%02x", kAmps[i].name, gLat0[i], gLat1[i]);
			return i + 1;
		}
		if ((gPwr[i] & 0x03) != kSFTasActive || (gPwr[i] & 0x0c) != 0) {
			snprintf(why, len, "%s: power control 0x%02x, not playing with sense on", kAmps[i].name, gPwr[i]);
			return i + 1;
		}
		if ((gGainSeen[i] & 0x1f) != SPK_AMP_GAIN || gVolSeen[i] != want) {
			snprintf(why, len, "%s: gain 0x%02x volume 0x%02x, written 0x%02x and 0x%02x", kAmps[i].name,
			    gGainSeen[i], gVolSeen[i], SPK_AMP_GAIN, want);
			return i + 1;
		}
	}
	return 0;
}

/* ---- the limit ------------------------------------------------------------------------ */

static void trip(const char *why);

/* What the volume of a side's amplifiers must be now, in half-dB steps below 0 dB. */
static uint32_t
wanted_steps(unsigned side)
{
	uint32_t steps;

	if (!kModelLiftsClamp || !gModelOn || !gModelValid || gModel.fault != kSFSpkModelOk)
		return SPK_STEPS_LOCKED;
	steps = sf_spk_model_limit_steps(&gModel);
	if ((!gSenseTrusted[side] || gHoldProbe) && steps < SPK_PROBE_STEPS)
		steps = SPK_PROBE_STEPS;
	if (steps < gCapSteps)
		steps = gCapSteps;
	return steps > SPK_STEPS_LOCKED ? SPK_STEPS_LOCKED : steps;
}

/*
 * Moves the volumes to the limit: down at once, up half a dB per call (a period).
 * false when an amplifier did not take it.
 */
static bool
apply_limit(void)
{
	for (unsigned side = 0; side < 2; side++) {
		uint32_t want = wanted_steps(side), next = want >= gSteps[side] ? want : gSteps[side] - 1;

		if (next == gSteps[side])
			continue;
		gSteps[side] = next;
		if (gAmp[side].setVolume(SPK_VOLUME_REG(next)) != 0 || gAmp[side + 2].setVolume(SPK_VOLUME_REG(next)) != 0)
			return false;
	}
	return true;
}

/* The model while nothing plays: the speakers cool. */
static void
model_idle(void)
{
	uint64_t now = uptime_ns();

	if (gModelValid && gIdleStamp != 0 && now > gIdleStamp)
		sf_spk_model_idle(&gModel, now - gIdleStamp);
	gIdleStamp = now;
}

static int32_t
history_min(const int32_t *h)
{
	int32_t m = h[0];
	for (unsigned i = 1; i < SPK_HISTORY; i++)
		if (h[i] < m)
			m = h[i];
	return m;
}

static int32_t
history_max(const int32_t *h)
{
	int32_t m = h[0];
	for (unsigned i = 1; i < SPK_HISTORY; i++)
		if (h[i] > m)
			m = h[i];
	return m;
}

/*
 * Rule 3, after every sense period, for each side. The sense trails the played signal
 * by a few periods, so a quarter second of both is compared: the quietest played period
 * against the loudest sensed one. Returns the reason to shut down, or NULL.
 */
static const char *
sense_check(unsigned side)
{
	int32_t expect = history_min(gExpect[side]), seenV = history_max(gSeenV[side]), seenI = history_max(gSeenI[side]);
	int32_t lastV = gSeenV[side][(gSeenAt + SPK_HISTORY - 1) % SPK_HISTORY];
	int32_t lastI = gSeenI[side][(gSeenAt + SPK_HISTORY - 1) % SPK_HISTORY];

	/* the played signal as it leaves the amplifiers at the volume in force */
	expect = gSteps[side] > 63 ? 0 : (int32_t)(((int64_t)expect * gAttenQ16[gSteps[side]]) >> 16);
	if (expect >= SPK_DEAD_COUNTS && seenV < expect / 8)
		return side ? "right: the voltage sense does not show the played signal" :
		       "left: the voltage sense does not show the played signal";
	if (seenV >= SPK_DEAD_COUNTS && (int64_t)seenI * 4096 < (int64_t)seenV * SPK_MIN_CURRENT_Q12)
		return side ? "right: voltage without current: the current sense or a speaker is not there" :
		       "left: voltage without current: the current sense or a speaker is not there";
	if (!gSenseTrusted[side]) {
		/* three periods in a row with the signal in the voltage and a plausible current */
		if (expect >= SPK_CONFIRM_COUNTS && lastV >= expect / 4 && (int64_t)lastI * 4096 >= (int64_t)lastV * SPK_MIN_CURRENT_Q12)
			gConfirmRun[side]++;
		else
			gConfirmRun[side] = 0;
		if (gConfirmRun[side] >= 3) {
			gSenseTrusted[side] = true;
			LOG("%s sense data follows the signal (expected %d, voltage %d, current %d of 32768): limit released to the model's",
			    side ? "right" : "left", expect, lastV, lastI);
		}
	}
	return NULL;
}

/* ---- playback and sense --------------------------------------------------------------- */

static const uint32_t kGainQ16[6] = { 65536, 58409, 52057, 46396, 41350, 36854 };

static int32_t
tone_sample(void)
{
	uint32_t db = gToneDb < MIN_TONE_DB ? MIN_TONE_DB : gToneDb;
	uint32_t gain = db / 6 >= 16 ? 0 : kGainQ16[db % 6] >> (db / 6);
	int32_t t = (int32_t)(gTonePhase >> 16) - 32768, a = t < 0 ? -t : t;
	int32_t y = (int32_t)(((int64_t)4 * t * (32768 - a)) >> 15), ay = y < 0 ? -y : y;

	/* sin(pi t / 32768) in Q15: a parabola and one correction step */
	y += (int32_t)(((int64_t)7373 * ((int32_t)(((int64_t)y * ay) >> 15) - y)) >> 15);
	y = y > 32767 ? 32767 : y < -32767 ? -32767 : y;
	gTonePhase += (uint32_t)(((uint64_t)gToneHz << 32) / gRate);
	return (int32_t)((int64_t)y * gain);
}

static void
fill_period(uint32_t index)
{
	int32_t *dst = (int32_t *)(void *)(gPlayRing + index * PERIOD_BYTES);
	bool any = false;
	uint32_t taken = 0;
	int64_t mag[2] = { 0, 0 };

	for (unsigned i = 0; i < PERIOD_FRAMES; i++) {
		int32_t l = 0, r = 0;

		if (gFifoCount > 0) {
			l = gFifo[2 * gFifoHead];
			r = gFifo[2 * gFifoHead + 1];
			gFifoHead = (gFifoHead + 1) % FIFO_FRAMES;
			gFifoCount--;
			taken++;
			any = true;
		} else if (gToneLeft > 0) {
			l = r = tone_sample();
			gToneLeft--;
			gFramesTone++;
			any = true;
		}
		/* slots: left front, right front, left rear, right rear */
		dst[4 * i] = l;
		dst[4 * i + 1] = r;
		dst[4 * i + 2] = l;
		dst[4 * i + 3] = r;
		mag[0] += l < 0 ? -(int64_t)l : l;
		mag[1] += r < 0 ? -(int64_t)r : r;
	}
	gExpect[0][gExpectAt] = (int32_t)(((mag[0] / PERIOD_FRAMES) * SPK_FULL_SCALE_COUNTS) >> 31);
	gExpect[1][gExpectAt] = (int32_t)(((mag[1] / PERIOD_FRAMES) * SPK_FULL_SCALE_COUNTS) >> 31);
	gExpectAt = (gExpectAt + 1) % SPK_HISTORY;
	gFramesData += taken;
	gSilentRun = any ? 0 : gSilentRun + 1;
	if (taken > 0)
		IOLockWakeup(gLock, &gFifoEvent, false);
}

/* One captured period: statistics, the model, the checks of rule 3, the limit. */
static void
take_sense(uint32_t index)
{
	const int16_t *src = (const int16_t *)(const void *)(gSenseRing + index * PERIOD_BYTES);
	int32_t peak[SFSPK_SENSE_CHANNELS] = { 0 }, mag[SFSPK_SENSE_CHANNELS];
	int64_t sum[SFSPK_SENSE_CHANNELS] = { 0 }, abssum[SFSPK_SENSE_CHANNELS] = { 0 };
	const char *why;
	int fault;

	for (unsigned i = 0; i < PERIOD_FRAMES; i++) {
		for (unsigned ch = 0; ch < SFSPK_SENSE_CHANNELS; ch++) {
			int32_t x = src[SFSPK_SENSE_CHANNELS * i + ch];
			sum[ch] += x;
			if (x < 0)
				x = -x;
			abssum[ch] += x;
			if (x > peak[ch])
				peak[ch] = x;
		}
	}
	for (unsigned ch = 0; ch < SFSPK_SENSE_CHANNELS; ch++) {
		gSensePeak[ch] = peak[ch];
		gSenseMean[ch] = (int32_t)(sum[ch] / PERIOD_FRAMES);
		mag[ch] = (int32_t)(abssum[ch] / PERIOD_FRAMES);
		if (peak[ch] > gSensePeakSession[ch])
			gSensePeakSession[ch] = peak[ch];
	}
	/* channels: current, voltage of left front, right front, left rear, right rear; per side the weaker speaker counts */
	for (unsigned side = 0; side < 2; side++) {
		gSeenI[side][gSeenAt] = mag[2 * side] < mag[2 * side + 4] ? mag[2 * side] : mag[2 * side + 4];
		gSeenV[side][gSeenAt] = mag[2 * side + 1] < mag[2 * side + 5] ? mag[2 * side + 1] : mag[2 * side + 5];
		/* the first periods of a capture are not judged */
		if (gSenseDone < SPK_SETTLE_PERIODS)
			gSeenI[side][gSeenAt] = gSeenV[side][gSeenAt] = 0;
	}
	gSeenAt = (gSeenAt + 1) % SPK_HISTORY;

	if (!gModelValid)
		return;
	fault = sf_spk_model_run(&gModel, src, PERIOD_FRAMES);
	if (fault != kSFSpkModelOk) {
		char text[96];
		snprintf(text, sizeof(text), "model: %s on %s", fault == kSFSpkModelNegativePower ? "mean power below zero" :
		    fault == kSFSpkModelCoilTooHot ? "coil above the limit" : "magnet above the limit",
		    kAmps[gModel.fault_speaker].name);
		trip(text);
		return;
	}
	for (unsigned side = 0; side < 2 && kModelLiftsClamp && gModelOn && gSenseDone >= SPK_HISTORY; side++) {
		why = sense_check(side);
		if (why != NULL) {
			trip(why);
			return;
		}
	}
	if (!apply_limit())
		trip("I2C error while setting the volume");
}

static void
stop_streams(void)
{
	gSense.stop();
	gMca.stopDma();
	gMca.stopClocks();
	gDart->check("speakers");
}

/* Rule 4: everything off, now. */
static void
trip(const char *why)
{
	if (gState != kPlaying)
		return;
	amps_off();
	stop_streams();
	gTrips++;
	snprintf(gTripReason, sizeof(gTripReason), "%s", why);
	gToneLeft = 0;
	gFifoCount = 0;
	gState = gTrips >= SPK_MAX_TRIPS ? kDisabled : kIdle;
	gSteps[0] = gSteps[1] = SPK_STEPS_LOCKED;
	gIdleStamp = uptime_ns();
	IOLockWakeup(gLock, &gFifoEvent, false);
	LOG("SHUT DOWN: %s (stop %u of %u before the speakers stay off)", why, gTrips, SPK_MAX_TRIPS);
}

static void
start_playback(void)
{
	char why[96];
	int err;

	gNext = gSenseNext = gSilentRun = gTicks = 0;
	gSubmitted = gDone = gSenseDone = gFirstCounter = gLastCounter = gDoneAtFirst = 0;
	gFramesData = gFramesTone = 0;
	gLate = gRingErrors = 0;
	gTonePhase = 0;
	bzero(gSensePeakSession, sizeof(gSensePeakSession));
	bzero(gSenseRing, SF_SPK_SENSE_RING_BYTES);
	bzero(gExpect, sizeof(gExpect));
	bzero(gSeenV, sizeof(gSeenV));
	bzero(gSeenI, sizeof(gSeenI));
	gExpectAt = gSeenAt = 0;
	gConfirmRun[0] = gConfirmRun[1] = 0;
	gSenseTrusted[0] = gSenseTrusted[1] = false;
	/* the model up to now, then the volume this session starts at */
	model_idle();
	if (gModelValid && sf_spk_model_set_rate(&gModel, gRate) != 0)
		gModelValid = false;
	gSteps[0] = wanted_steps(0);
	gSteps[1] = wanted_steps(1);
	gState = kPlaying;      /* from here a failure goes through trip() */

	err = amps_on();
	if (err == 0)
		err = gMca.startClocks(gRate);
	for (unsigned i = 0; i < SFSPK_SPEAKERS && err == 0; i++)
		err = gAmp[i].setRate(gRate);
	/* powered and muted, sense on: the amplifiers see the clocks from here */
	for (unsigned i = 0; i < SFSPK_SPEAKERS && err == 0; i++)
		err = gAmp[i].power(kSFTasMute);
	if (err == 0)
		err = gSense.prepare();
	if (err != 0) {
		snprintf(why, sizeof(why), "could not start at %u Hz (error %d)", gRate, err);
		trip(why);
		return;
	}
	while (!gSense.descRingFull()) {
		gSense.submit(gSenseIova + (uint64_t)gSenseNext * PERIOD_BYTES, PERIOD_BYTES);
		gSenseNext = (gSenseNext + 1) % RING_PERIODS;
	}
	gSense.start();
	gMca.prepareDma();
	while (!gMca.descRingFull()) {
		fill_period(gNext);
		gMca.submit(gPlayIova + (uint64_t)gNext * PERIOD_BYTES, PERIOD_BYTES);
		gNext = (gNext + 1) % RING_PERIODS;
		gSubmitted++;
	}
	gMca.startDma();
	/* what latched while the clocks came up is not a fault of this session */
	for (unsigned i = 0; i < SFSPK_SPEAKERS; i++)
		gAmp[i].faults(&gLat0[i], &gLat1[i]);
	for (unsigned i = 0; i < SFSPK_SPEAKERS && err == 0; i++)
		err = gAmp[i].power(kSFTasActive);
	if (err != 0 || amps_check(why, sizeof(why)) != 0) {
		if (err != 0)
			snprintf(why, sizeof(why), "unmute failed (error %d)", err);
		trip(why);
		return;
	}
	gSessions++;
	LOG("playing at %u Hz: volume %u.%u dB below full (registers 0x%02x 0x%02x 0x%02x 0x%02x; model %s, limit %u steps, "
	    "coil %d mC), gain 0x%02x, tone %llu frames at -%u dB, writer %s", gRate, gSteps[0] / 2, (gSteps[0] & 1) * 5, gVolSeen[0],
	    gVolSeen[1], gVolSeen[2], gVolSeen[3], wanted_steps(0) == SPK_STEPS_LOCKED ? "not in use" : "in use",
	    gModelValid ? sf_spk_model_limit_steps(&gModel) : 0, gModelValid ? sf_spk_model_coil_mc(&gModel, 0) : 0,
	    gGainSeen[0], gToneLeft + gFramesTone, gToneDb, gDevOpen ? "open" : "none");
}

static void
stop_playback(const char *why)
{
	uint32_t st[3];
	uint64_t periods = gDone - gDoneAtFirst, ticks = gLastCounter - gFirstCounter;
	uint32_t mhz = gFirstCounter && periods && ticks ?
	    (uint32_t)(periods * PERIOD_FRAMES * ADMAC_COUNTER_HZ * 1000 / ticks) : 0;

	gSense.status(st);
	amps_off();
	stop_streams();
	gState = kIdle;
	gSteps[0] = gSteps[1] = SPK_STEPS_LOCKED;
	gIdleStamp = uptime_ns();
	gLastRateMilliHz = mhz;
	LOG("stopped (%s): %llu periods played (%llu written frames, %llu tone), %llu sense periods, measured %u.%03u Hz, "
	    "late %u, ring errors %u, sense receiver 0x%x/0x%08x sync 0x%x", why, gDone, gFramesData, gFramesTone,
	    gSenseDone, mhz / 1000, mhz % 1000, gLate, gRingErrors, st[0], st[1], st[2]);
	LOG("sense peaks of the session (current/voltage, of 32768): LF %d/%d RF %d/%d LR %d/%d RR %d/%d; die %d %d %d %d mC",
	    gSensePeakSession[0], gSensePeakSession[1], gSensePeakSession[2], gSensePeakSession[3], gSensePeakSession[4],
	    gSensePeakSession[5], gSensePeakSession[6], gSensePeakSession[7], gDieMilli[0], gDieMilli[1], gDieMilli[2],
	    gDieMilli[3]);
	if (gModelValid)
		LOG("model: coil %d %d %d %d mC, magnet %d %d %d %d mC, limit %u half-dB steps, sense %s", sf_spk_model_coil_mc(&gModel, 0),
		    sf_spk_model_coil_mc(&gModel, 1), sf_spk_model_coil_mc(&gModel, 2), sf_spk_model_coil_mc(&gModel, 3),
		    sf_spk_model_magnet_mc(&gModel, 0), sf_spk_model_magnet_mc(&gModel, 1), sf_spk_model_magnet_mc(&gModel, 2),
		    sf_spk_model_magnet_mc(&gModel, 3), sf_spk_model_limit_steps(&gModel),
		    gSenseTrusted[0] && gSenseTrusted[1] ? "followed the signal on both sides" : gSenseTrusted[0] ?
		    "followed the signal on the left only" : gSenseTrusted[1] ? "followed the signal on the right only" :
		    "not confirmed (too quiet to tell, or absent)");
}

static void
service_playback(void)
{
	char why[96];
	uint64_t counter = 0;
	unsigned n = gMca.drainReports(&counter);

	if (n > 0) {
		gDone += n;
		gTotalDone += n;
		gLastCounter = counter;
		if (gFirstCounter == 0) {
			gFirstCounter = counter;
			gDoneAtFirst = gDone;
		}
	}
	/* captured periods come back in the order they were queued */
	n = gSense.drainReports(NULL);
	for (unsigned i = 0; i < n && gState == kPlaying; i++) {
		take_sense((uint32_t)(gSenseDone % RING_PERIODS));
		gSenseDone++;
		gTotalSense++;
	}
	if (gState != kPlaying)
		return;
	/* the sense stream runs with the playback: one that falls behind is one the model cannot use */
	if (gDone > gSenseDone + SPK_SENSE_LAG) {
		trip("the sense capture stopped");
		return;
	}
	while (!gSense.descRingFull()) {
		gSense.submit(gSenseIova + (uint64_t)gSenseNext * PERIOD_BYTES, PERIOD_BYTES);
		gSenseNext = (gSenseNext + 1) % RING_PERIODS;
	}

	if (gMca.clearError() || gSense.clearError()) {
		gRingErrors++;
		trip("DMA ring error");
		return;
	}
	if (gDone > 0 && gMca.descRingEmpty()) {
		gLate++;
		trip("the playback ring ran dry");
		return;
	}
	if (gDart->check("speakers") != 0) {
		trip("DART fault");
		return;
	}
	while (!gMca.descRingFull()) {
		fill_period(gNext);
		gMca.submit(gPlayIova + (uint64_t)gNext * PERIOD_BYTES, PERIOD_BYTES);
		gNext = (gNext + 1) % RING_PERIODS;
		gSubmitted++;
	}
	if (++gTicks % CHECK_TICKS == 0 && amps_check(why, sizeof(why)) != 0) {
		trip(why);
		return;
	}
	if (gToneLeft == 0 && !gDevOpen && gFifoCount == 0 && gSilentRun >= TRAILING_SILENCE)
		stop_playback("end of data");
}

static void
sf_speaker_thread(__unused void *arg, __unused wait_result_t wr)
{
	unsigned tick = 0;

	for (;;) {
		bool playing;

		IOLockLock(gLock);
		if (gState == kPlaying)
			service_playback();
		else if (gState == kIdle && (gToneLeft > 0 || gFifoCount > 0))
			start_playback();
		else
			model_idle();
		playing = gState == kPlaying;
		IOLockUnlock(gLock);

		/* devfs is not there when the driver starts */
		if (gNode == NULL && gMajor >= 0 && tick % (playing ? 200 : 10) == 0) {
			gNode = devfs_make_node(makedev(gMajor, 0), DEVFS_CHAR, 0, 0, 0666, "sfspeaker");
			if (gNode != NULL)
				LOG("/dev/sfspeaker published");
		}
		tick++;
		IOSleep(playing ? 5 : 100);
	}
}

/* ---- /dev/sfspeaker ------------------------------------------------------------------- */

static int
spk_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	int err = 0;

	IOLockLock(gLock);
	if (gState != kIdle && gState != kPlaying)
		err = ENXIO;
	else if (gDevOpen)
		err = EBUSY;
	else
		gDevOpen = true;
	IOLockUnlock(gLock);
	return err;
}

static int
spk_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	IOLockLock(gLock);
	gDevOpen = false;
	IOLockUnlock(gLock);
	return 0;
}

static int
spk_write(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	int16_t chunk[2 * 256];
	int err = 0;

	while (err == 0 && uio_resid(uio) > 0) {
		user_ssize_t left = uio_resid(uio);
		unsigned frames = left >= 4 ? (unsigned)(left / 4 > 256 ? 256 : left / 4) : 0;

		if (frames == 0) {
			err = uiomove((char *)chunk, (int)left, uio);   /* a trailing partial frame is dropped */
			break;
		}
		err = uiomove((char *)chunk, (int)(frames * 4), uio);
		if (err != 0)
			break;
		IOLockLock(gLock);
		for (unsigned i = 0; i < frames && err == 0; i++) {
			while (gFifoCount == FIFO_FRAMES && (gState == kIdle || gState == kPlaying)) {
				if (IOLockSleep(gLock, &gFifoEvent, THREAD_ABORTSAFE) != THREAD_AWAKENED) {
					err = EINTR;
					break;
				}
			}
			if (err == 0 && gState != kIdle && gState != kPlaying)
				err = ENXIO;
			if (err != 0)
				break;
			uint32_t at = (gFifoHead + gFifoCount) % FIFO_FRAMES;
			gFifo[2 * at] = (int32_t)chunk[2 * i] * 65536;
			gFifo[2 * at + 1] = (int32_t)chunk[2 * i + 1] * 65536;
			gFifoCount++;
		}
		IOLockUnlock(gLock);
	}
	return err;
}

static struct cdevsw spk_cdevsw = {
	spk_open, spk_close, eno_rdwrt, spk_write,
	eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
	eno_strat, eno_getc, eno_putc, 0
};

/* ---- sysctl --------------------------------------------------------------------------- */

enum {
	kCtlTone, kCtlToneDb, kCtlRate, kCtlPlaying, kCtlUnlocked, kCtlOwner, kCtlLimitSteps, kCtlVolumeSteps,
	kCtlSenseTrusted, kCtlPeriods, kCtlSensePeriods, kCtlTrips, kCtlSessions, kCtlRateMilliHz,
	kCtlTestShutdown, kCtlHoldProbe, kCtlFrames, kCtlFifoFrames, kCtlLockMs, kCtlPrerollMs, kCtlCapSteps,
};

static int
spk_sysctl_int(struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	int value = 0, error;

	IOLockLock(gLock);
	switch (arg2) {
	case kCtlTone:          value = (int)(gToneLeft / gRate); break;
	case kCtlToneDb:        value = (int)gToneDb; break;
	case kCtlRate:          value = (int)gRate; break;
	case kCtlPlaying:       value = gState == kPlaying; break;
	case kCtlUnlocked:      value = wanted_steps(0) != SPK_STEPS_LOCKED; break;
	case kCtlOwner:         value = 0; break;
	case kCtlLimitSteps:    value = gModelValid ? (int)sf_spk_model_limit_steps(&gModel) : -1; break;
	case kCtlVolumeSteps:   value = (int)(gSteps[0] > gSteps[1] ? gSteps[1] : gSteps[0]); break;
	case kCtlSenseTrusted:  value = gSenseTrusted[0] + 2 * gSenseTrusted[1]; break;
	case kCtlPeriods:       value = (int)gTotalDone; break;
	case kCtlSensePeriods:  value = (int)gTotalSense; break;
	case kCtlTrips:         value = (int)gTrips; break;
	case kCtlSessions:      value = (int)gSessions; break;
	case kCtlRateMilliHz:   value = (int)gLastRateMilliHz; break;
	case kCtlHoldProbe:     value = (int)gHoldProbe; break;
	case kCtlFrames:        value = (int)(gTotalDone * PERIOD_FRAMES); break;
	case kCtlFifoFrames:    value = (int)gFifoCount; break;
	/* the amplifiers need no lock time: both are 0, so a client of either output can read the same names */
	case kCtlLockMs:        value = 0; break;
	case kCtlPrerollMs:     value = 0; break;
	case kCtlCapSteps:      value = (int)gCapSteps; break;
	}
	IOLockUnlock(gLock);
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == USER_ADDR_NULL)
		return error;

	IOLockLock(gLock);
	switch (arg2) {
	case kCtlTone:
		if (gState != kIdle && gState != kPlaying)
			error = ENXIO;
		else if (value < 0 || value > 60)
			error = EINVAL;
		else
			gToneLeft = (uint64_t)value * gRate;
		break;
	case kCtlToneDb:
		if (value < MIN_TONE_DB || value > 96)
			error = EINVAL;
		else
			gToneDb = (uint32_t)value;
		break;
	case kCtlHoldProbe:
		/* can only make the speakers quieter */
		gHoldProbe = value != 0;
		break;
	case kCtlTestShutdown:
		/* takes the same path as a fault; it can only turn the speakers off */
		if (gState != kPlaying)
			error = ENXIO;
		else
			trip("shutdown requested through kern.sf_speaker.test_shutdown");
		break;
	case kCtlRate:
		if (value != 44100 && value != 48000)
			error = EINVAL;
		else if (gState == kPlaying || gFifoCount > 0 || gToneLeft > 0)
			error = EBUSY;
		else
			gRate = (uint32_t)value;
		break;
	default:
		error = EPERM;
	}
	IOLockUnlock(gLock);
	return error;
}

static int
spk_sysctl_string(__unused struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	char text[640];

	text[0] = 0;
	IOLockLock(gLock);
	switch (arg2) {
	case 0:
		snprintf(text, sizeof(text), "%s%s%s rate=%u amps=0x%02x,0x%02x,0x%02x,0x%02x die_mc=%d,%d,%d,%d trips=%u%s%s",
		    kStateNames[gState], gFailure[0] ? ": " : "", gFailure, gRate, gAmpRev[0], gAmpRev[1], gAmpRev[2],
		    gAmpRev[3], gDieMilli[0], gDieMilli[1], gDieMilli[2], gDieMilli[3], gTrips, gTripReason[0] ? " last: " : "",
		    gTripReason);
		break;
	case 1:
		snprintf(text, sizeof(text), "peak LF=%d/%d RF=%d/%d LR=%d/%d RR=%d/%d mean LF=%d/%d RF=%d/%d LR=%d/%d RR=%d/%d",
		    gSensePeak[0], gSensePeak[1], gSensePeak[2], gSensePeak[3], gSensePeak[4], gSensePeak[5], gSensePeak[6],
		    gSensePeak[7], gSenseMean[0], gSenseMean[1], gSenseMean[2], gSenseMean[3], gSenseMean[4], gSenseMean[5],
		    gSenseMean[6], gSenseMean[7]);
		break;
	case 2:
		snprintf(text, sizeof(text), "%s steps=%u,%u registers=0x%02x,0x%02x,0x%02x,0x%02x gain=0x%02x,0x%02x,"
		    "0x%02x,0x%02x power=0x%02x,0x%02x,0x%02x,0x%02x latched=0x%02x/0x%02x,0x%02x/0x%02x,0x%02x/0x%02x,0x%02x/0x%02x",
		    wanted_steps(0) == SPK_STEPS_LOCKED ? "locked" : "model", gSteps[0], gSteps[1], gVolSeen[0], gVolSeen[1],
		    gVolSeen[2], gVolSeen[3],
		    gGainSeen[0], gGainSeen[1], gGainSeen[2], gGainSeen[3], gPwr[0], gPwr[1], gPwr[2], gPwr[3], gLat0[0],
		    gLat1[0], gLat0[1], gLat1[1], gLat0[2], gLat1[2], gLat0[3], gLat1[3]);
		break;
	case 3:
		if (gModelValid)
			snprintf(text, sizeof(text), "%s%s coil_mc=%d,%d,%d,%d magnet_mc=%d,%d,%d,%d power_mw=%d,%d,%d,%d limit_steps=%u "
			    "volume_steps=%u,%u sense=%s fault=%d", !kModelLiftsClamp || !gModelOn ? "watching only" : "in charge",
			    gModel.fault ? " FAULT" : "", sf_spk_model_coil_mc(&gModel, 0), sf_spk_model_coil_mc(&gModel, 1),
			    sf_spk_model_coil_mc(&gModel, 2), sf_spk_model_coil_mc(&gModel, 3), sf_spk_model_magnet_mc(&gModel, 0),
			    sf_spk_model_magnet_mc(&gModel, 1), sf_spk_model_magnet_mc(&gModel, 2), sf_spk_model_magnet_mc(&gModel, 3),
			    gModel.spk[0].power_mw, gModel.spk[1].power_mw, gModel.spk[2].power_mw, gModel.spk[3].power_mw,
			    sf_spk_model_limit_steps(&gModel), gSteps[0], gSteps[1], gState != kPlaying ? "idle" :
			    gSenseTrusted[0] && gSenseTrusted[1] ? "trusted" : gSenseTrusted[0] ? "left trusted" : gSenseTrusted[1] ?
			    "right trusted" : "unconfirmed", gModel.fault);
		else
			snprintf(text, sizeof(text), "no model: volumes at -100 dB");
		break;
	}
	IOLockUnlock(gLock);
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_NODE(_kern, OID_AUTO, sf_speaker, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "built-in speakers");

#define SPK_INT(name, id, flags, text) \
	SYSCTL_PROC(_kern_sf_speaker, OID_AUTO, name, CTLTYPE_INT | (flags) | CTLFLAG_LOCKED, 0, id, spk_sysctl_int, "I", text)
#define SPK_STR(name, id, text) \
	SYSCTL_PROC(_kern_sf_speaker, OID_AUTO, name, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, id, spk_sysctl_string, "A", text)

SPK_STR(state, 0, "state, amplifier revisions and die temperatures, shutdowns");
SPK_STR(sense, 1, "current/voltage sense of the last period, of 32768: largest magnitude and mean");
SPK_STR(volume, 2, "the volume in force and the amplifier registers as last read back");
SPK_STR(model, 3, "the temperature model: coil and magnet per speaker, mean power, limit, sense health");
SPK_INT(tone, kCtlTone, CTLFLAG_RW, "seconds of test tone left; write to play (at the volume in force)");
SPK_INT(tone_db, kCtlToneDb, CTLFLAG_RW, "test tone level, dB below digital full scale (at least 20)");
SPK_INT(rate, kCtlRate, CTLFLAG_RW, "sample rate");
SPK_INT(playing, kCtlPlaying, CTLFLAG_RD, "1 while the speakers play");
SPK_INT(unlocked, kCtlUnlocked, CTLFLAG_RD, "1 while the model sets the volume, 0 while it is held at -100 dB");
SPK_INT(owner, kCtlOwner, CTLFLAG_RD, "always 0: no process can hold the volume open");
SPK_INT(limit_steps, kCtlLimitSteps, CTLFLAG_RD, "the model's limit in half-dB steps below 0 dB (-1: no model)");
SPK_INT(volume_steps, kCtlVolumeSteps, CTLFLAG_RD, "the amplifiers' volume in half-dB steps below 0 dB (200: -100 dB)");
SPK_INT(sense_trusted, kCtlSenseTrusted, CTLFLAG_RD, "this session's sense data has followed the signal: 1 left, 2 right, 3 both");
SPK_INT(periods, kCtlPeriods, CTLFLAG_RD, "DMA periods played since boot");
SPK_INT(sense_periods, kCtlSensePeriods, CTLFLAG_RD, "sense periods captured since boot");
SPK_INT(trips, kCtlTrips, CTLFLAG_RD, "times the amplifiers were shut down for a fault");
SPK_INT(sessions, kCtlSessions, CTLFLAG_RD, "playback sessions started");
SPK_INT(rate_mhz, kCtlRateMilliHz, CTLFLAG_RD, "sample rate of the last session from the DMA timestamps, in mHz");
SPK_INT(hold_probe, kCtlHoldProbe, CTLFLAG_RW, "1: keep the volume 20 dB down even once the sense data is confirmed (for quiet tests)");
SPK_INT(test_shutdown, kCtlTestShutdown, CTLFLAG_RW, "write while playing: shut the amplifiers down as a fault would");
SPK_INT(frames, kCtlFrames, CTLFLAG_RD, "frames played since boot (wraps)");
SPK_INT(fifo_frames, kCtlFifoFrames, CTLFLAG_RD, "frames a writer has queued that are not yet in the DMA ring");
SPK_INT(lock_ms, kCtlLockMs, CTLFLAG_RD, "always 0 (kern.sf_sound.lock_ms has the codec's lock time)");
SPK_INT(preroll_ms, kCtlPrerollMs, CTLFLAG_RD, "always 0: playback starts with the first frames");
SPK_INT(cap_steps, kCtlCapSteps, CTLFLAG_RD, "the volume never goes above this many half-dB steps below 0 dB (boot argument sfspeaker_cap_steps)");

static struct sysctl_oid *const gOids[] = {
	&sysctl__kern_sf_speaker,
	&sysctl__kern_sf_speaker_state, &sysctl__kern_sf_speaker_sense, &sysctl__kern_sf_speaker_volume,
	&sysctl__kern_sf_speaker_tone, &sysctl__kern_sf_speaker_tone_db, &sysctl__kern_sf_speaker_rate,
	&sysctl__kern_sf_speaker_playing, &sysctl__kern_sf_speaker_unlocked, &sysctl__kern_sf_speaker_owner,
	&sysctl__kern_sf_speaker_limit_steps, &sysctl__kern_sf_speaker_volume_steps, &sysctl__kern_sf_speaker_sense_trusted,
	&sysctl__kern_sf_speaker_model, &sysctl__kern_sf_speaker_periods, &sysctl__kern_sf_speaker_sense_periods,
	&sysctl__kern_sf_speaker_trips, &sysctl__kern_sf_speaker_sessions, &sysctl__kern_sf_speaker_rate_mhz,
	&sysctl__kern_sf_speaker_test_shutdown, &sysctl__kern_sf_speaker_hold_probe,
	&sysctl__kern_sf_speaker_frames, &sysctl__kern_sf_speaker_fifo_frames, &sysctl__kern_sf_speaker_lock_ms,
	&sysctl__kern_sf_speaker_preroll_ms, &sysctl__kern_sf_speaker_cap_steps,
};

/* ---- bring-up ------------------------------------------------------------------------- */

static int
fail(const char *what, int err)
{
	snprintf(gFailure, sizeof(gFailure), "%s (error %d)", what, err);
	LOG("unavailable: %s", gFailure);
	gState = kFailed;
	return err;
}

int
sf_speakers_bring_up(SFGpio *gpio, SFDartFixed *dart, uint8_t *playRing, uint64_t playIova, uint8_t *senseRing,
    uint64_t senseIova)
{
	thread_t thread;
	int err;

	gLock = IOLockAlloc();
	if (gLock == NULL)
		return ENOMEM;
	gGpio = gpio;
	gDart = dart;
	gPlayRing = playRing;
	gPlayIova = playIova;
	gSenseRing = senseRing;
	gSenseIova = senseIova;
	for (unsigned i = 0; i < sizeof(gOids) / sizeof(gOids[0]); i++)
		sysctl_register_oid(gOids[i]);

	LOG("shutdown line (pin %u) 0x%08x before; i2c1 pins 0x%08x 0x%08x, i2c3 pins 0x%08x 0x%08x", PIN_SPEAKER_SDZ,
	    gpio->raw(PIN_SPEAKER_SDZ), gpio->raw(201), gpio->raw(199), gpio->raw(73), gpio->raw(72));
	gpio->output(PIN_SPEAKER_SDZ, 0);
	/* i2c1_pins and i2c3_pins of the Linux device tree */
	gpio->function(201, 1);
	gpio->function(199, 1);
	gpio->function(73, 1);
	gpio->function(72, 1);
	if (!gBus[0].attach("/arm-io/i2c1", 100000) || !gBus[1].attach("/arm-io/i2c3", 100000))
		return fail("I2C controllers i2c1/i2c3", ENXIO);
	for (unsigned i = 0; i < SFSPK_SPEAKERS; i++) {
		gAmp[i].bus = &gBus[kAmps[i].bus];
		gAmp[i].addr = kAmps[i].addr;
		gAmp[i].name = kAmps[i].name;
	}

	/* identify the four amplifiers: out of hardware shutdown, reset, read, back into it */
	IOLockLock(gLock);
	err = amps_on();
	for (unsigned i = 0; i < SFSPK_SPEAKERS && err == 0; i++) {
		uint8_t pwr = 0, gain = 0, vol = 0;
		err = gAmp[i].rd(0x02, &pwr);
		if (err == 0)
			err = gAmp[i].rd(0x03, &gain);
		if (err == 0)
			err = gAmp[i].rd(0x05, &vol);
		gAmp[i].faults(&gLat0[i], &gLat1[i]);
		gAmp[i].dieTemp(&gDieMilli[i]);
		LOG("amplifier %s at i2c%u 0x%02x: revision 0x%02x, power 0x%02x, gain 0x%02x, volume 0x%02x, latched 0x%02x/0x%02x",
		    kAmps[i].name, kAmps[i].bus ? 3 : 1, kAmps[i].addr, gAmp[i].rev, pwr, gain, vol, gLat0[i], gLat1[i]);
		if (err == 0 && ((pwr & 0x03) != kSFTasShutdown || (gain & 0x1f) != SPK_AMP_GAIN ||
		    vol != SPK_VOLUME_REG(SPK_STEPS_LOCKED)))
			err = EIO;
	}
	amps_off();
	IOLockUnlock(gLock);
	if (err != 0)
		return fail("amplifier identification", err);

	err = gMca.attach(SPK_FE_CLUSTER, SPK_PORTS, SPK_SLOTS, SFSPK_SPEAKERS, 1);
	if (err == 0)
		err = gSense.attach(SPK_SENSE_CLUSTER, SPK_PORTS, true, 0, SPK_SENSE_SLOTS, 16, SFSPK_SENSE_CHANNELS, 0);
	if (err != 0)
		return fail("speaker transport", err);
	gFifo = (int32_t *)IOMalloc(FIFO_FRAMES * 2 * sizeof(int32_t));
	if (gFifo == NULL)
		return fail("FIFOs", ENOMEM);
	gMajor = cdevsw_add(-1, &spk_cdevsw);
	/* 10^(-1/40) per half-dB step, for the signal the voltage sense should show */
	gAttenQ16[0] = 65536;
	for (unsigned i = 1; i < 64; i++)
		gAttenQ16[i] = (uint32_t)(((uint64_t)gAttenQ16[i - 1] * 61870) >> 16);
	PE_parse_boot_argn("sfspeaker_model", &gModelOn, sizeof(gModelOn));
	PE_parse_boot_argn("sfspeaker_cap_steps", &gCapSteps, sizeof(gCapSteps));
	if (gCapSteps > SPK_STEPS_LOCKED)
		gCapSteps = SPK_STEPS_LOCKED;
	gModelValid = sf_spk_model_init(&gModel, gRate, SPK_AMP_GAIN) == 0;
	gIdleStamp = uptime_ns();
	gState = kIdle;
	if (kernel_thread_start(sf_speaker_thread, NULL, &thread) != KERN_SUCCESS)
		return fail("thread", ENOMEM);
	thread_deallocate(thread);
	LOG("ready: amplifiers in hardware shutdown, gain fixed at 0x%02x; volume %s (coil %d mC, magnet %d mC at start)",
	    SPK_AMP_GAIN, wanted_steps(0) == SPK_STEPS_LOCKED ? "held at -100 dB, the model only watches" :
	    "set by the temperature model, 20 dB down until the sense data is confirmed",
	    gModelValid ? sf_spk_model_coil_mc(&gModel, 0) : 0, gModelValid ? sf_spk_model_magnet_mc(&gModel, 0) : 0);
	return 0;
}

void
sf_speakers_quiesce(void)
{
	if (gLock == NULL)
		return;
	IOLockLock(gLock);
	if (gState == kPlaying)
		stop_playback("shutdown");
	if (gState == kIdle)
		gState = kDisabled;
	if (gGpio != NULL && gAmp[0].bus != NULL)
		amps_off();
	IOLockWakeup(gLock, &gFifoEvent, false);
	IOLockUnlock(gLock);
}
