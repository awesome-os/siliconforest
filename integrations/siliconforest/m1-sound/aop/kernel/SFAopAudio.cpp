// SPDX-License-Identifier: GPL-2.0-only
/*
 * SFAopAudio: capture from the three built-in PDM microphones of the 13" M1 MacBook Pro
 * (J293, T8103) for SiliconForest.
 *
 * The microphones are not on the audio codec. The always-on coprocessor (AOP, RTKit
 * firmware) clocks and decimates them and feeds a second audio DMA controller. This kext
 * does what the Omacom Linux tree does with them:
 *   drivers/soc/apple/aop.rs       start the AOP and talk to it       -> sf_aop.cpp
 *   sound/soc/apple/aop_audio.rs   "Apple AOP audio driver", Copyright (C) The Asahi Linux
 *                                  Contributors (GPL-2.0-only OR MIT): attach the audio
 *                                  devices, configure the PDM device and its decimator,
 *                                  switch the high-power input on and off -> this file
 *   drivers/dma/apple-admac.c      the DMA channel                    -> sf_admac.cpp
 * The order of every step is that of those drivers; where Linux leaves a step to ALSA
 * (hw_params, trigger) the order is the one ALSA calls them in.
 *
 * Apple device tree nodes used (J293):
 *   /arm-io/aop               reg 0: ASC (CPU control, mailbox at +0x8000); reg 2: the
 *                             firmware's memory with its boot arguments. No clock-gates.
 *   /arm-io/aop/iop-aop-nub/aop-audio/...   the values Linux has as constants; only compared
 *   /arm-io/dart-aop          reg 0: DART; reg 1: its address filter. Stream 0 (mapper-aop):
 *                             the AOP; stream 7 (mapper-aop-admac): the DMA controller
 *   /arm-io/admac-aop-audio   audio DMA controller; channel 1 (the first receive channel),
 *                             interrupt output 2 (not used: the rings are polled). Its
 *                             power is switched by the AOP: no register of it is touched
 *                             before the AOP has acknowledged power state 'pw1 '.
 *
 * The stream, as Linux declares it (aop_pcm_open): 48000 Hz, 3 channels, 32-bit float
 * little-endian, interleaved; DMA bus width 32 bit, 4 words per frame signalled.
 *
 * Interfaces:
 *   /dev/sfmic            read-only, one reader. Opening it starts the capture (AOP power
 *                         states 'pw1 ' then 'pwrd', then the DMA channel), closing stops
 *                         it ('pw1 ', then 'idle'). Reads return what the DMA controller
 *                         wrote: frames of three 32-bit little-endian floats, 48000 per
 *                         second, 12 bytes per frame. A reader that falls more than a
 *                         quarter of a second behind loses the oldest data (overruns).
 *   kern.sf_aop.*         state and counters; capture=<seconds> runs the capture without
 *                         a reader (statistics only); stats0..2 are the statistics of
 *                         the last window (65536 frames, 1.37 s) per microphone.
 *
 * Boot arguments:
 *   sfaop=0|1|2|3         0: nothing is touched. 1: start the AOP and its endpoints, list
 *                         the services. 2: also attach and configure the audio devices.
 *                         3 (default): also set up the DMA window; capture on demand.
 *   sfaop_capture=<s>     capture for s seconds once brought up (no reader needed)
 *   sfaop_delay=<ms>      wait before touching hardware (default 3000)
 *   sfaop_verbose=1       log every mailbox message
 *   sfaop_diag=1          around the first capture, log (read only) the PMGR states of the
 *                         AOP's audio blocks, the pin words of aop-gpio and nub-gpio and
 *                         which aop-gpio pins move (PDM clocks: pins 30/31; data: 36/37)
 *
 * Locking: one mutex (gLock) covers everything readers of the device node and of the
 * sysctls share with the driver's thread: state, requests, the capture position,
 * statistics. The thread alone talks to the AOP and touches the DMA controller (SFAop is
 * not shared), so no caller ever waits on the firmware with the mutex held.
 *
 * Does nothing on a chip other than T8103.
 *
 * State on hardware (boots sd41..sd48, November 11): everything up to and including the
 * DMA stream is proven (the AOP boots, the audio service takes every call Linux makes,
 * 48000.02 frames per second arrive without ring errors or DART faults). The samples
 * themselves carry no microphone signal: after a start-up transient that is bit-identical
 * in every session they decay to the smallest normal floats (1e-38) and zero. With
 * sfaop_diag=1 the log shows why: the two PDM clock pins (aop-gpio 30, 31) run, the pins
 * that go with them (36, 37) and every other pin of the block stay at one level. What
 * keeps the microphones from driving their data lines here is not known; nothing in the
 * Linux drivers this is ported from addresses it.
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOService.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <miscfs/devfs/devfs.h>
#include <pexpert/pexpert.h>
#include <sys/conf.h>
#include <sys/errno.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/uio.h>

#include "sf_admac.h"
#include "sf_aop.h"
#include "sf_aop_audio_tables.h"
#include "sf_dart_fixed.h"
#include "sf_soc.h"

#define LOG(fmt, ...) IOLog("SFAopAudio: " fmt "\n", ##__VA_ARGS__)

#define T8103_CHIP_ID        0x8103

#define AOP_STREAM           0               /* /arm-io/dart-aop/mapper-aop */
#define ADMAC_STREAM         7               /* /arm-io/dart-aop/mapper-aop-admac */
#define AOP_WINDOW_IOVA      0xf0000000ULL   /* everything the firmware is handed */
#define AOP_WINDOW_BYTES     (16u << 20)
#define RING_IOVA            0xf8000000ULL   /* the capture ring */
#define ADMAC_CHANNEL        1               /* dmas = <&aop_admac 1> */
#define ADMAC_IRQ_INDEX      2               /* irq-destination-index */
#define ADMAC_COUNTER_HZ     24000000ULL

#define RATE                 48000
#define CHANNELS             3
#define FRAME_BYTES          (CHANNELS * 4)
#define PERIOD_FRAMES        1024
#define PERIOD_BYTES         (PERIOD_FRAMES * FRAME_BYTES)   /* 12288; Linux allows up to 0x4000 */
#define RING_PERIODS         16
#define RING_BYTES           (RING_PERIODS * PERIOD_BYTES)   /* 196608: twelve 16 KiB pages */
#define MAX_QUEUED           4               /* descriptors the controller's ring holds */
#define READ_BEHIND_MAX      ((RING_PERIODS - MAX_QUEUED - 1) * PERIOD_BYTES)
#define WINDOW_PERIODS       64              /* statistics window: 65536 frames */
#define WINDOW_FRAMES        (WINDOW_PERIODS * PERIOD_FRAMES)

/* ---- aop_audio.rs --------------------------------------------------------------------- */

#define EPIC_SUBTYPE_WRAPPED_CALL    0x20
#define CALLTYPE_AUDIO_ATTACH_DEVICE 0xc3000002u
#define CALLTYPE_AUDIO_SET_PROP      0xc3000005u
#define FOURCC(a, b, c, d)   (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define AUDIO_DEV_PDM0       FOURCC('p', 'd', 'm', '0')
#define AUDIO_DEV_LPAI       FOURCC('l', 'p', 'a', 'i')
#define AUDIO_DEV_HPAI       FOURCC('h', 'p', 'a', 'i')
#define POWER_STATE_OFF      FOURCC('i', 'd', 'l', 'e')
#define POWER_STATE_IDLE     FOURCC('p', 'w', '1', ' ')
#define POWER_STATE_ON       FOURCC('p', 'w', 'r', 'd')

