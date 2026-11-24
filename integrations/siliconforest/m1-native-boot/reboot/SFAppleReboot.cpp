// SPDX-License-Identifier: GPL-2.0
/*
 * reboot(2) and halt on an Apple T8103 machine, and the PMU's record of how boots end.
 *
 * Ported from Linux drivers/power/reset/macsmc-reboot.c (GPL-2.0-only OR MIT, Copyright The
 * Asahi Linux Contributors; Omacom linux-aurora integration/aurora 6a08a08e), with the SMC
 * and SPMI access it gets from drivers/mfd/macsmc.c, drivers/spmi/spmi-apple-controller.c
 * and drivers/nvmem/apple-spmi-nvmem.c (here: ../../m1-thermal/kernel/SFSmcSession and
 * sf_pmu_scratch.c).
 *
 * What Linux does, and where it is done here:
 *   probe            boot_stage = 0x30 (kernel started); if boot_error_count or panic_count
 *                    is not zero: one warning line with both, then both written 0.
 *                    -> SFAppleRebootStart(): always one line with the values; the writes
 *                       only with sfpmu=2.
 *   reboot notifier  SMC key MBSE = 'rest' (restart) or 'offw' (power-off), an ordinary
 *                    write; boot_stage = 0 (clean shutdown); shutdown_flag = 0 or 1.
 *   prepare          SMC key NTAP = 0 (no more notifications), then only polled writes.
 *   restart          MBSE = 'phra'; 100 ms; "Unable to restart system".
 *   power-off        MBSE = 'off1'; 100 ms; "Unable to power off system".
 *                    -> sf_apple_halt_restart(), the kernel's PE_halt_restart hook, in that
 *                       order. The two PMU writes only with sfpmu=2. SFSmcSession is a
 *                       polled client, which is what Linux's "atomic" mode is.
 *   panic            nothing.  -> nothing; other halt types go to the previous handler.
 * Every SMC write of this file passes sf_smc_halt_gate() (sf_halt_gate.h: MBSE with one of
 * the four values above, NTAP = 0, nothing else) directly before the one writeKey() call;
 * kern.sf_smc_halt_writes shows what was sent and refused.
 *
 * The clock (Linux drivers/rtc/rtc-macsmc.c, read side only): once the thermal thread's SMC
 * session is open, SMC key CLKM (the 48-bit counter, 32768 counts a second) and the six
 * offset bytes Linux keeps at 0xd100 of the PMU are read, and the wall-clock time they give
 * is offered as kern.sf_rtc_usec (microseconds since 1970 UTC, 0 until known) and
 * kern.sf_rtc (text). The kernel's calendar is set from it by the system's sf-clock at
 * boot (settimeofday; this kernel has no RTC service, so that changes nothing in hardware).
 * Nothing is written: the offset is Linux's (or macOS's) to keep, and a clock set under
 * this system lasts until it stops. sfrtc=0: neither is read.
 *
 * Boot-args:
 *   sfreboot=0   leave PE_halt_restart as the platform expert set it (PSCI, which this
 *                machine does not have). Default 1.
 *   sfpoweroff=1 let a halt power the machine off. Default 0: the request is logged and
 *                refused, and the kernel stops as it did before this file existed. Every
 *                machine this runs on today is a bench machine loaded over USB, and one
 *                that is off needs a person; an installed system boots with sfpoweroff=1.
 *                Without sfpmu=2 the PMU's shutdown flag is not set, and Linux's comment on
 *                that flag says the machine then restarts instead of staying off.
 *   sfpmu=0      never touch the SPMI controller.
 *   sfpmu=1      (default) read the boot state bytes once and log them. No write, at boot
 *                or later.
 *   sfpmu=2      as Linux: the probe writes above, and the two writes before a restart or
 *                power-off.
 * The PMU is only used when the device tree names what Linux's device tree names
 * (controller at 0x23d0d9300, primary PMU at address 0xf, legacy scratchpad at 0x9f00).
 */