#pragma pack(push, 1)
struct AudioAttachDevice {
	uint32_t zero0;
	uint32_t unk0;
	uint32_t calltype;
	uint64_t zero1;
	uint64_t zero2;
	uint32_t pad0;
	uint64_t len;
	uint32_t dev_id;
	uint32_t pad1;
};
struct AudioSetDevicePropHeader {
	uint32_t zero0;
	uint32_t unk0;
	uint32_t calltype;
	uint8_t  zero1[8];
	uint8_t  zero2[8];
	uint32_t pad0;
	uint32_t len;
	uint32_t pad1;
	uint32_t dev_id;
	uint32_t modifier;
	uint32_t len2;
	/* data follows */
};
struct LpaiChannelConfig {
	uint32_t channels_enabled;
	uint32_t channels_supported;
	uint32_t voice_trigger_channel;
	uint32_t unk4;
};
struct DecimatorConfig {
	uint32_t latency;
	uint8_t  ratio1;
	uint8_t  ratio2;
	uint8_t  ratio3;
	uint8_t  pad0;
	uint32_t filter_lengths;
	uint32_t coeff_bulk;
	uint8_t  coeffs[SF_DECIM_NUM_COEFFS * 4];
};
struct SFMConfig {
	uint32_t bytes_per_sample;
	uint32_t clock_source;
	uint32_t pdm_frequency;
	uint32_t pdmc_frequency;
	uint32_t slow_clock_speed;
	uint32_t fast_clock_speed;
	uint32_t channel_polarity_select;
	uint8_t  unk1;
	uint32_t channel_phase_select;
	uint8_t  unk2;
	struct DecimatorConfig decim;
	uint32_t mic_turn_on_time_valid;
	uint32_t mic_turn_on_time_ms;
	uint8_t  zero0[8];
	uint8_t  zero1[8];
	uint32_t mic_settle_time_valid;
	uint32_t mic_settle_time_ms;
	uint8_t  zero2[69];
	uint8_t  pad_extra;
};
struct PowerSetting {
	uint32_t dev_id;
	uint32_t cookie;
	uint32_t unk0;
	uint8_t  zero0[8];
	uint32_t target_pstate;
	uint32_t unk1;
	uint8_t  zero1[20];
};
#pragma pack(pop)

/* the sizes the Rust driver asserts */
static_assert(sizeof(struct AudioAttachDevice) == 48, "AudioAttachDevice");
static_assert(sizeof(struct AudioSetDevicePropHeader) == 52, "AudioSetDeviceProp");
static_assert(sizeof(struct LpaiChannelConfig) == 16, "LpaiChannelConfig");
static_assert(sizeof(struct DecimatorConfig) == 16 + SF_DECIM_NUM_COEFFS * 4, "DecimatorConfig");
static_assert(sizeof(struct SFMConfig) == 34 + sizeof(struct DecimatorConfig) + 32 + 69 + 1, "SFMConfig");
static_assert(sizeof(struct PowerSetting) == 48, "PowerSetting");

/* ---- state ---------------------------------------------------------------------------- */

enum { kStateOff, kStateFailed, kStateAopOnly, kStateConfigured, kStateIdle, kStateCapturing };
static const char *const kStateNames[] = { "off", "failed", "aop-only", "configured", "idle", "capturing" };

struct ChannelStats {
	int64_t  sum;           /* of the samples, Q23 */
	uint64_t meanSquares;   /* sum over the periods of each period's mean square, Q46 */
	uint64_t periodSquares; /* sum of squares in the period being filled */
	int32_t  min, max;      /* Q23 */
	uint32_t zeros;         /* samples that are exactly +0.0 or -0.0 */
	uint32_t repeats;       /* samples with the same bits as the one before in this channel */
	uint32_t nonfinite;     /* infinities and NaNs */
	uint32_t clipped;       /* beyond +-8.0, which the statistics clamp to */
	uint32_t last;          /* bits of the previous sample */
};

struct WindowResult {
	int32_t  mean, rms, peak, min, max;   /* Q23: 8388608 is 1.0 */
	uint32_t zeros, repeats, nonfinite, clipped;
};

static IOLock      *gLock;
static SFAop        gAop;                    /* the thread's alone */
static SFDartFixed  gRingDart;
static SFMmio       gAdmacRegs;
static SFAdmacChan  gChan;
static const SFAopService *gAudio;
static bool         gAopUp;                  /* the coprocessor runs: its mailbox is served from now on */

static uint32_t gLevel = 3;
static uint32_t gDiag;                       /* sfaop_diag=1: log PMGR states and pin levels around the first capture */
static uint32_t gDelayMs = 3000;
static int      gState = kStateOff;
static char     gFailure[160];
static bool     gShutdown;

/* requests, under gLock */
static bool     gReaderOpen;
static uint32_t gTimedPeriods;               /* periods of capture still wanted without a reader */
static int      gSessionError;               /* why the last start failed */

/* capture, under gLock */
static uint64_t gSubmitted, gDone;           /* periods of this session */
static uint64_t gReadPos;                    /* byte position of the reader in this session */
static uint64_t gTotalPeriods;               /* since boot */
static uint32_t gSessions, gOverruns, gRingErrors, gStalls;
static uint64_t gFirstStamp, gLastStamp, gStampPeriods;
static uint32_t gRateMilliHz;
static uint32_t gAdmacSeen[4];
static uint32_t gPowerCookie = 1;
static uint32_t gFirstFrame[CHANNELS], gLastFrame[CHANNELS];
static uint64_t gLastProgress, gLastStatus;  /* the thread's: uptime in ns of the last period and status read */
static int      gReadEvent;
static int      gStateEvent;

/* statistics, under gLock */
static ChannelStats gAcc[CHANNELS];
static uint32_t     gAccPeriods;
static uint32_t     gPairDiff[3], gAccPairDiff[3];   /* frames where 0/1, 0/2, 1/2 differ, last window */
static WindowResult gWin[CHANNELS];
static uint32_t     gWindows, gSessionWindows;
static int32_t      gPeakEver[CHANNELS];

static void *gDevNode;
static int   gMajor = -1;
static uint8_t *gReadChunk;

static void
fail(const char *why)
{
	strlcpy(gFailure, why, sizeof(gFailure));
	LOG("FAILED: %s", gFailure);
	IOLockLock(gLock);
	gState = kStateFailed;
	IOLockWakeup(gLock, &gStateEvent, false);
	IOLockUnlock(gLock);
}

static void
set_state(int state)
{
	IOLockLock(gLock);
	gState = state;
	IOLockWakeup(gLock, &gStateEvent, false);
	IOLockUnlock(gLock);
}

/* ---- the audio service (aop_audio.rs) -------------------------------------------------- */

/* SndSocAopData::epic_wrapped_call(): 0, or an errno value; a firmware return code is EIO */
static int
wrapped_call(const void *msg, uint32_t len, const char *what)
{
	uint32_t ret = 0;
	int err = gAop.epicCall(gAudio, EPIC_SUBTYPE_WRAPPED_CALL, msg, len, &ret);

	if (err != 0) {
		LOG("%s: the call failed (%d)", what, err);
		return err;
	}
	if (ret != 0) {
		LOG("%s: return code %u (0x%x)", what, ret, ret);
		return EIO;
	}
	LOG("%s: ok", what);
	return 0;
}

static int
audio_attach_device(uint32_t dev, const char *name)
{
	struct AudioAttachDevice msg;
	char what[48];

	bzero(&msg, sizeof(msg));
	msg.unk0 = 0xffffffff;
	msg.calltype = CALLTYPE_AUDIO_ATTACH_DEVICE;
	msg.dev_id = dev;
	msg.len = 0x2c;
	snprintf(what, sizeof(what), "attach device '%s'", name);
	return wrapped_call(&msg, sizeof(msg), what);
}

/* AudioSetDeviceProp::new(dev, modifier, data) */
static int
audio_set_prop(uint32_t dev, uint32_t modifier, const void *data, uint32_t len, const char *what)
{
	uint32_t total = sizeof(struct AudioSetDevicePropHeader) + len;
	uint8_t *msg = (uint8_t *)IOMalloc(total);
	struct AudioSetDevicePropHeader *h = (struct AudioSetDevicePropHeader *)msg;
	int err;

	if (msg == NULL)
		return ENOMEM;
	bzero(msg, total);
	h->unk0 = 0xffffffff;
	h->calltype = CALLTYPE_AUDIO_SET_PROP;
	h->dev_id = dev;
	h->modifier = modifier;
	h->len = len + 0x30;
	h->len2 = len;
	memcpy(msg + sizeof(*h), data, len);
	err = wrapped_call(msg, total, what);
	IOFree(msg, total);
	return err;
}

/* SndSocAopData::build_decimator_config() */
static void
build_decimator_config(struct DecimatorConfig *d)
{
	bzero(d, sizeof(*d));
	d->latency = SF_DECIM_LATENCY;
	d->ratio1 = SF_DECIM_RATIO1;
	d->ratio2 = SF_DECIM_RATIO2;
	d->ratio3 = SF_DECIM_RATIO3;
	d->filter_lengths = SF_FILTER_LENGTHS;
	d->coeff_bulk = SF_DECIM_NUM_COEFFS;
	memcpy(d->coeffs, kSFDecimCoefficients, sizeof(kSFDecimCoefficients));
}

/* set_audio_power(): property 202 of 'hpai' */
static int
set_audio_power(uint32_t pstate, uint32_t unk1)
{
	struct PowerSetting p;
	char what[64];

	bzero(&p, sizeof(p));
	p.dev_id = AUDIO_DEV_HPAI;
	p.cookie = gPowerCookie++;
	p.target_pstate = pstate;
	p.unk1 = unk1;
	snprintf(what, sizeof(what), "hpai power state '%c%c%c%c' (cookie %u, unk1 %u)", (char)(pstate >> 24),
	    (char)(pstate >> 16), (char)(pstate >> 8), (char)pstate, p.cookie, unk1);
	return audio_set_prop(AUDIO_DEV_HPAI, 202, &p, sizeof(p), what);
}

/* What the Apple device tree says about the PDM device, next to what Linux sends. */
static void
log_adt_audio(void)
{
	static const char *const pdm = "/arm-io/aop/iop-aop-nub/aop-audio/audio-pdm2";
	static const char *const dc = "/arm-io/aop/iop-aop-nub/aop-audio/dc-2400000";
	uint32_t bps = 0, src = 0, pdmf = 0, pdmc = 0, slow = 0, fast = 0, pol = 0, phase = 0, on = 0, settle = 0;
	uint32_t count = 0, enabled = 0, supported = 0, trigger = 0, latency = 0, lengths = 0, ratios = 0;
	bool same = false;

	sf_adt_u32(pdm, "bytesPerSample", 0, &bps);
	sf_adt_u32(pdm, "clockSource", 0, &src);
	sf_adt_u32(pdm, "pdmFrequency", 0, &pdmf);
	sf_adt_u32(pdm, "pdmcFrequency", 0, &pdmc);
	sf_adt_u32(pdm, "slowClockSpeed", 0, &slow);
	sf_adt_u32(pdm, "fastClockSpeed", 0, &fast);
	sf_adt_u32(pdm, "channelPolaritySelect", 0, &pol);
	sf_adt_u32(pdm, "channelPhaseSelect", 0, &phase);
	sf_adt_u32(pdm, "micTurnOnTimeMs", 0, &on);
	sf_adt_u32(pdm, "micSettleTimeMs", 0, &settle);
	sf_adt_u32(pdm, "channelCount", 0, &count);
	sf_adt_u32(pdm, "channelsEnabled", 0, &enabled);
	sf_adt_u32(pdm, "channelsSupported", 0, &supported);
	sf_adt_u32(pdm, "voiceTriggerChannel", 0, &trigger);
	sf_adt_u32(dc, "latency", 0, &latency);
	sf_adt_u32(dc, "filterLengths", 0, &lengths);
	sf_adt_u32(dc, "ratios", 0, &ratios);
	IORegistryEntry *node = IORegistryEntry::fromPath(dc, gIODTPlane);
	OSData *coeff = node ? OSDynamicCast(OSData, node->getProperty("coefficients")) : NULL;
	if (coeff != NULL && coeff->getLength() == sizeof(kSFDecimCoefficients))
		same = memcmp(coeff->getBytesNoCopy(), kSFDecimCoefficients, sizeof(kSFDecimCoefficients)) == 0;
	OSSafeReleaseNULL(node);
	LOG("device tree audio-pdm2: bytesPerSample=%u clockSource=0x%08x pdmFrequency=%u pdmcFrequency=%u "
	    "slowClockSpeed=%u fastClockSpeed=%u channelPolaritySelect=%u channelPhaseSelect=%u micTurnOnTimeMs=%u "
	    "micSettleTimeMs=%u channelCount=%u channelsEnabled=%u channelsSupported=%u voiceTriggerChannel=%u", bps, src,
	    pdmf, pdmc, slow, fast, pol, phase, on, settle, count, enabled, supported, trigger);
	LOG("device tree dc-2400000: latency=%u ratios=0x%08x filterLengths=0x%x coefficients %s the ones Linux sends "
	    "(Linux: latency %u, ratios %u/%u/%u, filter lengths 0x%x)", latency, ratios, lengths,
	    same ? "equal" : "DIFFER FROM", SF_DECIM_LATENCY, SF_DECIM_RATIO1, SF_DECIM_RATIO2, SF_DECIM_RATIO3,
	    SF_FILTER_LENGTHS);
}

/* SndSocAopDriver::probe() */
static int
audio_probe(void)
{
	static const struct { uint32_t id; const char *name; } devs[] = {
		{ AUDIO_DEV_PDM0, "pdm0" }, { AUDIO_DEV_HPAI, "hpai" }, { AUDIO_DEV_LPAI, "lpai" },
	};
	int err;

	log_adt_audio();
	for (unsigned i = 0; i < sizeof(devs) / sizeof(devs[0]); i++)
		if ((err = audio_attach_device(devs[i].id, devs[i].name)) != 0)
			return err;

	/* set_lpai_channel_cfg() */
	struct LpaiChannelConfig lpai = { 7, 7, 1, 7 };
	if ((err = audio_set_prop(AUDIO_DEV_LPAI, 301, &lpai, sizeof(lpai),
	    "lpai channel configuration (enabled 7, supported 7, voice trigger 1, 7)")) != 0)
		return err;

	/* set_pdm_config() */
	struct SFMConfig *pdm = (struct SFMConfig *)IOMalloc(sizeof(*pdm));
	if (pdm == NULL)
		return ENOMEM;
	bzero(pdm, sizeof(*pdm));
	pdm->bytes_per_sample = 2;                       /* HW_CFG_T8013 */
	pdm->clock_source = FOURCC('p', 'l', 'l', ' ');
	pdm->pdm_frequency = 2400000;
	pdm->pdmc_frequency = 24000000;
	pdm->slow_clock_speed = 24000000;
	pdm->fast_clock_speed = 24000000;
	pdm->channel_polarity_select = 256;
	pdm->channel_phase_select = 0;
	pdm->mic_turn_on_time_valid = 1;
	pdm->mic_turn_on_time_ms = 20;
	pdm->mic_settle_time_valid = 1;
	pdm->mic_settle_time_ms = 50;
	build_decimator_config(&pdm->decim);
	err = audio_set_prop(AUDIO_DEV_PDM0, 200, pdm, sizeof(*pdm),
	    "pdm0 configuration (2 bytes per sample, clock 'pll ', PDM 2400000 Hz, 24000000 Hz clocks, polarity 256)");
	IOFree(pdm, sizeof(*pdm));
	if (err != 0)
		return err;

	/* set_decimator_config() */
	struct DecimatorConfig *dec = (struct DecimatorConfig *)IOMalloc(sizeof(*dec));
	if (dec == NULL)
		return ENOMEM;
	build_decimator_config(dec);
	err = audio_set_prop(AUDIO_DEV_PDM0, 210, dec, sizeof(*dec),
	    "pdm0 decimator (latency 15, ratios 15/5/2, 120 coefficients)");
	IOFree(dec, sizeof(*dec));
	return err;
}