#include "SFAppleReboot.h"

#include <IOKit/IODeviceMemory.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IORegistryEntry.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSCollectionIterator.h>
#include <libkern/c++/OSData.h>
#include <pexpert/pexpert.h>

#include <sys/sysctl.h>
#include "SFSmcSession.h"
#include "sf_pmu_scratch.h"
#include "sf_halt_gate.h"

#include <kern/clock.h>

#define LOG(fmt, ...) IOLog("SFReboot: " fmt "\n", ##__VA_ARGS__)

#define SOC_PAGE 0x4000ULL

static IOLock *gSmcLock;
static SFSmcSession *gThermalSmc;       /* under gSmcLock */
static int (*gPreviousHaltRestart)(unsigned int type);

static uint32_t gPmuMode = 1;
static uint32_t gPowerOffAllowed;
static bool gPmuUsable;
static IOMemoryDescriptor *gSpmiMD;
static IOMemoryMap *gSpmiMap;
static volatile UInt8 *gSpmiVA;
static struct sf_spmi_io gSpmiIO;
static struct sf_pmu gPmu;

/* the SMC write gate's counters (sf_halt_gate.h) */
static uint32_t gSmcHaltSent, gSmcHaltRefused, gSmcHaltLastRefusal;

/* the clock: read once, kept as "this many microseconds since 1970 at this uptime" */
static uint32_t gRtcMode = 1;
static uint32_t gRtcTries;
static int gRtcResult = -1;             /* -1 not read yet; SF_RTC_*; 100 + SPMI error; 200 SMC */
static volatile int64_t gRtcBaseUsec;   /* 0 until known */
static uint64_t gRtcBaseUptimeNs;
#define RTC_MAX_TRIES 30

static uint32_t
spmi_read32(void *, unsigned offset)
{
	uint32_t value = OSReadLittleInt32((volatile void *)gSpmiVA, offset);
	OSSynchronizeIO();
	return value;
}

static void
spmi_write32(void *, unsigned offset, uint32_t value)
{
	OSWriteLittleInt32((volatile void *)gSpmiVA, offset, value);
	OSSynchronizeIO();
}

static void
spmi_delay(void *, unsigned us)
{
	IODelay(us);
}

static bool
has_compatible(IORegistryEntry *entry, const char *want)
{
	OSData *d = OSDynamicCast(OSData, entry->getProperty("compatible"));
	if (d == NULL)
		return false;
	const char *p = (const char *)d->getBytesNoCopy();
	size_t left = d->getLength(), n = strlen(want);
	while (left) {
		size_t l = strnlen(p, left);
		if (l == n && memcmp(p, want, n) == 0)
			return true;
		if (l >= left)
			break;
		p += l + 1;
		left -= l + 1;
	}
	return false;
}

static bool
property_u32(IORegistryEntry *entry, const char *name, uint32_t *value)
{
	OSData *d = OSDynamicCast(OSData, entry->getProperty(name));
	if (d == NULL || d->getLength() < sizeof(uint32_t))
		return false;
	*value = OSReadLittleInt32(d->getBytesNoCopy(), 0);
	return true;
}

/* m1n1 proxyclient/m1n1/hw/pmu.py find_primary_pmu(), for the SPMI case, with the
 * addresses required to be the ones Linux's t8103.dtsi carries. */