/* ---- bring-up -------------------------------------------------------------------------- */

static void
bring_up(void)
{
	char why[160];
	uint32_t verbose = 0;
	int err;

	PE_parse_boot_argn("sfaop_verbose", &verbose, sizeof(verbose));
	gAop.verbose = verbose != 0;

	/* (a) the coprocessor */
	why[0] = 0;
	if ((err = gAop.boot(AOP_WINDOW_IOVA, AOP_WINDOW_BYTES, why, sizeof(why))) != 0) {
		fail(why[0] ? why : "the AOP did not boot");
		return;
	}
	gAopUp = true;
	if ((err = gAop.startEndpoints()) != 0) {
		fail("the AOP's application endpoints could not be started");
		return;
	}

	/* (b) its services: they announce themselves once the endpoints run */
	for (int i = 0; i < 300 && gAop.findService("aop-audio") == NULL && !gAop.crashed; i++)
		gAop.pollFor(10);
	gAop.pollFor(300);              /* the rest of the announcements and the firmware's log */
	for (unsigned i = 0; i < SF_AOP_EP_COUNT; i++) {
		const SFAopEndpoint *e = &gAop.ep[i];
		if (e->started)
			LOG("endpoint 0x%02x: buffer 0x%x bytes at iova 0x%llx, rx ring 0x%x/%u, tx ring 0x%x/%u, "
			    "%u announcements, %u reports", e->index, e->bufSize, e->iova, e->rx.size, e->rx.block, e->tx.size,
			    e->tx.block, e->announces, e->reports);
	}
	LOG("%u services announced; DMA window: 0x%x of 0x%x bytes handed out; %u syslog lines; dart faults %u",
	    gAop.nservices, gAop.dartUsed, gAop.dart.size, gAop.syslogLines, gAop.dart.faults);
	gAudio = gAop.findService("aop-audio");
	if (gAop.crashed) {
		fail("the AOP firmware crashed while starting");
		return;
	}
	if (gAudio == NULL) {
		fail("the AOP did not announce the service \"aop-audio\"");
		return;
	}
	LOG("audio service \"%s\": endpoint 0x%02x, channel %u", gAudio->name, gAudio->ep, gAudio->channel);
	if (gLevel < 2) {
		LOG("the AOP runs; the audio devices are not configured (sfaop=%u)", gLevel);
		set_state(kStateAopOnly);
		return;
	}

	/* (c) the audio devices */
	if ((err = audio_probe()) != 0) {
		fail("the audio devices could not be attached and configured");
		return;
	}
	gAop.pollFor(200);
	if (gLevel < 3) {
		LOG("audio devices configured; no capture (sfaop=%u)", gLevel);
		set_state(kStateConfigured);
		return;
	}

	/* the capture ring behind the DMA controller's stream; the controller itself is not touched yet */
	if ((err = gRingDart.attach("/arm-io/dart-aop", ADMAC_STREAM, RING_IOVA, RING_BYTES)) != 0) {
		fail("dart-aop stream 7 could not be attached");
		return;
	}
	if (!gAdmacRegs.mapNode("/arm-io/admac-aop-audio", 0)) {
		fail("the registers of /arm-io/admac-aop-audio could not be mapped");
		return;
	}
	gChan.admac = &gAdmacRegs;
	gChan.ch = ADMAC_CHANNEL;
	gChan.irqIndex = ADMAC_IRQ_INDEX;
	LOG("stream: %u Hz, %u channels, 32-bit float little-endian, %u bytes per frame; admac-aop-audio 0x%llx "
	    "channel %u (receive), dart-aop stream %u, ring %u periods of %u frames at iova 0x%llx", RATE, CHANNELS,
	    FRAME_BYTES, gAdmacRegs.phys, ADMAC_CHANNEL, ADMAC_STREAM, RING_PERIODS, PERIOD_FRAMES, RING_IOVA);
	set_state(kStateIdle);
}

/* ---- capture ---------------------------------------------------------------------------- */

static void
submit_period(void)
{
	gChan.submit(RING_IOVA + (gSubmitted % RING_PERIODS) * PERIOD_BYTES, PERIOD_BYTES);
	gSubmitted++;
}

/*
 * The power states of the AOP's own audio blocks, which its firmware switches: read only,
 * for the record of what a capture turns on.
 */
static void
log_aop_domains(const char *when)
{
	if (!gDiag)
		return;
	static const char *const names[] = {
		"AOP_LEAP_AOPCLK", "AOP_LEAP", "AOP_AUDIO_SHIM", "AOP_AUDIO_ADMA0", "AOP_PDMC_LPD", "AOP_PDM0_REF",
		"AOP_SFM1_REF", "AOP_SHIM", "AOP_ADMA0", "AOP_MCA0", "AOP_GPIO", "AOP_FILTER", "AOP_BASE", "AUDIO_P",
	};
	char text[400];
	size_t at = 0;

	text[0] = 0;
	for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]) && at < sizeof(text) - 40; i++)
		at += snprintf(text + at, sizeof(text) - at, " %s=0x%x", names[i], sf_pmgr_state_named(names[i]));
	LOG("pmgr (%s):%s", when, text);
}

/*
 * The pins of the AOP's GPIO block and of the NUB GPIO block, whose pins 8..22 the AOP may
 * drive itself (dart-aop's address filter): read only. The first call logs every pin's
 * configuration word, later calls the pins that changed.
 */
static SFMmio gPinRegs[2];

static void
log_pins(const char *when)
{
	static const struct { const char *path; const char *name; unsigned pins; } blocks[] = {
		{ "/arm-io/aop-gpio", "aop-gpio", 42 }, { "/arm-io/nub-gpio", "nub-gpio", 23 },
	};
	SFMmio *regs = gPinRegs;
	static uint32_t seen[2][42];
	static bool first = true;

	if (!gDiag)
		return;

	for (unsigned b = 0; b < 2; b++) {
		char text[420];
		size_t at = 0;

		if (regs[b].va == NULL && !regs[b].mapNode(blocks[b].path, 0))
			continue;
		text[0] = 0;
		for (unsigned pin = 0; pin < blocks[b].pins; pin++) {
			uint32_t v = regs[b].r32(4 * pin);

			if (first) {
				at += snprintf(text + at, sizeof(text) - at, " %x", v);
				if (pin % 21 == 20) {
					LOG("%s pins %u..%u (%s):%s", blocks[b].name, pin - 20, pin, when, text);
					at = 0;
					text[0] = 0;
				}
			} else if (v != seen[b][pin]) {
				LOG("%s pin %u (%s): 0x%x -> 0x%x", blocks[b].name, pin, when, seen[b][pin], v);
			}
			seen[b][pin] = v;
		}
		if (first && at != 0)
			LOG("%s last pins (%s):%s", blocks[b].name, when, text);
	}
	first = false;
}

/*
 * Reads the level of every pin of the AOP's GPIO block 1024 times in a row and logs, for
 * the pins whose level changed at all, how often it read high, how often it changed
 * between two reads and how long the reads took: a clock shows as about half high with
 * changes / (2 * time) its frequency (when the reads are faster than its half period), a
 * live PDM data line as about half high with irregular changes, a line nobody drives never
 * changes. Read only.
 */
static void
log_pin_activity(const char *when)
{
	char text[440];
	size_t at = 0;
	unsigned still = 0;

	if (!gDiag || gPinRegs[0].va == NULL)
		return;
	text[0] = 0;
	for (unsigned pin = 0; pin < 42; pin++) {
		uint32_t high = 0, changes = 0, last = gPinRegs[0].r32(4 * pin) & 1;
		uint64_t t0, t1, ns0, ns1;

		clock_get_uptime(&t0);
		for (unsigned n = 0; n < 1024; n++) {
			uint32_t v = gPinRegs[0].r32(4 * pin) & 1;

			high += v;
			changes += v != last;
			last = v;
		}
		clock_get_uptime(&t1);
		absolutetime_to_nanoseconds(t0, &ns0);
		absolutetime_to_nanoseconds(t1, &ns1);
		if (changes == 0) {
			still++;
			continue;
		}
		if (at < sizeof(text) - 60)
			at += snprintf(text + at, sizeof(text) - at, " pin%u(0x%x)=%u/%u/%lluus", pin, gPinRegs[0].r32(4 * pin),
			    high, changes, (ns1 - ns0) / 1000);
	}
	LOG("aop-gpio activity (%s): %u pins never changed in 1024 reads; high/changes/time of the others:%s", when,
	    still, text[0] ? text : " none");
}

/* aop_pcm_open(), aop_hw_params(), snd_dmaengine_pcm_trigger(START) */
static void
start_capture(void)
{
	uint32_t st[4];
	int err;

	LOG("capture: starting (session %u)", gSessions + 1);
	if (gSessions == 0)
		log_aop_domains("idle");
	if (gSessions == 0) {
		log_pins("idle");
		log_pin_activity("idle");
	}
	if ((err = set_audio_power(POWER_STATE_IDLE, 0)) != 0) {
		LOG("capture: could not enter 'pw1 '");
		goto failed;
	}
	if (gSessions == 0)
		log_aop_domains("pw1");
	if (gSessions == 0)
		log_pins("pw1");
	/* from here the AOP has the DMA controller powered: admac_alloc_chan_resources() */
	LOG("admac: tx sram %u bytes, rx sram %u bytes", gChan.txSramSize(), gChan.rxSramSize());
	gChan.allocResources(0);
	if ((err = set_audio_power(POWER_STATE_ON, 1)) != 0) {
		LOG("capture: could not power the microphones on");
		set_audio_power(POWER_STATE_OFF, 0);
		goto failed;
	}
	/* hw_params: 32-bit words, src_port_window_size 4 */
	gChan.config(4, 4);
	bzero(gRingDart.buf, RING_BYTES);

	IOLockLock(gLock);
	gSubmitted = gDone = 0;
	gReadPos = 0;
	gFirstStamp = gLastStamp = gStampPeriods = 0;
	gRateMilliHz = 0;
	gSessionWindows = 0;
	gAccPeriods = 0;
	bzero(gAcc, sizeof(gAcc));
	bzero(gAccPairDiff, sizeof(gAccPairDiff));
	gSessions++;
	gSessionError = 0;
	IOLockUnlock(gLock);

	gLastProgress = gLastStatus = 0;
	/* admac_start_current_tx() */
	gChan.resetRings();
	submit_period();
	gChan.start();
	while (gSubmitted < MAX_QUEUED && !gChan.descRingFull())
		submit_period();
	gChan.status(st);
	LOG("capture: DMA channel %u started: enabled=%d desc_ring=0x%08x report_ring=0x%08x status=0x%08x residue=0x%x",
	    ADMAC_CHANNEL, gChan.enabled(), st[0], st[1], st[2], st[3]);
	set_state(kStateCapturing);
	/* after the channel runs, so that the diagnostics do not delay its start */
	if (gSessions == 1) {
		log_aop_domains("pwrd");
		log_pins("pwrd");
		log_pin_activity("pwrd");
	}
	return;

failed:
	IOLockLock(gLock);
	gSessionError = err;
	gTimedPeriods = 0;
	gReaderOpen = false;            /* the opener is told; nothing asks for a second attempt */
	IOLockWakeup(gLock, &gStateEvent, false);
	IOLockUnlock(gLock);
}

/* trigger(STOP) and aop_pcm_close() */
static void
stop_capture(const char *why)
{
	uint32_t st[4];
	bool stopped = gChan.stop();

	gChan.status(st);
	gChan.resetRings();
	LOG("capture: stopping (%s): channel %s, desc_ring=0x%08x report_ring=0x%08x status=0x%08x; %llu periods, "
	    "%u ring errors, %u overruns, %u dart faults", why, stopped ? "stopped" : "STILL ENABLED", st[0], st[1], st[2],
	    gDone, gRingErrors, gOverruns, gRingDart.faults);
	if (set_audio_power(POWER_STATE_IDLE, 1) != 0)
		LOG("capture: could not power the microphones off");
	if (set_audio_power(POWER_STATE_OFF, 0) != 0)
		LOG("capture: could not enter 'idle'");
	if (gSessions == 1) {
		log_aop_domains("idle again");
		log_pins("idle again");
	}
	IOLockLock(gLock);
	gState = kStateIdle;
	gTimedPeriods = 0;
	IOLockWakeup(gLock, &gReadEvent, false);
	IOLockWakeup(gLock, &gStateEvent, false);
	IOLockUnlock(gLock);
}

/*
 * A 32-bit float as Q23 fixed point (8388608 is 1.0), clamped to +-8.0. The kernel does
 * no floating point; this is exact for every magnitude from 2^-23 up.
 */
static int32_t
q23_from_float(uint32_t bits, ChannelStats *s)
{
	uint32_t exp = (bits >> 23) & 0xff, man = (bits & 0x7fffff) | 0x800000;
	int64_t v;

	if (exp == 0xff) {
		s->nonfinite++;
		return 0;
	}
	if (exp == 0)
		return 0;                       /* zero and denormals: below 2^-126 */
	int shift = (int)exp - 127;             /* value = man * 2^(shift - 23) */
	if (shift >= 3) {
		s->clipped++;
		v = (int64_t)8 << 23;
	} else if (shift >= 0) {
		v = (int64_t)man << shift;
	} else if (shift > -24) {
		v = man >> -shift;
	} else {
		v = 0;
	}
	return (int32_t)((bits >> 31) ? -v : v);
}

static uint32_t
isqrt64(uint64_t x)
{
	uint64_t r = 0, bit = 1ULL << 62;

	while (bit > x)
		bit >>= 2;
	while (bit != 0) {
		if (x >= r + bit) {
			x -= r + bit;
			r = (r >> 1) + bit;
		} else {
			r >>= 1;
		}
		bit >>= 2;
	}
	return (uint32_t)r;
}