static bool
pmu_find(void)
{
	IORegistryEntry *node = IORegistryEntry::fromPath("/arm-io/nub-spmi", gIODTPlane);
	IORegistryEntry *child, *pmu = NULL;
	OSIterator *children;
	uint64_t phys = 0;
	uint32_t sid = ~0u, scrpad = 0, pm_setting = 0, primary = 0;

	if (node == NULL) {
		LOG("no /arm-io/nub-spmi in the device tree; the PMU is not used");
		return false;
	}
	/* The registry owns the returned array (as in SFSmcSession). */
	OSArray *regs = IODTResolveAddressing(node, "reg", NULL);
	IOMemoryDescriptor *d = regs ? OSDynamicCast(IOMemoryDescriptor, regs->getObject(0)) : NULL;
	if (d != NULL)
		phys = d->getPhysicalSegment(0, NULL, kIOMemoryMapperNone);
	children = node->getChildIterator(gIODTPlane);
	while (children != NULL && pmu == NULL && (child = OSDynamicCast(IORegistryEntry, children->getNextObject())) != NULL) {
		if (has_compatible(child, "pmu,spmi") && property_u32(child, "is-primary", &primary) && primary == 1)
			pmu = child;
	}
	if (pmu != NULL) {
		property_u32(pmu, "reg", &sid);
		property_u32(pmu, "info-leg_scrpad", &scrpad);
		property_u32(pmu, "info-pm_setting", &pm_setting);
	}
	OSSafeReleaseNULL(children);
	node->release();

	if (pmu == NULL || phys != SF_T8103_NUB_SPMI_PHYS || sid != SF_T8103_PMU_SID || scrpad != SF_T8103_LEG_SCRPAD) {
		LOG("the device tree's PMU is not the one this was written for (controller 0x%llx, pmu %s, address 0x%x, "
		    "scratchpad 0x%x); the PMU is not used", (unsigned long long)phys, pmu ? "found" : "not found", sid, scrpad);
		return false;
	}

	uint64_t start = phys & ~(SOC_PAGE - 1);
	gSpmiMD = IODeviceMemory::withRange((IOPhysicalAddress)start, (IOPhysicalLength)SOC_PAGE);
	gSpmiMap = gSpmiMD ? gSpmiMD->map(kIOMapInhibitCache) : NULL;
	if (gSpmiMap == NULL || gSpmiMap->getVirtualAddress() == 0) {
		LOG("could not map the SPMI controller at 0x%llx; the PMU is not used", (unsigned long long)phys);
		OSSafeReleaseNULL(gSpmiMap);
		OSSafeReleaseNULL(gSpmiMD);
		return false;
	}
	gSpmiVA = (volatile UInt8 *)gSpmiMap->getVirtualAddress() + (phys - start);
	gSpmiIO.ctx = NULL;
	gSpmiIO.read32 = spmi_read32;
	gSpmiIO.write32 = spmi_write32;
	gSpmiIO.delay_us = spmi_delay;
	gPmu.io = &gSpmiIO;
	gPmu.sid = (uint8_t)sid;
	gPmu.scrpad = (uint16_t)scrpad;
	/* Linux t8103.dtsi pm_setting@d001; read for the log only */
	gPmu.pm_setting = pm_setting == 0xd001 ? 0xd001 : 0;
	/* The write gate's level (sf_pmu_scratch.h): only sfpmu=2 lets any SPMI write out. */
	gPmu.level = gPmuMode == 2 ? SF_PMU_WRITES_AS_LINUX : SF_PMU_READ_ONLY;
	return true;
}