/* One completed period: statistics. Called with gLock held. */
static void
account_period(const uint32_t *frames)
{
	for (unsigned c = 0; c < CHANNELS; c++)
		gAcc[c].periodSquares = 0;
	for (unsigned f = 0; f < PERIOD_FRAMES; f++) {
		const uint32_t *fr = frames + f * CHANNELS;

		for (unsigned c = 0; c < CHANNELS; c++) {
			ChannelStats *s = &gAcc[c];
			uint32_t bits = fr[c];
			int32_t v = q23_from_float(bits, s);

			if (gAccPeriods == 0 && f == 0) {
				s->min = s->max = v;
				s->last = ~bits;
			}
			if ((bits << 1) == 0)
				s->zeros++;
			if (bits == s->last)
				s->repeats++;
			s->last = bits;
			s->sum += v;
			s->periodSquares += (uint64_t)((int64_t)v * v);
			if (v < s->min)
				s->min = v;
			if (v > s->max)
				s->max = v;
		}
		gAccPairDiff[0] += fr[0] != fr[1];
		gAccPairDiff[1] += fr[0] != fr[2];
		gAccPairDiff[2] += fr[1] != fr[2];
	}
	for (unsigned c = 0; c < CHANNELS; c++)
		gAcc[c].meanSquares += gAcc[c].periodSquares / PERIOD_FRAMES;
	if (++gAccPeriods < WINDOW_PERIODS)
		return;

	for (unsigned c = 0; c < CHANNELS; c++) {
		ChannelStats *s = &gAcc[c];
		WindowResult *w = &gWin[c];
		int32_t peak = s->max > -s->min ? s->max : -s->min;

		w->mean = (int32_t)(s->sum / (int64_t)WINDOW_FRAMES);
		w->rms = (int32_t)isqrt64(s->meanSquares / WINDOW_PERIODS);
		w->peak = peak;
		w->min = s->min;
		w->max = s->max;
		w->zeros = s->zeros;
		w->repeats = s->repeats;
		w->nonfinite = s->nonfinite;
		w->clipped = s->clipped;
		if (peak > gPeakEver[c])
			gPeakEver[c] = peak;
	}
	memcpy(gPairDiff, gAccPairDiff, sizeof(gPairDiff));
	gWindows++;
	gSessionWindows++;
	gAccPeriods = 0;
	bzero(gAcc, sizeof(gAcc));
	bzero(gAccPairDiff, sizeof(gAccPairDiff));
}

static void
log_window(void)
{
	WindowResult w[CHANNELS];
	uint32_t diff[3], n;

	IOLockLock(gLock);
	memcpy(w, gWin, sizeof(w));
	memcpy(diff, gPairDiff, sizeof(diff));
	n = gSessionWindows;
	IOLockUnlock(gLock);
	for (unsigned c = 0; c < CHANNELS; c++)
		LOG("window %u mic %u: mean=%d rms=%d peak=%d min=%d max=%d (Q23, 8388608 = 1.0) zeros=%u repeats=%u "
		    "nonfinite=%u clipped=%u of %u", n, c, w[c].mean, w[c].rms, w[c].peak, w[c].min, w[c].max, w[c].zeros,
		    w[c].repeats, w[c].nonfinite, w[c].clipped, WINDOW_FRAMES);
	LOG("window %u: frames where the microphones differ: 0/1 %u, 0/2 %u, 1/2 %u of %u", n, diff[0], diff[1], diff[2],
	    WINDOW_FRAMES);
}

static void
service_capture(void)
{
	uint64_t stamp = 0, now, ns;
	unsigned reports = gChan.drainReports(&stamp);
	bool error = gChan.clearError();
	uint32_t windowsBefore, windowsAfter;

	clock_get_uptime(&now);
	absolutetime_to_nanoseconds(now, &ns);
	IOLockLock(gLock);
	windowsBefore = gSessionWindows;
	if (error)
		gRingErrors++;
	for (unsigned i = 0; i < reports; i++) {
		const uint32_t *frames = (const uint32_t *)(gRingDart.buf + (gDone % RING_PERIODS) * PERIOD_BYTES);

		if (gDone == 0)
			memcpy(gFirstFrame, frames, sizeof(gFirstFrame));
		memcpy(gLastFrame, frames + (PERIOD_FRAMES - 1) * CHANNELS, sizeof(gLastFrame));
		account_period(frames);
		gDone++;
		gTotalPeriods++;
		if (gTimedPeriods > 0)
			gTimedPeriods--;
	}
	if (reports > 0) {
		gLastProgress = ns;
		/* the timestamp of the last report: 24 MHz ticks per period give the real rate */
		if (gFirstStamp == 0) {
			gFirstStamp = stamp;
			gStampPeriods = 0;
		} else {
			gStampPeriods += reports;
			gLastStamp = stamp;
			if (gLastStamp > gFirstStamp)
				gRateMilliHz = (uint32_t)(gStampPeriods * PERIOD_FRAMES * ADMAC_COUNTER_HZ * 1000ULL /
				    (gLastStamp - gFirstStamp));
		}
		IOLockWakeup(gLock, &gReadEvent, false);
	} else if (gLastProgress == 0 || ns < gLastProgress) {
		gLastProgress = ns;
	}
	windowsAfter = gSessionWindows;
	IOLockUnlock(gLock);

	while (gSubmitted - gDone < MAX_QUEUED && !gChan.descRingFull())
		submit_period();

	if (reports > 0 && gDone == reports) {
		const uint32_t *f = (const uint32_t *)gRingDart.buf;
		LOG("capture: first period done: frames 0..3 raw: %08x %08x %08x | %08x %08x %08x | %08x %08x %08x | "
		    "%08x %08x %08x", f[0], f[1], f[2], f[3], f[4], f[5], f[6], f[7], f[8], f[9], f[10], f[11]);
	}
	if (windowsAfter != windowsBefore && windowsAfter <= 8) {
		log_window();
		if (windowsAfter == 2 && gSessions == 1) {
			log_aop_domains("capturing");
			log_pins("capturing");
			log_pin_activity("capturing");
		}
	}
	if (ns - gLastStatus > 1000000000ULL) {
		uint32_t st[4];

		gLastStatus = ns;
		gChan.status(st);
		gRingDart.check("capture");
		gAop.dart.check("capture");
		IOLockLock(gLock);
		memcpy(gAdmacSeen, st, sizeof(gAdmacSeen));
		IOLockUnlock(gLock);
	}
	if (reports == 0 && ns - gLastProgress > 2000000000ULL) {
		uint32_t st[4];

		gLastProgress = ns;
		gChan.status(st);
		IOLockLock(gLock);
		gStalls++;
		IOLockUnlock(gLock);
		LOG("capture: no period completed for 2 s: enabled=%d desc_ring=0x%08x report_ring=0x%08x status=0x%08x "
		    "residue=0x%x submitted=%llu done=%llu", gChan.enabled(), st[0], st[1], st[2], st[3], gSubmitted, gDone);
	}
}

/* ---- /dev/sfmic ------------------------------------------------------------------------- */

static int
sf_mic_open(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	int err = 0;

	IOLockLock(gLock);
	if (gState < kStateIdle || gShutdown) {
		err = ENXIO;
	} else if (gReaderOpen) {
		err = EBUSY;
	} else {
		AbsoluteTime deadline;

		gReaderOpen = true;
		gSessionError = 0;
		clock_interval_to_deadline(20, kSecondScale, &deadline);
		/* the thread starts the capture; a session that already runs is joined at its end */
		while (gState != kStateCapturing && gState >= kStateIdle && gSessionError == 0) {
			if (IOLockSleepDeadline(gLock, &gStateEvent, deadline, THREAD_ABORTSAFE) != THREAD_AWAKENED) {
				err = EINTR;
				break;
			}
		}
		if (err == 0 && gState != kStateCapturing)
			err = gSessionError ? gSessionError : EIO;
		if (err != 0)
			gReaderOpen = false;
		else
			gReadPos = gDone * PERIOD_BYTES;
	}
	IOLockUnlock(gLock);
	return err;
}

static int
sf_mic_close(__unused dev_t dev, __unused int flags, __unused int devtype, __unused struct proc *p)
{
	IOLockLock(gLock);
	gReaderOpen = false;
	IOLockUnlock(gLock);
	return 0;
}