static void
pmu_start(void)
{
	struct sf_pmu_state state;
	int ret;

	if (gPmuMode == 0) {
		LOG("sfpmu=0: the PMU is not read");
		return;
	}
	if (!pmu_find())
		return;
	ret = sf_pmu_read_state(&gPmu, &state);
	if (ret != SF_SPMI_OK) {
		LOG("the PMU's boot state could not be read (SPMI error %d); the PMU is not used", ret);
		return;
	}
	gPmuUsable = true;
	if (gPmuMode == 2) {
		ret = sf_pmu_kernel_started(&gPmu, &state);
		struct sf_pmu_state after = state;
		int again = sf_pmu_read_state(&gPmu, &after);
		LOG("PMU logged %u boot error(s) and %u panic(s); boot_stage 0x%02x boot_error_stage 0x%02x shutdown_flag %u "
		    "pm_setting 0x%02x; sfpmu=2: boot_stage set to 0x30%s (write result %d; now boot_stage 0x%02x, %u and %u, "
		    "read result %d)",
		    state.boot_error_count, state.panic_count, state.boot_stage, state.boot_error_stage, state.shutdown_flag,
		    state.pm_setting, (state.boot_error_count || state.panic_count) ? ", both counts cleared" : "", ret,
		    after.boot_stage, after.boot_error_count, after.panic_count, again);
	} else {
		LOG("PMU logged %u boot error(s) and %u panic(s); boot_stage 0x%02x boot_error_stage 0x%02x shutdown_flag %u "
		    "pm_setting 0x%02x; sfpmu=1: nothing written",
		    state.boot_error_count, state.panic_count, state.boot_stage, state.boot_error_stage, state.shutdown_flag,
		    state.pm_setting);
	}
}

void
SFAppleRebootSmcBegin(SFSmcSession *session)
{
	if (gSmcLock == NULL)
		return;
	IOLockLock(gSmcLock);
	gThermalSmc = session;
}

static uint64_t
uptime_ns(void)
{
	uint64_t abs, ns;

	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns;
}

/* macsmc_rtc_get_time(), under the SMC bracket's lock (which also keeps the halt path's
 * SPMI use away). Reads only. */
static void
rtc_read(SFSmcSession *smc)
{
	uint8_t ctr[8] = { 0 }, off[8] = { 0 };
	int64_t seconds = 0;
	uint32_t micros = 0, result = 0;
	uint64_t at;
	int ret;

	gRtcTries++;
	if (smc->readKey(SF_SMC_KEY('C', 'L', 'K', 'M'), ctr, SF_RTC_BYTES, &result) != kIOReturnSuccess) {
		gRtcResult = 200;
		if (gRtcTries == RTC_MAX_TRIES)
			LOG("clock: SMC key CLKM could not be read (SMC result 0x%x); no time from the hardware clock", result);
		return;
	}
	at = uptime_ns();
	ret = sf_spmi_ext_readl(gPmu.io, gPmu.sid, SF_T8103_RTC_OFFSET, off, SF_RTC_BYTES);
	if (ret != SF_SPMI_OK) {
		gRtcResult = 100 - ret;
		if (gRtcTries == RTC_MAX_TRIES)
			LOG("clock: the PMU's clock offset could not be read (SPMI error %d); no time from the hardware clock", ret);
		return;
	}
	gRtcResult = sf_rtc_time(ctr, off, &seconds, &micros);
	gRtcTries = RTC_MAX_TRIES;      /* an answer, good or not, is final */
	if (gRtcResult != SF_RTC_OK) {
		LOG("clock: %s; no time from the hardware clock", gRtcResult == SF_RTC_NO_OFFSET ?
		    "no clock offset is stored in the PMU" : "the counter and the stored offset give a time before 2024 or after 2099");
		return;
	}
	gRtcBaseUptimeNs = at;
	gRtcBaseUsec = seconds * 1000000LL + micros;
	LOG("clock: %lld s since 1970 UTC at %llu ms of uptime (SMC CLKM and the PMU's offset, both read; nothing written)",
	    (long long)seconds, (unsigned long long)(at / 1000000));
}

void
SFAppleRebootSmcEnd(void)
{
	if (gSmcLock == NULL)
		return;
	if (gRtcMode != 0 && gPmuUsable && gRtcTries < RTC_MAX_TRIES && gThermalSmc != NULL && !gThermalSmc->failed())
		rtc_read(gThermalSmc);
	IOLockUnlock(gSmcLock);
}