static int
sf_mic_read(__unused dev_t dev, struct uio *uio, __unused int ioflag)
{
	int err = 0;

	while (err == 0 && uio_resid(uio) > 0) {
		uint32_t n = 0;

		IOLockLock(gLock);
		while (err == 0 && gState == kStateCapturing && gReadPos >= gDone * PERIOD_BYTES) {
			if (IOLockSleep(gLock, &gReadEvent, THREAD_ABORTSAFE) != THREAD_AWAKENED)
				err = EINTR;
		}
		if (err == 0 && gState != kStateCapturing) {
			IOLockUnlock(gLock);
			break;                  /* end of the stream */
		}
		if (err == 0) {
			uint64_t end = gDone * PERIOD_BYTES;

			if (end - gReadPos > READ_BEHIND_MAX) {
				/* the controller is about to overwrite what was not read: skip to recent data */
				gReadPos = end - READ_BEHIND_MAX / 2;
				gOverruns++;
			}
			uint32_t off = (uint32_t)(gReadPos % RING_BYTES);
			uint64_t want = end - gReadPos;

			if (want > PERIOD_BYTES)
				want = PERIOD_BYTES;
			if (want > (uint64_t)uio_resid(uio))
				want = (uint64_t)uio_resid(uio);
			if (want > RING_BYTES - off)
				want = RING_BYTES - off;
			n = (uint32_t)want;
			memcpy(gReadChunk, gRingDart.buf + off, n);
			gReadPos += n;
		}
		IOLockUnlock(gLock);
		if (err == 0 && n > 0)
			err = uiomove((char *)gReadChunk, (int)n, uio);
	}
	return err;
}

static struct cdevsw sf_mic_cdevsw = {
	sf_mic_open, sf_mic_close, sf_mic_read, eno_rdwrt,
	eno_ioctl, eno_stop, eno_reset, 0, eno_select, eno_mmap,
	eno_strat, eno_getc, eno_putc, 0
};

/* devfs is not there yet when the driver starts: keep trying from the thread. */
static void
publish_node(void)
{
	if (gDevNode != NULL)
		return;
	if (gReadChunk == NULL)
		gReadChunk = (uint8_t *)IOMalloc(PERIOD_BYTES);
	if (gReadChunk == NULL)
		return;
	if (gMajor < 0)
		gMajor = cdevsw_add(-1, &sf_mic_cdevsw);
	if (gMajor >= 0)
		gDevNode = devfs_make_node(makedev(gMajor, 0), DEVFS_CHAR, 0, 0, 0444, "sfmic");
	if (gDevNode != NULL)
		LOG("/dev/sfmic published (major %d)", gMajor);
}

/* ---- the thread ------------------------------------------------------------------------- */

static void
sf_aop_thread(__unused void *arg, __unused wait_result_t wr)
{
	uint32_t seconds = 0;
	unsigned tick = 0;

	IOSleep(gDelayMs);
	bring_up();
	if (gState == kStateIdle && PE_parse_boot_argn("sfaop_capture", &seconds, sizeof(seconds)) && seconds > 0 &&
	    seconds <= 600) {
		IOLockLock(gLock);
		gTimedPeriods = seconds * RATE / PERIOD_FRAMES;
		IOLockUnlock(gLock);
	}

	for (;;) {
		int state;
		bool want;

		/* the firmware's log and reports are taken whether anything is captured or not */
		if (gAopUp)
			gAop.poll();

		IOLockLock(gLock);
		state = gState;
		want = !gShutdown && (gReaderOpen || gTimedPeriods > 0);
		IOLockUnlock(gLock);

		if (state == kStateIdle && want) {
			start_capture();
		} else if (state == kStateCapturing) {
			if (gAop.crashed) {
				stop_capture("the AOP firmware crashed");
				fail("the AOP firmware crashed");
			} else if (!want) {
				stop_capture(gShutdown ? "shutdown" : "nobody wants it any more");
			} else {
				service_capture();
			}
		}
		if (gState >= kStateIdle && gDevNode == NULL && tick % 25 == 0)
			publish_node();
		tick++;
		IOSleep(gState == kStateCapturing ? 4 : 20);
	}
}

/* ---- sysctl ----------------------------------------------------------------------------- */

enum {
	kCtlCapture, kCtlPeriods, kCtlFrames, kCtlSessions, kCtlOverruns, kCtlRingErrors, kCtlDartFaults, kCtlRate,
	kCtlWindows, kCtlStalls, kCtlSyslog, kCtlCalls, kCtlCallErrors, kCtlCapturing, kCtlServices,
};

static int
sf_sysctl_int(struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	int value = 0, error;

	IOLockLock(gLock);
	switch (arg2) {
	case kCtlCapture:    value = (int)((uint64_t)gTimedPeriods * PERIOD_FRAMES / RATE); break;
	case kCtlPeriods:    value = (int)gTotalPeriods; break;
	case kCtlFrames:     value = (int)(gTotalPeriods * PERIOD_FRAMES); break;
	case kCtlSessions:   value = (int)gSessions; break;
	case kCtlOverruns:   value = (int)gOverruns; break;
	case kCtlRingErrors: value = (int)gRingErrors; break;
	case kCtlDartFaults: value = (int)(gRingDart.faults + gAop.dart.faults); break;
	case kCtlRate:       value = (int)gRateMilliHz; break;
	case kCtlWindows:    value = (int)gWindows; break;
	case kCtlStalls:     value = (int)gStalls; break;
	case kCtlSyslog:     value = (int)gAop.syslogLines; break;
	case kCtlCalls:      value = (int)gAop.epicCalls; break;
	case kCtlCallErrors: value = (int)(gAop.epicErrors + gAop.epicTimeouts); break;
	case kCtlCapturing:  value = gState == kStateCapturing; break;
	case kCtlServices:   value = (int)gAop.nservices; break;
	}
	IOLockUnlock(gLock);
	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == USER_ADDR_NULL)
		return error;
	if (arg2 != kCtlCapture)
		return EPERM;
	if (value < 0 || value > 600)
		return EINVAL;
	IOLockLock(gLock);
	if (gState < kStateIdle || gShutdown)
		error = ENXIO;
	else
		gTimedPeriods = (uint32_t)value * RATE / PERIOD_FRAMES;
	IOLockUnlock(gLock);
	return error;
}

static int
sf_sysctl_string(__unused struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	char text[512];

	text[0] = 0;
	IOLockLock(gLock);
	switch (arg2) {
	case 0:
		snprintf(text, sizeof(text), "%s%s%s", kStateNames[gState], gFailure[0] ? ": " : "", gFailure);
		break;
	case 1:
		/* the thread's own numbers, read without it: a torn value is harmless here */
		snprintf(text, sizeof(text), "protocol=%u iop_power=0x%x ap_power=0x%x endpoints=0x%08x/0x%08x crashed=%d "
		    "rx=%u tx=%u unknown=%u syslog=%u window_used=0x%x/0x%x", gAop.version, gAop.iopPower, gAop.apPower,
		    gAop.endpoints[0], gAop.endpoints[1], gAop.crashed, gAop.rxMessages, gAop.txMessages,
		    gAop.unknownMessages, gAop.syslogLines, gAop.dartUsed, gAop.dart.size);
		break;
	case 2:
		if (gAudio != NULL)
			snprintf(text, sizeof(text), "service=%s endpoint=0x%02x channel=%u calls=%u errors=%u timeouts=%u",
			    gAudio->name, gAudio->ep, gAudio->channel, gAop.epicCalls, gAop.epicErrors, gAop.epicTimeouts);
		break;
	case 3:
		snprintf(text, sizeof(text), "rate=%u channels=%u format=float32le frame_bytes=%u admac_channel=%u "
		    "dart_stream=%u ring_iova=0x%llx period_frames=%u ring_periods=%u submitted=%llu done=%llu "
		    "desc_ring=0x%08x report_ring=0x%08x status=0x%08x residue=0x%x first=%08x/%08x/%08x last=%08x/%08x/%08x",
		    RATE, CHANNELS, FRAME_BYTES, ADMAC_CHANNEL, ADMAC_STREAM, RING_IOVA, PERIOD_FRAMES, RING_PERIODS,
		    gSubmitted, gDone, gAdmacSeen[0], gAdmacSeen[1], gAdmacSeen[2], gAdmacSeen[3], gFirstFrame[0],
		    gFirstFrame[1], gFirstFrame[2], gLastFrame[0], gLastFrame[1], gLastFrame[2]);
		break;
	case 4: case 5: case 6: {
		const WindowResult *w = &gWin[arg2 - 4];
		snprintf(text, sizeof(text), "window=%u mean=%d rms=%d peak=%d min=%d max=%d zeros=%u repeats=%u "
		    "nonfinite=%u clipped=%u frames=%u peak_ever=%d", gWindows, w->mean, w->rms, w->peak, w->min, w->max,
		    w->zeros, w->repeats, w->nonfinite, w->clipped, WINDOW_FRAMES, gPeakEver[arg2 - 4]);
		break;
	}
	case 7:
		snprintf(text, sizeof(text), "window=%u differ01=%u differ02=%u differ12=%u frames=%u", gWindows,
		    gPairDiff[0], gPairDiff[1], gPairDiff[2], WINDOW_FRAMES);
		break;
	}
	IOLockUnlock(gLock);
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_NODE(_kern, OID_AUTO, sf_aop, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "built-in microphones (AOP)");

#define SF_INT(name, id, flags, text) \
	SYSCTL_PROC(_kern_sf_aop, OID_AUTO, name, CTLTYPE_INT | (flags) | CTLFLAG_LOCKED, 0, id, sf_sysctl_int, "I", text)
#define SF_STR(name, id, text) \
	SYSCTL_PROC(_kern_sf_aop, OID_AUTO, name, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, id, sf_sysctl_string, "A", text)

SF_STR(state, 0, "driver state and, when it failed, why");
SF_STR(rtkit, 1, "the AOP's RTKit state");
SF_STR(service, 2, "the audio service and the calls made on it");
SF_STR(stream, 3, "the capture stream and the DMA channel's registers as last read");
SF_STR(stats0, 4, "last statistics window of microphone 0 (Q23: 8388608 is 1.0)");
SF_STR(stats1, 5, "last statistics window of microphone 1");
SF_STR(stats2, 6, "last statistics window of microphone 2");
SF_STR(differ, 7, "frames of the last window in which two microphones' samples differ");
SF_INT(capture, kCtlCapture, CTLFLAG_RW, "seconds of capture without a reader still to run; write to start, 0 to stop");
SF_INT(capturing, kCtlCapturing, CTLFLAG_RD, "1 while the microphones are on");
SF_INT(periods, kCtlPeriods, CTLFLAG_RD, "DMA periods captured since boot");
SF_INT(frames, kCtlFrames, CTLFLAG_RD, "frames captured since boot");
SF_INT(sessions, kCtlSessions, CTLFLAG_RD, "capture sessions started");
SF_INT(overruns, kCtlOverruns, CTLFLAG_RD, "times the reader of /dev/sfmic fell behind and lost data");
SF_INT(ring_errors, kCtlRingErrors, CTLFLAG_RD, "DMA ring errors");
SF_INT(dart_faults, kCtlDartFaults, CTLFLAG_RD, "DART faults on dart-aop streams 0 and 7");
SF_INT(rate_mhz, kCtlRate, CTLFLAG_RD, "sample rate measured from the DMA timestamps, in mHz");
SF_INT(windows, kCtlWindows, CTLFLAG_RD, "statistics windows completed since boot");
SF_INT(stalls, kCtlStalls, CTLFLAG_RD, "times no period completed for two seconds while capturing");
SF_INT(syslog_lines, kCtlSyslog, CTLFLAG_RD, "lines of the firmware's log taken");
SF_INT(calls, kCtlCalls, CTLFLAG_RD, "EPIC calls made");
SF_INT(call_errors, kCtlCallErrors, CTLFLAG_RD, "EPIC calls that failed or were not answered");
SF_INT(services, kCtlServices, CTLFLAG_RD, "services the AOP announced");

static struct sysctl_oid *const gOids[] = {
	&sysctl__kern_sf_aop,
	&sysctl__kern_sf_aop_state, &sysctl__kern_sf_aop_rtkit, &sysctl__kern_sf_aop_service, &sysctl__kern_sf_aop_stream,
	&sysctl__kern_sf_aop_stats0, &sysctl__kern_sf_aop_stats1, &sysctl__kern_sf_aop_stats2, &sysctl__kern_sf_aop_differ,
	&sysctl__kern_sf_aop_capture, &sysctl__kern_sf_aop_capturing, &sysctl__kern_sf_aop_periods,
	&sysctl__kern_sf_aop_frames, &sysctl__kern_sf_aop_sessions, &sysctl__kern_sf_aop_overruns,
	&sysctl__kern_sf_aop_ring_errors, &sysctl__kern_sf_aop_dart_faults, &sysctl__kern_sf_aop_rate_mhz,
	&sysctl__kern_sf_aop_windows, &sysctl__kern_sf_aop_stalls, &sysctl__kern_sf_aop_syslog_lines,
	&sysctl__kern_sf_aop_calls, &sysctl__kern_sf_aop_call_errors, &sysctl__kern_sf_aop_services,
};

/* ---- IOKit ------------------------------------------------------------------------------ */

class SFAopAudio : public IOService {
	OSDeclareDefaultStructors(SFAopAudio);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void systemWillShutdown(IOOptionBits specifier) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SFAopAudio, IOService);

bool
SFAopAudio::start(IOService *provider)
{
	IORegistryEntry *chosen;
	OSData *chip;
	uint32_t chipID = 0, value = 0;
	thread_t thread;

	if (!IOService::start(provider))
		return false;

	chosen = IORegistryEntry::fromPath("/chosen", gIODTPlane);
	chip = chosen ? OSDynamicCast(OSData, chosen->getProperty("chip-id")) : NULL;
	if (chip != NULL && chip->getLength() >= sizeof(uint32_t))
		chipID = *(const uint32_t *)chip->getBytesNoCopy();
	OSSafeReleaseNULL(chosen);
	if (chipID != T8103_CHIP_ID) {
		LOG("chip 0x%x is not T8103; nothing is done", chipID);
		return false;
	}

	PE_parse_boot_argn("sfaop", &gLevel, sizeof(gLevel));
	if (PE_parse_boot_argn("sfaop_delay", &value, sizeof(value)) && value <= 60000)
		gDelayMs = value;
	PE_parse_boot_argn("sfaop_diag", &gDiag, sizeof(gDiag));
	if (gLevel == 0) {
		LOG("off (sfaop=0)");
		return false;
	}

	gLock = IOLockAlloc();
	if (gLock == NULL)
		return false;
	for (unsigned i = 0; i < sizeof(gOids) / sizeof(gOids[0]); i++)
		sysctl_register_oid(gOids[i]);
	if (kernel_thread_start(sf_aop_thread, NULL, &thread) != KERN_SUCCESS)
		return false;
	thread_deallocate(thread);
	registerService();
	return true;
}

/* Take the microphones through 'pw1 ' to 'idle' before the system goes, as a close does. */
void
SFAopAudio::systemWillShutdown(IOOptionBits specifier)
{
	if (gLock != NULL) {
		AbsoluteTime deadline;

		IOLockLock(gLock);
		gShutdown = true;
		clock_interval_to_deadline(3, kSecondScale, &deadline);
		while (gState == kStateCapturing) {
			if (IOLockSleepDeadline(gLock, &gStateEvent, deadline, THREAD_UNINT) != THREAD_AWAKENED)
				break;
		}
		IOLockUnlock(gLock);
	}
	IOService::systemWillShutdown(specifier);
}