static bool
smc_write(SFSmcSession *smc, UInt32 key, const void *value, UInt32 length, const char *what)
{
	UInt32 result = 0;
	IOReturn ret;
	int verdict = sf_smc_halt_gate(key, value, length, gPowerOffAllowed != 0);

	/* the only SMC write of this file, behind its gate */
	if (verdict != SF_SMC_HALT_OK) {
		gSmcHaltRefused++;
		gSmcHaltLastRefusal = (uint32_t)verdict;
		LOG("%s refused by the SMC write gate (%d); nothing sent", what, verdict);
		return false;
	}
	gSmcHaltSent++;
	ret = smc->writeKey(key, value, length, &result);
	if (ret != kIOReturnSuccess)
		LOG("failed to issue %s (0x%08x, SMC result 0x%x)", what, ret, result);
	return ret == kIOReturnSuccess;
}

static int
sf_apple_halt_restart(unsigned int type)
{
	bool off = type == kPEHaltCPU;
	bool locked = false;
	SFSmcSession *smc;
	UInt32 value;
	UInt8 flag = 0;

	if (type != kPERestartCPU && type != kPEHaltCPU)
		return gPreviousHaltRestart != NULL ? gPreviousHaltRestart(type) : -1;
	if (off && !gPowerOffAllowed) {
		LOG("power-off requested and not issued: boot with sfpoweroff=1 to allow it");
		return -1;
	}

	/* The thermal thread is inside a poll for some tens of milliseconds a second; with an
	 * SMC that does not answer, for seconds. */
	for (int i = 0; i < 1000 && !(locked = IOLockTryLock(gSmcLock)); i++)
		IOSleep(10);
	if (!locked) {
		LOG("the SMC session was not given up in 10 s; %s not issued", off ? "power-off" : "restart");
		return -1;
	}
	/* never unlocked: the SMC is this thread's from here on */
	smc = gThermalSmc;
	if (smc == NULL)
		smc = new SFSmcSession;
	if (smc != NULL && smc->failed()) {
		smc->close();
		IOReturn ret = smc->open();
		if (ret != kIOReturnSuccess) {
			LOG("the SMC session could not be opened (0x%08x); %s not issued", ret, off ? "power-off" : "restart");
			return -1;
		}
	}
	if (smc == NULL)
		return -1;

	/* macsmc_reboot_notify() */
	value = off ? SF_SMC_KEY('o', 'f', 'f', 'w') : SF_SMC_KEY('r', 'e', 's', 't');
	LOG("preparing for %s (MBSE = %s)", off ? "power-off" : "restart", off ? "offw" : "rest");
	smc_write(smc, SF_SMC_KEY('M', 'B', 'S', 'E'), &value, sizeof(value), off ? "MBSE = offw" : "MBSE = rest");
	if (gPmuUsable && gPmuMode == 2) {
		int ret = sf_pmu_prepare_halt(&gPmu, off);
		if (ret != SF_SPMI_OK)
			LOG("failed to write boot_stage and shutdown_flag (SPMI error %d)", ret);
	}

	/* apple_smc_enter_atomic(): failure ignored, as there */
	smc_write(smc, SF_SMC_KEY('N', 'T', 'A', 'P'), &flag, sizeof(flag), "NTAP = 0");

	/* macsmc_restart() / macsmc_power_off() */
	value = off ? SF_SMC_KEY('o', 'f', 'f', '1') : SF_SMC_KEY('p', 'h', 'r', 'a');
	LOG("issuing %s", off ? "power off (off1)" : "restart (phra)");
	if (smc_write(smc, SF_SMC_KEY('M', 'B', 'S', 'E'), &value, sizeof(value), off ? "MBSE = off1" : "MBSE = phra")) {
		/* Linux waits 100 ms; the console is slower here */
		for (int i = 0; i < 20; i++)
			IODelay(100000);
		LOG("unable to %s the system", off ? "power off" : "restart");
	}
	return -1;
}

/* kern.sf_pmu_writes: the write gate's level and counters (sf_pmu_scratch.h). */
static int
sf_pmu_writes_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char text[112];

	snprintf(text, sizeof(text), "sfpmu=%u level=%u usable=%u sent=%u refused=%u last_refusal=0x%x",
	    gPmuMode, gPmu.level, gPmuUsable ? 1 : 0, gPmu.writes_sent, gPmu.writes_refused, gPmu.last_refusal);
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

/* kern.sf_smc_halt_writes: the SMC write gate of the restart / power-off path. */
static int
sf_smc_halt_writes_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char text[96];

	snprintf(text, sizeof(text), "poweroff_allowed=%u sent=%u refused=%u last_refusal=%u",
	    gPowerOffAllowed ? 1 : 0, gSmcHaltSent, gSmcHaltRefused, gSmcHaltLastRefusal);
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

static int64_t
rtc_now_usec(void)
{
	int64_t base = gRtcBaseUsec;

	return base == 0 ? 0 : base + (int64_t)((uptime_ns() - gRtcBaseUptimeNs) / 1000);
}

/* kern.sf_rtc_usec: microseconds since 1970 UTC by the hardware clock, 0 while unknown. */
static int
sf_rtc_usec_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	int64_t now = rtc_now_usec();

	return SYSCTL_OUT(req, &now, sizeof(now));
}

static int
sf_rtc_sysctl SYSCTL_HANDLER_ARGS
{
#pragma unused(oidp, arg1, arg2)
	char text[160];
	int64_t now = rtc_now_usec();

	snprintf(text, sizeof(text), "sfrtc=%u result=%d tries=%u utc=%lld read_at_uptime_ms=%llu source=smc-clkm+pmu-offset writes=none",
	    gRtcMode, gRtcResult, gRtcTries, (long long)(now / 1000000), (unsigned long long)(gRtcBaseUptimeNs / 1000000));
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_PROC(_kern, OID_AUTO, sf_smc_halt_writes, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0,
    sf_smc_halt_writes_sysctl, "A", "restart and power-off: SMC writes sent and refused by the gate");
SYSCTL_PROC(_kern, OID_AUTO, sf_rtc_usec, CTLTYPE_QUAD | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0,
    sf_rtc_usec_sysctl, "Q", "microseconds since 1970 UTC by the hardware clock (0: unknown)");
SYSCTL_PROC(_kern, OID_AUTO, sf_rtc, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0,
    sf_rtc_sysctl, "A", "the hardware clock as read at boot (SMC counter and the PMU's offset; never written)");
SYSCTL_PROC(_kern, OID_AUTO, sf_pmu_writes, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, 0,
    sf_pmu_writes_sysctl, "A", "PMU boot-state bytes: SPMI writes sent and refused by the gate");

void
SFAppleRebootStart(void)
{
	uint32_t hook = 1;

	sysctl_register_oid(&sysctl__kern_sf_pmu_writes);
	sysctl_register_oid(&sysctl__kern_sf_smc_halt_writes);
	sysctl_register_oid(&sysctl__kern_sf_rtc_usec);
	sysctl_register_oid(&sysctl__kern_sf_rtc);
	PE_parse_boot_argn("sfrtc", &gRtcMode, sizeof(gRtcMode));

	PE_parse_boot_argn("sfpmu", &gPmuMode, sizeof(gPmuMode));
	PE_parse_boot_argn("sfreboot", &hook, sizeof(hook));
	PE_parse_boot_argn("sfpoweroff", &gPowerOffAllowed, sizeof(gPowerOffAllowed));
	pmu_start();
	if (hook == 0) {
		LOG("sfreboot=0: restart and power-off are left to the platform expert");
		return;
	}
	gSmcLock = IOLockAlloc();
	if (gSmcLock == NULL)
		return;
	gPreviousHaltRestart = PE_halt_restart;
	PE_halt_restart = sf_apple_halt_restart;
	LOG("handling restart%s requests via SMC", gPowerOffAllowed ? " and power-off" : " (power-off: sfpoweroff=1)");
}
