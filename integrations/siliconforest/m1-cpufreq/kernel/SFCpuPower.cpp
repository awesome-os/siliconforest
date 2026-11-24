// SPDX-License-Identifier: GPL-2.0-only
/*
 * SFCpuPower: CPU cluster performance states that follow load, under a die-temperature
 * limit, for Apple T8103 (M1) running SiliconForest.
 *
 * - The performance-state request (sf_cluster_request and the register layout) is ported
 *   from the pinned Linux drivers/cpufreq/apple-soc-cpufreq.c (apple_soc_cpufreq_set_pstate,
 *   apple_soc_cpufreq_write, apple_soc_cpufreq_get_rate, soc_t8103_info), GPL-2.0-only,
 *   Copyright The Asahi Linux Contributors. The cluster register addresses are those of
 *   the pinned device tree (t8103.dtsi, cpufreq_e / cpufreq_p) and of m1n1 src/cpufreq.c
 *   (t8103_clusters, base + CLUSTER_PSTATE).
 * - The sensors are SMC keys, read as the pinned Linux drivers/hwmon/macsmc-hwmon.c reads
 *   them ("flt " and "ioft" values, the fan keys F0Ac/F0Tg/F0Mn/F0Mx/F0Md of
 *   hwmon-fan.dtsi). Linux has no SoC die keys for this machine (its list is for M3
 *   boards), so the die sensors are found by enumerating the SMC's key table: see
 *   thermalEnumerate().
 * - The policies are in ../src/sf_cpufreq_policy.c and ../../m1-thermal/src/
 *   sf_thermal_policy.c, which the host test covers.
 *
 * The cluster setup (APSC, voltage tables) is the boot loader's: m1n1 cpufreq_init(),
 * which the lab's launcher runs before the guest. This driver only requests states.
 *
 * Boot arguments (all optional):
 *   sfcpufreq=0|1|2        0: never request a state unless the die is over its limit;
 *                          1 (default): follow load; 2: hold the highest allowed state
 *   sfcpufreq_period=MS    sample period of the load-following policy (default 100)
 *   sfthermal=0|1          0: no SMC access (the clusters then stay at the fallback
 *                          states in modes 1 and 2); 1 (default): poll the sensors
 *   sfthermal_limit=C      start lowering the caps at this die temperature (default 92)
 *   sfthermal_crit=C       cut both clusters to their lowest state (default 102)
 *   sfthermal_poll=MS      sensor poll period (default 1000)
 *   sfthermal_log=S        one log line every S seconds (default 60; 0: only changes)
 *   sfthermal_delay=S      earliest uptime of the first SMC access (default 20)
 *   sfthermal_keys=K,K,..  die sensor keys instead of the enumerated Tp/Te/Tg ones
 *   sfthermal_suffix=C     of the enumerated ones, only keys ending in this character
 *                          (a, b, x or z on J293, see thermalEnumerate)
 *   sfthermal_dump=0|1     log every temperature key once after enumeration (default 1)
 *   sffan=0|1              1 (default): set the fan speed from the die temperature (manual
 *                          mode, as macsmc-hwmon's fan_control); 0: the fan is the SMC's,
 *                          which on J293 leaves it off (boots th03, th05: 0 rpm through
 *                          four minutes at 88-92 C). Manual mode does not outlive the
 *                          session: the reset that returns the machine to Linux clears it
 *                          (boot thf01), and a shutdown or restart of this system gives
 *                          the fan back first (systemWillShutdown).
 *   sffan_start=C          die temperature at which the fan starts (default 70)
 *   sffan_full=C           die temperature at which it runs at its maximum (default 95)
 *   sfsmc_verbose=1        keep the SMC client's message log on after the first open
 * Everything that matters is also a sysctl (kern.sf_cpufreq.*, kern.sf_thermal.*), so
 * one boot can try several settings.
 */
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IORegistryEntry.h>
#include <IOKit/IOService.h>
#include <arm/cpu_topology.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <libkern/OSAtomic.h>
#include <libkern/OSByteOrder.h>
#include <libkern/c++/OSData.h>
#include <mach/mach_types.h>
#include <mach/machine.h>
#include <mach/processor_info.h>
#include <pexpert/pexpert.h>
#include <sys/errno.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/systm.h>

#include "SFSmcSession.h"
#include "sf_cpufreq_policy.h"
#include "sf_thermal_policy.h"

extern "C" {
/* osfmk/kern/processor.h */
processor_t cpu_to_processor(int cpu);
kern_return_t processor_info(processor_t processor, processor_flavor_t flavor, host_t *host,
    processor_info_t info, mach_msg_type_number_t *count);
}

#define LOG(fmt, ...) IOLog("SFCpuPower: " fmt "\n", ##__VA_ARGS__)

/* ---- drivers/cpufreq/apple-soc-cpufreq.c ---- */
#define APPLE_DVFS_CMD                  0x20
#define APPLE_DVFS_CMD_BUSY             (1ULL << 31)
#define APPLE_DVFS_CMD_SET              (1ULL << 25)
#define APPLE_DVFS_CMD_PS2              0x0000f000ULL   /* GENMASK(15, 12) */
#define APPLE_DVFS_CMD_PS2_SHIFT        12
#define APPLE_DVFS_CMD_PS1              0x0000001fULL   /* GENMASK(4, 0) */
#define APPLE_DVFS_STATUS               0x50
#define APPLE_DVFS_STATUS_CUR_PS_T8103  0x000000f0ULL   /* GENMASK(7, 4) */
#define APPLE_DVFS_STATUS_CUR_PS_SHIFT_T8103 4
#define APPLE_DVFS_TRANSITION_TIMEOUT   400             /* us */

/* t8103.dtsi cpufreq_e / cpufreq_p reg; m1n1 t8103_clusters base + 0x20000 */
#define T8103_CHIP_ID                   0x8103
#define T8103_ECLUSTER_DVFS             0x210e20000ULL
#define T8103_PCLUSTER_DVFS             0x211e20000ULL
#define CLUSTER_DVFS_LEN                0x1000

/* How long a requested state may take to show in the status register before the stress
 * test counts it as failed: far above the 55 us the device tree gives as the latency. */
#define STRESS_VERIFY_US                20000

#define MAX_CLUSTER_CPUS                8
#define MAX_STATES                      16
#define MAX_TEMP_KEYS                   160
#define MAX_DIE_KEYS                    64

enum { kModeHandsOff = 0, kModeFollowLoad = 1, kModeHoldTop = 2 };

struct Cluster {
	const struct sf_cluster_opps *opps;
	IOMemoryMap *map;
	volatile uint8_t *reg;
	struct sf_gov gov;
	unsigned cpu[MAX_CLUSTER_CPUS];
	unsigned ncpu;
	uint32_t idleTicks[MAX_CLUSTER_CPUS];
	bool haveIdle;
	unsigned utilPermille;
	unsigned userMax;               /* index; the user's ceiling (sysctl) */
	unsigned effectiveCap;          /* index; what the last sample allowed */
	uint64_t transitions;           /* requests written */
	uint64_t busyTimeouts;          /* requests not written: the cluster stayed busy */
	uint64_t mismatches;            /* samples that found another state than the one requested */
	uint32_t residency[MAX_STATES]; /* samples spent at each index */
	bool requested;                 /* a state has been requested by this driver */
};

struct TempKey {
	uint32_t key;
	uint32_t type;
	uint8_t  length;
};

static IOLock *gLock;                   /* clusters, thermal policy and the settings below */
static Cluster gE, gP;
static bool gHaveClusters;
static uint32_t gMode = kModeFollowLoad;
static uint32_t gPeriodMs = 100;
/*
 * up_permille 300: measured (boot th04, the 8 MB hashing loop on one worker): 592 MB/20 s
 * with 800, where the E cluster spent a quarter of the samples below its top state, 608
 * with 300, the same as holding the top states. Both drop to E1/P1 when idle.
 */
static struct sf_gov_tunables gTunables = { 300, 1250, 3 };

static struct sf_thermal gThermal;
static struct sf_thermal_config gThermalCfg = { 92000, 102000, 4000, 3 };
static uint32_t gThermalEnabled = 1;
static uint32_t gThermalPollMs = 1000;
static uint32_t gThermalLogS = 60;
static uint32_t gThermalDelayS = 20;
static uint32_t gThermalDump = 1;
static uint32_t gSmcKeepVerbose;
static char gThermalKeyArg[160];
static char gThermalSuffixArg[4];

/* written by the thermal thread, read by the sysctls (word-sized, no lock) */
static TempKey gTempKeys[MAX_TEMP_KEYS];
static unsigned gTempKeyCount;
static TempKey gDieKeys[MAX_DIE_KEYS];
static int32_t gDieMilli[MAX_DIE_KEYS];
static unsigned gDieKeyCount;
static int32_t gDieMaxMilli;
static uint32_t gDieMaxKey;
static uint32_t gDieValid;
/*
 * Set by the first sensor poll, whatever it found (at once when thermal is off). Until
 * then the clusters stay as the boot loader left them, which is how every boot before
 * this driver ran: the fallback states would only slow the boot down.
 */
static bool gArmed;
static uint32_t gSmcKeyTotal;
static uint64_t gThermalPolls, gThermalReadErrors, gSmcOpens, gSmcOpenFailures;
static uint64_t gPollLastUs, gPollMaxUs;        /* how long a sensor poll took */
static int32_t gFanRpm = -1, gFanTarget = -1, gFanMin = -1, gFanMax = -1, gFanMode = -1;
/* fan control: off unless asked for, see thermalFan() */
static uint32_t gFanControl = 1;
static volatile uint32_t gShutdown;     /* 1: asked to give the fan back and stop; 2: done */
static struct sf_fan_config gFanCfg = { 70000, 95000 };
static int32_t gFanAsked;               /* rpm last written to F0Tg; 0: none, mode left to the SMC */
static bool gFanManual;                 /* F0Md was written 1 by this driver */
static uint64_t gFanWrites, gFanWriteErrors;
static uint32_t gDumpRequest;
static int32_t gSystemPowerMilliwatt = -1;
/* test hooks (sysctl, root): added to every die reading / make every poll fail */
static int32_t gTestOffsetC;
static uint32_t gTestFail;

static uint64_t gStressDone, gStressFailures, gStressMaxUs;

/* ------------------------------------------------------------------------------------ */

static uint64_t
uptime_ms(void)
{
	uint64_t abs, ns;

	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000000ULL;
}

static uint64_t
uptime_us(void)
{
	uint64_t abs, ns;

	clock_get_uptime(&abs);
	absolutetime_to_nanoseconds(abs, &ns);
	return ns / 1000ULL;
}

static inline uint64_t
reg_read64(const Cluster *c, unsigned off)
{
	return *(volatile uint64_t *)(c->reg + off);
}

static inline void
reg_write64(const Cluster *c, unsigned off, uint64_t value)
{
	*(volatile uint64_t *)(c->reg + off) = value;
	OSSynchronizeIO();
}

/* apple_soc_cpufreq_get_rate(): the state the cluster is in, as its hardware number */
static unsigned
sf_cluster_current(const Cluster *c)
{
	return (unsigned)((reg_read64(c, APPLE_DVFS_STATUS) & APPLE_DVFS_STATUS_CUR_PS_T8103) >>
	       APPLE_DVFS_STATUS_CUR_PS_SHIFT_T8103);
}

/*
 * apple_soc_cpufreq_set_pstate() + apple_soc_cpufreq_write() for soc_t8103_info (has_ps2,
 * no verify_transition): wait for the previous request to be taken, then write PS1, PS2
 * and SET. Like Linux this does not wait for the new state; the next request does.
 */
static bool
sf_cluster_request(Cluster *c, unsigned index)
{
	unsigned pstate = c->opps->opp[index].pstate;
	uint64_t reg;
	unsigned waited = 0;

	for (;;) {
		reg = reg_read64(c, APPLE_DVFS_CMD);
		if (!(reg & APPLE_DVFS_CMD_BUSY))
			break;
		if (waited >= APPLE_DVFS_TRANSITION_TIMEOUT) {
			c->busyTimeouts++;
			return false;
		}
		IODelay(2);
		waited += 2;
	}
	reg &= ~APPLE_DVFS_CMD_PS1;
	reg |= pstate;
	reg &= ~APPLE_DVFS_CMD_PS2;
	reg |= (uint64_t)pstate << APPLE_DVFS_CMD_PS2_SHIFT;
	reg |= APPLE_DVFS_CMD_SET;
	reg_write64(c, APPLE_DVFS_CMD, reg);
	c->transitions++;
	c->requested = true;
	return true;
}

/* Utilisation of the cluster's busiest processor since the last call, from idle time. */
static unsigned
sf_cluster_utilisation(Cluster *c, uint64_t elapsedMs)
{
	unsigned busiest = 0;
	/* processor_cpu_load_info() counts in 10 ms ticks */
	uint64_t ticks = elapsedMs / 10;

	for (unsigned i = 0; i < c->ncpu; i++) {
		processor_cpu_load_info_data_t load;
		mach_msg_type_number_t count = PROCESSOR_CPU_LOAD_INFO_COUNT;
		host_t host = HOST_NULL;
		processor_t processor = cpu_to_processor((int)c->cpu[i]);
		uint32_t idle, delta;
		unsigned util;

		if (processor == PROCESSOR_NULL ||
		    processor_info(processor, PROCESSOR_CPU_LOAD_INFO, &host, (processor_info_t)&load, &count) !=
		    KERN_SUCCESS)
			continue;
		idle = load.cpu_ticks[CPU_STATE_IDLE];
		delta = idle - c->idleTicks[i];
		c->idleTicks[i] = idle;
		if (!c->haveIdle || ticks == 0)
			continue;
		util = delta >= ticks ? 0 : (unsigned)((ticks - delta) * 1000 / ticks);
		if (util > busiest)
			busiest = util;
	}
	c->haveIdle = true;
	return busiest;
}

/* One sample of one cluster. Called with gLock held. */
static void
sf_cluster_sample(Cluster *c, unsigned thermalCap, uint64_t elapsedMs)
{
	unsigned top = sf_opp_top(c->opps);
	unsigned cap = thermalCap < c->userMax ? thermalCap : c->userMax;
	int hw = sf_opp_index(c->opps, sf_cluster_current(c));
	unsigned want;

	if (cap > top)
		cap = top;
	c->effectiveCap = cap;
	c->utilPermille = sf_cluster_utilisation(c, elapsedMs);

	if (hw >= 0 && (unsigned)hw < MAX_STATES)
		c->residency[hw]++;
	if (c->requested && hw != (int)c->gov.index)
		c->mismatches++;

	if (!gArmed) {
		if (hw >= 0)
			c->gov.index = (unsigned)hw;
		return;
	}
	switch (gMode) {
	case kModeFollowLoad:
		want = sf_gov_sample(c->opps, &gTunables, &c->gov, c->utilPermille, cap);
		break;
	case kModeHoldTop:
		want = c->gov.index = cap;
		c->gov.lower_votes = 0;
		break;
	default:
		/* hands off: only the die-temperature limit may lower the state */
		if (hw < 0)
			return;
		if (!c->requested)
			c->gov.index = (unsigned)hw;
		if ((gThermal.status != SF_THERMAL_CAPPED && gThermal.status != SF_THERMAL_CRITICAL) ||
		    c->gov.index <= cap)
			return;
		want = c->gov.index = cap;
		break;
	}
	if (hw != (int)want)
		sf_cluster_request(c, want);
}

static void
sf_governor_thread(void *, wait_result_t)
{
	uint64_t last = uptime_ms();

	for (;;) {
		uint32_t period = gPeriodMs;

		if (period < 10)
			period = 10;
		IOSleep(period);
		uint64_t now = uptime_ms();

		IOLockLock(gLock);
		sf_cluster_sample(&gE, gThermal.cap_e, now - last);
		sf_cluster_sample(&gP, gThermal.cap_p, now - last);
		IOLockUnlock(gLock);
		last = now;
	}
}

/*
 * N requests on each cluster, alternating between the lowest and the highest state the
 * caps allow, each one checked against the status register. Called with gLock held (the
 * policy thread waits meanwhile), from the stress sysctl.
 */
static void
sf_stress(uint32_t count)
{
	Cluster *clusters[2] = { &gE, &gP };
	uint64_t failures = 0, maxUs = 0, done = 0;

	for (uint32_t n = 0; n < count; n++) {
		for (unsigned k = 0; k < 2; k++) {
			Cluster *c = clusters[k];
			unsigned cap = c->effectiveCap;
			/* walk the whole table as well as the big jump: 0, cap, 1, cap - 1, ... */
			unsigned span = cap + 1;
			unsigned step = (n / 2) % span;
			unsigned index = (n & 1) ? cap - step : step;
			uint64_t start = uptime_us(), now = start;
			bool ok = sf_cluster_request(c, index);

			while (ok) {
				uint64_t cmd = reg_read64(c, APPLE_DVFS_CMD);

				now = uptime_us();
				if (!(cmd & APPLE_DVFS_CMD_BUSY) &&
				    sf_cluster_current(c) == c->opps->opp[index].pstate)
					break;
				if (now - start > STRESS_VERIFY_US) {
					ok = false;
					break;
				}
				IODelay(2);
			}
			if (!ok) {
				if (failures++ < 5)
					LOG("stress: %s request %u for state %u failed: command 0x%llx status 0x%llx",
					    c->opps->name, n, c->opps->opp[index].pstate,
					    reg_read64(c, APPLE_DVFS_CMD), reg_read64(c, APPLE_DVFS_STATUS));
			} else if (now - start > maxUs) {
				maxUs = now - start;
			}
			c->gov.index = index;
			c->gov.lower_votes = 0;
			done++;
		}
	}
	gStressDone = done;
	gStressFailures = failures;
	gStressMaxUs = maxUs;
	LOG("stress: %llu transitions, %llu failed, slowest %llu us", done, failures, maxUs);
}

/* ---- sensors ------------------------------------------------------------------------ */

static void
key_name(uint32_t key, char out[5])
{
	out[0] = (char)(key >> 24);
	out[1] = (char)(key >> 16);
	out[2] = (char)(key >> 8);
	out[3] = (char)key;
	out[4] = 0;
	for (int i = 0; i < 4; i++) {
		if (out[i] < 0x20 || out[i] > 0x7e)
			out[i] = '?';
	}
}

/*
 * A sensor value in thousandths; false when unreadable or not a number. *silent
 * (optional) is set when the SMC did not answer at all, as opposed to answering with an
 * error for this key: every further read would then wait out its timeout as well.
 */
static bool
smc_read_milli(SFSmcSession *smc, const TempKey *k, int32_t *milli, bool *silent = NULL)
{
	IOReturn ret;

	if (k->type == SF_SMC_KEY('f', 'l', 't', ' ') && k->length == 4) {
		uint32_t raw = 0;

		ret = smc->readKey(k->key, &raw, 4, NULL);
		if (ret == kIOReturnSuccess)
			return sf_smc_flt_to_milli(OSSwapLittleToHostInt32(raw), milli) == 0;
	} else if (k->type == SF_SMC_KEY('i', 'o', 'f', 't') && k->length == 8) {
		uint64_t raw = 0;

		ret = smc->readKey(k->key, &raw, 8, NULL);
		if (ret == kIOReturnSuccess)
			return sf_smc_ioft_to_milli(OSSwapLittleToHostInt64(raw), milli) == 0;
	} else {
		return false;
	}
	if (silent != NULL && ret != kIOReturnDeviceError)
		*silent = true;
	return false;
}

/* A die temperature between 1 and 150 C is a reading; anything else is a sensor that is off. */
static bool
plausible_die(int32_t milli)
{
	return milli >= 1000 && milli <= 150000;
}

/*
 * Walk the SMC's key table ("#KEY" entries, m1n1 get_key_by_index) and keep every
 * temperature key (first letter T, a "flt " or "ioft" value). The die sensors are the
 * keys named in sfthermal_keys, or else those starting Tp, Te or Tg (CPU performance
 * and efficiency cores, GPU: one die) that read a plausible temperature now.
 *
 * On J293 (boots th02, th03) that is 36 keys: Te0, Te3 and Tp2..Tp9 (no Tp6), each as
 * four keys ending a, b, x and z. x follows load within a second, a lags it; b reads a
 * fixed 9 to 10 C above a, and z a fixed 12 to 20 C above x, a different amount per
 * sensor (Tp4: 17.0, Tp3: 12.0, Te3: 20.0). What the offsets stand for is not known here;
 * z is the highest, and the SMC's own TCMz equals the hottest of them. The limit is
 * applied to the hottest of all 36, so to a z value: the cautious reading.
 */
static bool
thermalEnumerate(SFSmcSession *smc)
{
	uint32_t raw = 0, count;
	unsigned temps = 0, die = 0;

	if (smc->readKey(SF_SMC_KEY('#', 'K', 'E', 'Y'), &raw, 4, NULL) != kIOReturnSuccess) {
		LOG("thermal: the SMC key count (#KEY) cannot be read");
		return false;
	}
	/* a big-endian count in the key's four bytes (Linux macsmc.c: be32_to_cpu) */
	count = OSSwapBigToHostInt32(raw);
	if (count == 0 || count > 8192) {
		LOG("thermal: implausible SMC key count %u", count);
		return false;
	}
	gSmcKeyTotal = count;
	for (uint32_t i = 0; i < count && temps < MAX_TEMP_KEYS; i++) {
		TempKey k = { 0, 0, 0 };

		if (smc->keyByIndex(i, &k.key) != kIOReturnSuccess) {
			if (smc->failed())
				return false;
			continue;
		}
		if ((k.key >> 24) != 'T')
			continue;
		if (smc->keyInfo(k.key, &k.length, &k.type, NULL) != kIOReturnSuccess)
			continue;
		if (!((k.type == SF_SMC_KEY('f', 'l', 't', ' ') && k.length == 4) ||
		    (k.type == SF_SMC_KEY('i', 'o', 'f', 't') && k.length == 8)))
			continue;
		gTempKeys[temps++] = k;
	}
	gTempKeyCount = temps;

	char line[160];
	size_t used = 0;

	line[0] = 0;
	for (unsigned i = 0; i < temps; i++) {
		int32_t milli = 0;
		char name[5];
		bool ok = smc_read_milli(smc, &gTempKeys[i], &milli);
		bool wanted;

		key_name(gTempKeys[i].key, name);
		if (gThermalKeyArg[0])
			wanted = strnstr(gThermalKeyArg, name, sizeof(gThermalKeyArg)) != NULL;
		else
			wanted = (name[1] == 'p' || name[1] == 'e' || name[1] == 'g') &&
			    (gThermalSuffixArg[0] == 0 || name[3] == gThermalSuffixArg[0]);
		if (wanted && die < MAX_DIE_KEYS && (gThermalKeyArg[0] || (ok && plausible_die(milli))))
			gDieKeys[die++] = gTempKeys[i];
		if (gThermalDump) {
			if (ok)
				used += (size_t)snprintf(line + used, sizeof(line) - used, " %s=%d.%d", name,
				    milli / 1000, (milli < 0 ? -milli : milli) % 1000 / 100);
			else
				used += (size_t)snprintf(line + used, sizeof(line) - used, " %s=?", name);
			if (used > sizeof(line) - 16 || i + 1 == temps) {
				LOG("thermal: keys:%s", line);
				used = 0;
				line[0] = 0;
			}
		}
	}
	gDieKeyCount = die;
	LOG("thermal: %u SMC keys, %u temperature keys, %u die sensors (%s)", count, temps, die,
	    gThermalKeyArg[0] ? "sfthermal_keys" : "Tp/Te/Tg keys with a reading");
	return true;
}

/*
 * The fan keys of hwmon-fan.dtsi and "Total System Power" of hwmon-common.dtsi. Their
 * types are asked once; speed, target and mode are then read at every poll, the limits
 * with every log line.
 */
static TempKey gKeyFanNow = { SF_SMC_KEY('F', '0', 'A', 'c'), 0, 0 };
static TempKey gKeyFanTarget = { SF_SMC_KEY('F', '0', 'T', 'g'), 0, 0 };
static TempKey gKeyFanMin = { SF_SMC_KEY('F', '0', 'M', 'n'), 0, 0 };
static TempKey gKeyFanMax = { SF_SMC_KEY('F', '0', 'M', 'x'), 0, 0 };
static TempKey gKeyPower = { SF_SMC_KEY('P', 'S', 'T', 'R'), 0, 0 };

static int32_t
smc_read_cached(SFSmcSession *smc, TempKey *k, int32_t divisor)
{
	int32_t milli;

	if (k->length == 0 && smc->keyInfo(k->key, &k->length, &k->type, NULL) != kIOReturnSuccess) {
		k->length = 0;
		return -1;
	}
	return smc_read_milli(smc, k, &milli) ? milli / divisor : -1;
}

static void
thermalReadFanNow(SFSmcSession *smc)
{
	uint8_t mode = 0;

	gFanRpm = smc_read_cached(smc, &gKeyFanNow, 1000);
	gFanTarget = smc_read_cached(smc, &gKeyFanTarget, 1000);
	gFanMode = smc->readKey(SF_SMC_KEY('F', '0', 'M', 'd'), &mode, 1, NULL) == kIOReturnSuccess ? mode : -1;
	gSystemPowerMilliwatt = smc_read_cached(smc, &gKeyPower, 1);
}

static void
thermalReadFan(SFSmcSession *smc)
{
	thermalReadFanNow(smc);
	gFanMin = smc_read_cached(smc, &gKeyFanMin, 1000);
	gFanMax = smc_read_cached(smc, &gKeyFanMax, 1000);
}

/* Log every temperature key with its present value (sfthermal_dump, kern.sf_thermal.dump). */
static void
thermalDump(SFSmcSession *smc)
{
	char line[160];
	size_t used = 0;

	line[0] = 0;
	for (unsigned i = 0; i < gTempKeyCount; i++) {
		int32_t milli = 0;
		char name[5];

		key_name(gTempKeys[i].key, name);
		if (smc_read_milli(smc, &gTempKeys[i], &milli))
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %s=%d.%d", name,
			    milli / 1000, (milli < 0 ? -milli : milli) % 1000 / 100);
		else
			used += (size_t)snprintf(line + used, sizeof(line) - used, " %s=?", name);
		if (used > sizeof(line) - 16 || i + 1 == gTempKeyCount) {
			LOG("thermal: keys:%s", line);
			used = 0;
			line[0] = 0;
		}
	}
}

/*
 * Fan control, for when the SMC does not run the fan by itself. macsmc_hwmon_write_fan()
 * of the pinned Linux macsmc-hwmon.c: the SMC does not check a requested speed, so it is
 * kept between F0Mn and F0Mx; F0Md = 1 takes the fan over, F0Md = 0 gives it back.
 */
static void
thermalFan(SFSmcSession *smc, bool valid, int32_t dieMilli, enum sf_thermal_status status)
{
	int32_t want = 0;
	uint8_t mode;
	uint32_t bits;

	if (gFanControl) {
		if (!valid || status == SF_THERMAL_CRITICAL)
			want = gFanMax;         /* nothing vouches for the die: all the air there is */
		else
			want = sf_fan_target(&gFanCfg, dieMilli, gFanMin, gFanMax, gFanAsked);
		if (gFanMin <= 0 || gFanMax <= gFanMin)
			want = 0;               /* limits unknown: never a target */
	}
	/* a running fan is not retargeted for less than 300 rpm (the die reading moves a
	 * degree or two from poll to poll), except to its limits */
	if (want != 0 && gFanAsked != 0 && want != gFanMax && want != gFanMin &&
	    want - gFanAsked < 300 && gFanAsked - want < 300)
		want = gFanAsked;
	if (want == gFanAsked && (want != 0) == gFanManual)
		return;

	if (want == 0) {
		mode = 0;
		if (smc->writeKey(SF_SMC_KEY('F', '0', 'M', 'd'), &mode, 1, NULL) != kIOReturnSuccess) {
			gFanWriteErrors++;
			return;
		}
		gFanWrites++;
		gFanManual = false;
		gFanAsked = 0;
		LOG("thermal: fan given back to the SMC (F0Md = 0)");
		return;
	}
	if (!gFanManual) {
		mode = 1;
		if (smc->writeKey(SF_SMC_KEY('F', '0', 'M', 'd'), &mode, 1, NULL) != kIOReturnSuccess) {
			gFanWriteErrors++;
			return;
		}
		gFanWrites++;
		gFanManual = true;
		LOG("thermal: fan taken over (F0Md = 1), first target %d rpm at %d.%d C", want,
		    dieMilli / 1000, dieMilli % 1000 / 100);
	}
	bits = OSSwapHostToLittleInt32(sf_smc_int_to_flt(want));
	if (smc->writeKey(SF_SMC_KEY('F', '0', 'T', 'g'), &bits, 4, NULL) != kIOReturnSuccess) {
		gFanWriteErrors++;
		return;
	}
	gFanWrites++;
	gFanAsked = want;
}

static void
thermalLog(const char *why)
{
	char name[5];
	unsigned e, p, ce, cp, ue, up;
	enum sf_thermal_status status;

	IOLockLock(gLock);
	e = gHaveClusters ? sf_cluster_current(&gE) : 0;
	p = gHaveClusters ? sf_cluster_current(&gP) : 0;
	ce = sf_t8103_ecluster.opp[gThermal.cap_e].pstate;
	cp = sf_t8103_pcluster.opp[gThermal.cap_p].pstate;
	ue = gE.utilPermille;
	up = gP.utilPermille;
	status = gThermal.status;
	IOLockUnlock(gLock);

	key_name(gDieMaxKey, name);
	if (gDieValid)
		LOG("thermal: %s: die %d.%d C (%s, %u sensors) limit %d crit %d status %s caps E%u/P%u | "
		    "pstate E%u/P%u util E %u%% P %u%% | fan %d rpm (target %d, min %d, max %d, mode %d) "
		    "system %d mW | uptime %llu s",
		    why, gDieMaxMilli / 1000, gDieMaxMilli % 1000 / 100, name, gDieKeyCount,
		    gThermalCfg.limit_mc / 1000, gThermalCfg.critical_mc / 1000, sf_thermal_status_name(status),
		    ce, cp, e, p, ue / 10, up / 10, gFanRpm, gFanTarget, gFanMin, gFanMax, gFanMode,
		    gSystemPowerMilliwatt, uptime_ms() / 1000);
	else
		LOG("thermal: %s: NO die temperature (%u sensors, %llu read errors) status %s caps E%u/P%u | "
		    "pstate E%u/P%u | fan %d rpm | uptime %llu s",
		    why, gDieKeyCount, gThermalReadErrors, sf_thermal_status_name(status), ce, cp, e, p, gFanRpm,
		    uptime_ms() / 1000);
}

static void
sf_thermal_thread(void *, wait_result_t)
{
	SFSmcSession *smc = new SFSmcSession;
	bool enumerated = false;
	unsigned silentPolls = 0;
	uint64_t lastLog = 0, nextOpen = 0;
	enum sf_thermal_status lastStatus = SF_THERMAL_NO_SENSOR;

	/*
	 * The PCIe bridge powers the radio in its own SMC session while it starts, and
	 * quiesces the SMC when it is done: about half a second, before this kext is even
	 * started (boots th02..th05, where that session ended 30 s before the first access
	 * here). There is no lock shared with it, so stay out of the mailbox until the
	 * earliest uptime, which leaves it that margin on a faster boot as well.
	 */
	while (uptime_ms() < (uint64_t)gThermalDelayS * 1000)
		IOSleep(200);
	LOG("thermal: first SMC access at %llu s of uptime", uptime_ms() / 1000);

	for (;;) {
		bool valid = false;
		int32_t hottest = 0;
		uint32_t hottestKey = 0;
		uint64_t now = uptime_ms();
		uint64_t pollStart = uptime_us();

		if (smc == NULL) {
			IOSleep(1000);
			continue;
		}
		if (smc->failed() && now >= nextOpen) {
			smc->close();
			gSFSmcVerbose = gSmcOpens == 0 || gSmcKeepVerbose;
			IOReturn ret = smc->open();
			gSFSmcVerbose = gSmcKeepVerbose != 0;
			if (ret != kIOReturnSuccess) {
				gSmcOpenFailures++;
				LOG("thermal: SMC session could not be opened (0x%08x); again in 10 s", ret);
				nextOpen = now + 10000;
			} else {
				gSmcOpens++;
				if (!enumerated)
					enumerated = thermalEnumerate(smc);
				thermalReadFan(smc);
				nextOpen = now + 10000;
			}
		} else if (!smc->failed() && !enumerated && now >= nextOpen) {
			/* the key table could not be walked: try again, the session is fine */
			enumerated = thermalEnumerate(smc);
			nextOpen = now + 10000;
		}

		if (!smc->failed() && enumerated && !gTestFail) {
			bool silent = false;

			for (unsigned i = 0; i < gDieKeyCount && !silent; i++) {
				int32_t milli = 0;

				if (!smc_read_milli(smc, &gDieKeys[i], &milli, &silent)) {
					gThermalReadErrors++;
					gDieMilli[i] = INT32_MIN;
					continue;
				}
				gDieMilli[i] = milli;
				if (!plausible_die(milli))
					continue;
				if (!valid || milli > hottest) {
					hottest = milli;
					hottestKey = gDieKeys[i].key;
				}
				valid = true;
			}
			if (valid)
				hottest += gTestOffsetC * 1000;
			if (!silent)
				thermalReadFanNow(smc);
			/* an SMC that has stopped answering: start a new session rather than wait
			 * out a timeout per key on every poll */
			silentPolls = silent ? silentPolls + 1 : 0;
			if (silentPolls >= 3) {
				LOG("thermal: the SMC has not answered for 3 polls; closing the session, again in 10 s");
				smc->close();
				silentPolls = 0;
				nextOpen = uptime_ms() + 10000;
			}
		}
		gThermalPolls++;
		gPollLastUs = uptime_us() - pollStart;
		if (gPollLastUs > gPollMaxUs && gThermalPolls > 2)
			gPollMaxUs = gPollLastUs;
		gDieValid = valid;
		if (valid) {
			gDieMaxMilli = hottest;
			gDieMaxKey = hottestKey;
		}

		IOLockLock(gLock);
		sf_thermal_poll(&gThermal, &gThermalCfg, &sf_t8103_ecluster, &sf_t8103_pcluster, valid, hottest);
		enum sf_thermal_status status = gThermal.status;
		gArmed = true;
		IOLockUnlock(gLock);

		if (gShutdown == 1) {
			/* the system is going down: the fan goes back to the SMC, nothing more is asked */
			gFanControl = 0;
			if (!smc->failed() && enumerated)
				thermalFan(smc, valid, hottest, status);
			gShutdown = 2;
			for (;;)
				IOSleep(1000);
		}
		if (!smc->failed() && enumerated) {
			thermalFan(smc, valid, hottest, status);
			if (gDumpRequest) {
				gDumpRequest = 0;
				thermalDump(smc);
			}
		}

		now = uptime_ms();
		if (status != lastStatus) {
			if (!smc->failed())
				thermalReadFan(smc);
			thermalLog(sf_thermal_status_name(status));
			lastStatus = status;
			lastLog = now;
		} else if (gThermalLogS && now - lastLog >= (uint64_t)gThermalLogS * 1000) {
			if (!smc->failed())
				thermalReadFan(smc);
			thermalLog("minute");
			lastLog = now;
		}
		IOSleep(gThermalPollMs < 100 ? 100 : gThermalPollMs);
	}
}

/* ---- sysctl ------------------------------------------------------------------------- */

enum {
	kCtlEPstate, kCtlPPstate, kCtlEKhz, kCtlPKhz, kCtlEUtil, kCtlPUtil, kCtlECap, kCtlPCap,
	kCtlEUserMax, kCtlPUserMax, kCtlMode, kCtlPeriod, kCtlTransitions, kCtlBusyTimeouts,
	kCtlMismatches, kCtlStress, kCtlStressFailures, kCtlStressMaxUs, kCtlResidencyReset,
	kCtlUpPermille, kCtlDownHold,
	kCtlDieMc, kCtlValid, kCtlLimitC, kCtlCritC, kCtlThCapE, kCtlThCapP, kCtlFanRpm, kCtlFanTarget,
	kCtlFanMin, kCtlFanMax, kCtlFanMode, kCtlPowerMw, kCtlPolls, kCtlReadErrors, kCtlSmcKeys,
	kCtlTempKeys, kCtlDieKeys, kCtlTestOffset, kCtlTestFail, kCtlSmcOpens,
	kCtlPollUs, kCtlPollMaxUs,
	kCtlFanControl, kCtlFanStartC, kCtlFanFullC, kCtlFanAsked, kCtlFanWrites, kCtlFanWriteErrors, kCtlDump,
};

static int
sf_sysctl_int(struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	int value = 0, error;
	Cluster *c = NULL;

	IOLockLock(gLock);
	switch (arg2) {
	case kCtlEPstate:       value = gHaveClusters ? (int)sf_cluster_current(&gE) : -1; break;
	case kCtlPPstate:       value = gHaveClusters ? (int)sf_cluster_current(&gP) : -1; break;
	case kCtlEKhz: {
		int i = gHaveClusters ? sf_opp_index(gE.opps, sf_cluster_current(&gE)) : -1;
		value = i >= 0 ? (int)gE.opps->opp[i].khz : 0;
		break;
	}
	case kCtlPKhz: {
		int i = gHaveClusters ? sf_opp_index(gP.opps, sf_cluster_current(&gP)) : -1;
		value = i >= 0 ? (int)gP.opps->opp[i].khz : 0;
		break;
	}
	case kCtlEUtil:         value = (int)gE.utilPermille; break;
	case kCtlPUtil:         value = (int)gP.utilPermille; break;
	case kCtlECap:          value = gHaveClusters ? gE.opps->opp[gE.effectiveCap].pstate : -1; break;
	case kCtlPCap:          value = gHaveClusters ? gP.opps->opp[gP.effectiveCap].pstate : -1; break;
	case kCtlEUserMax:      c = &gE; value = gHaveClusters ? gE.opps->opp[gE.userMax].pstate : -1; break;
	case kCtlPUserMax:      c = &gP; value = gHaveClusters ? gP.opps->opp[gP.userMax].pstate : -1; break;
	case kCtlMode:          value = (int)gMode; break;
	case kCtlPeriod:        value = (int)gPeriodMs; break;
	case kCtlTransitions:   value = (int)(gE.transitions + gP.transitions); break;
	case kCtlBusyTimeouts:  value = (int)(gE.busyTimeouts + gP.busyTimeouts); break;
	case kCtlMismatches:    value = (int)(gE.mismatches + gP.mismatches); break;
	case kCtlStress:        value = (int)gStressDone; break;
	case kCtlStressFailures: value = (int)gStressFailures; break;
	case kCtlStressMaxUs:   value = (int)gStressMaxUs; break;
	case kCtlResidencyReset: value = 0; break;
	case kCtlUpPermille:    value = (int)gTunables.up_permille; break;
	case kCtlDownHold:      value = (int)gTunables.down_hold; break;
	case kCtlDieMc:         value = gDieValid ? gDieMaxMilli : -1; break;
	case kCtlValid:         value = (int)gDieValid; break;
	case kCtlLimitC:        value = gThermalCfg.limit_mc / 1000; break;
	case kCtlCritC:         value = gThermalCfg.critical_mc / 1000; break;
	case kCtlThCapE:        value = sf_t8103_ecluster.opp[gThermal.cap_e].pstate; break;
	case kCtlThCapP:        value = sf_t8103_pcluster.opp[gThermal.cap_p].pstate; break;
	case kCtlFanRpm:        value = gFanRpm; break;
	case kCtlFanTarget:     value = gFanTarget; break;
	case kCtlFanMin:        value = gFanMin; break;
	case kCtlFanMax:        value = gFanMax; break;
	case kCtlFanMode:       value = gFanMode; break;
	case kCtlPowerMw:       value = gSystemPowerMilliwatt; break;
	case kCtlPolls:         value = (int)gThermalPolls; break;
	case kCtlReadErrors:    value = (int)gThermalReadErrors; break;
	case kCtlSmcKeys:       value = (int)gSmcKeyTotal; break;
	case kCtlTempKeys:      value = (int)gTempKeyCount; break;
	case kCtlDieKeys:       value = (int)gDieKeyCount; break;
	case kCtlTestOffset:    value = gTestOffsetC; break;
	case kCtlTestFail:      value = (int)gTestFail; break;
	case kCtlSmcOpens:      value = (int)gSmcOpens; break;
	case kCtlPollUs:        value = (int)gPollLastUs; break;
	case kCtlPollMaxUs:     value = (int)gPollMaxUs; break;
	case kCtlFanControl:    value = (int)gFanControl; break;
	case kCtlFanStartC:     value = gFanCfg.start_mc / 1000; break;
	case kCtlFanFullC:      value = gFanCfg.full_mc / 1000; break;
	case kCtlFanAsked:      value = gFanAsked; break;
	case kCtlFanWrites:     value = (int)gFanWrites; break;
	case kCtlFanWriteErrors: value = (int)gFanWriteErrors; break;
	case kCtlDump:          value = 0; break;
	}
	IOLockUnlock(gLock);

	error = sysctl_handle_int(oidp, &value, 0, req);
	if (error != 0 || req->newptr == USER_ADDR_NULL)
		return error;

	error = 0;
	IOLockLock(gLock);
	switch (arg2) {
	case kCtlEUserMax:
	case kCtlPUserMax: {
		int index = gHaveClusters ? sf_opp_index(c->opps, (unsigned)value) : -1;

		if (index < 0 || (unsigned)index > sf_opp_top(c->opps))
			error = EINVAL;
		else
			c->userMax = (unsigned)index;
		break;
	}
	case kCtlMode:
		if (value < kModeHandsOff || value > kModeHoldTop)
			error = EINVAL;
		else
			gMode = (uint32_t)value;
		break;
	case kCtlPeriod:
		if (value < 10 || value > 10000)
			error = EINVAL;
		else
			gPeriodMs = (uint32_t)value;
		break;
	case kCtlStress:
		if (!gHaveClusters)
			error = ENODEV;
		else if (value < 1 || value > 100000)
			error = EINVAL;
		else
			sf_stress((uint32_t)value);
		break;
	case kCtlResidencyReset:
		bzero(gE.residency, sizeof(gE.residency));
		bzero(gP.residency, sizeof(gP.residency));
		break;
	case kCtlUpPermille:
		if (value < 100 || value > 1000)
			error = EINVAL;
		else
			gTunables.up_permille = (unsigned)value;
		break;
	case kCtlDownHold:
		if (value < 1 || value > 100)
			error = EINVAL;
		else
			gTunables.down_hold = (unsigned)value;
		break;
	case kCtlLimitC:
		if (value < 20 || value > 110 || value * 1000 >= gThermalCfg.critical_mc)
			error = EINVAL;
		else
			gThermalCfg.limit_mc = value * 1000;
		break;
	case kCtlCritC:
		if (value < 25 || value > 115 || value * 1000 <= gThermalCfg.limit_mc)
			error = EINVAL;
		else
			gThermalCfg.critical_mc = value * 1000;
		break;
	case kCtlTestOffset:
		if (value < -100 || value > 100)
			error = EINVAL;
		else
			gTestOffsetC = value;
		break;
	case kCtlTestFail:
		gTestFail = value != 0;
		break;
	case kCtlFanControl:
		if (value < 0 || value > 1)
			error = EINVAL;
		else
			gFanControl = (uint32_t)value;
		break;
	case kCtlFanStartC:
		if (value < 30 || value > 100 || value * 1000 >= gFanCfg.full_mc)
			error = EINVAL;
		else
			gFanCfg.start_mc = value * 1000;
		break;
	case kCtlFanFullC:
		if (value < 40 || value > 110 || value * 1000 <= gFanCfg.start_mc)
			error = EINVAL;
		else
			gFanCfg.full_mc = value * 1000;
		break;
	case kCtlDump:
		gDumpRequest = 1;
		break;
	default:
		error = EPERM;
	}
	IOLockUnlock(gLock);
	return error;
}

static int
sf_sysctl_string(__unused struct sysctl_oid *oidp, __unused void *arg1, int arg2, struct sysctl_req *req)
{
	char text[1024];
	size_t used = 0;

	text[0] = 0;
	IOLockLock(gLock);
	switch (arg2) {
	case 0:         /* thermal status */
		used = (size_t)snprintf(text, sizeof(text), "%s", sf_thermal_status_name(gThermal.status));
		break;
	case 1:         /* die sensors */
		for (unsigned i = 0; i < gDieKeyCount && used < sizeof(text) - 16; i++) {
			char name[5];
			int32_t m = gDieMilli[i];

			key_name(gDieKeys[i].key, name);
			if (m == INT32_MIN)
				used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%s=?", i ? " " : "", name);
			else
				used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%s=%d.%d", i ? " " : "", name,
				    m / 1000, (m < 0 ? -m : m) % 1000 / 100);
		}
		break;
	case 2:         /* hottest die sensor */
		if (gDieValid) {
			key_name(gDieMaxKey, text);
			used = 4;
		}
		break;
	case 3:
	case 4: {       /* samples at each state: "1:12 2:0 ..." */
		const Cluster *c = arg2 == 3 ? &gE : &gP;

		for (unsigned i = 0; gHaveClusters && i <= sf_opp_top(c->opps) && used < sizeof(text) - 24; i++)
			used += (size_t)snprintf(text + used, sizeof(text) - used, "%s%u:%u", i ? " " : "",
			    c->opps->opp[i].pstate, c->residency[i]);
		break;
	}
	}
	IOLockUnlock(gLock);
	return SYSCTL_OUT(req, text, strlen(text) + 1);
}

SYSCTL_DECL(_kern);
SYSCTL_NODE(_kern, OID_AUTO, sf_cpufreq, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "CPU cluster performance states");
SYSCTL_NODE(_kern, OID_AUTO, sf_thermal, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "SoC die temperature limit and fan");

#define SF_INT(parent, name, id, flags, text) \
	SYSCTL_PROC(parent, OID_AUTO, name, CTLTYPE_INT | (flags) | CTLFLAG_LOCKED, 0, id, sf_sysctl_int, "I", text)
#define SF_STR(parent, name, id, text) \
	SYSCTL_PROC(parent, OID_AUTO, name, CTLTYPE_STRING | CTLFLAG_RD | CTLFLAG_LOCKED, 0, id, sf_sysctl_string, "A", text)

SF_INT(_kern_sf_cpufreq, mode, kCtlMode, CTLFLAG_RW, "0 hands off, 1 follow load, 2 hold the highest allowed state");
SF_INT(_kern_sf_cpufreq, period_ms, kCtlPeriod, CTLFLAG_RW, "sample period");
SF_INT(_kern_sf_cpufreq, e_pstate, kCtlEPstate, CTLFLAG_RD, "E cluster state (status register)");
SF_INT(_kern_sf_cpufreq, p_pstate, kCtlPPstate, CTLFLAG_RD, "P cluster state (status register)");
SF_INT(_kern_sf_cpufreq, e_khz, kCtlEKhz, CTLFLAG_RD, "E cluster frequency");
SF_INT(_kern_sf_cpufreq, p_khz, kCtlPKhz, CTLFLAG_RD, "P cluster frequency");
SF_INT(_kern_sf_cpufreq, e_util, kCtlEUtil, CTLFLAG_RD, "E cluster busiest processor, per mille");
SF_INT(_kern_sf_cpufreq, p_util, kCtlPUtil, CTLFLAG_RD, "P cluster busiest processor, per mille");
SF_INT(_kern_sf_cpufreq, e_cap, kCtlECap, CTLFLAG_RD, "highest E state allowed now");
SF_INT(_kern_sf_cpufreq, p_cap, kCtlPCap, CTLFLAG_RD, "highest P state allowed now");
SF_INT(_kern_sf_cpufreq, e_max, kCtlEUserMax, CTLFLAG_RW, "highest E state the policy may use");
SF_INT(_kern_sf_cpufreq, p_max, kCtlPUserMax, CTLFLAG_RW, "highest P state the policy may use");
SF_INT(_kern_sf_cpufreq, transitions, kCtlTransitions, CTLFLAG_RD, "state requests written");
SF_INT(_kern_sf_cpufreq, busy_timeouts, kCtlBusyTimeouts, CTLFLAG_RD, "requests dropped: cluster busy");
SF_INT(_kern_sf_cpufreq, mismatches, kCtlMismatches, CTLFLAG_RD, "samples that found another state than requested");
SF_INT(_kern_sf_cpufreq, stress, kCtlStress, CTLFLAG_RW, "write N: N checked transitions per cluster; read: done");
SF_INT(_kern_sf_cpufreq, stress_failures, kCtlStressFailures, CTLFLAG_RD, "failed transitions of the last stress");
SF_INT(_kern_sf_cpufreq, stress_max_us, kCtlStressMaxUs, CTLFLAG_RD, "slowest transition of the last stress");
SF_INT(_kern_sf_cpufreq, residency_reset, kCtlResidencyReset, CTLFLAG_RW, "write: clear the residency counts");
SF_INT(_kern_sf_cpufreq, up_permille, kCtlUpPermille, CTLFLAG_RW, "utilisation that goes straight to the top");
SF_INT(_kern_sf_cpufreq, down_hold, kCtlDownHold, CTLFLAG_RW, "samples before stepping down");
SF_STR(_kern_sf_cpufreq, e_residency, 3, "samples at each E state");
SF_STR(_kern_sf_cpufreq, p_residency, 4, "samples at each P state");

SF_STR(_kern_sf_thermal, status, 0, "no-sensor, ok, capped or critical");
SF_STR(_kern_sf_thermal, sensors, 1, "die sensors, degrees C");
SF_STR(_kern_sf_thermal, die_key, 2, "hottest die sensor");
SF_INT(_kern_sf_thermal, die_mc, kCtlDieMc, CTLFLAG_RD, "hottest die sensor, millidegrees C (-1: none)");
SF_INT(_kern_sf_thermal, valid, kCtlValid, CTLFLAG_RD, "the last poll read a die temperature");
SF_INT(_kern_sf_thermal, limit_c, kCtlLimitC, CTLFLAG_RW, "die temperature at which the caps start to fall");
SF_INT(_kern_sf_thermal, critical_c, kCtlCritC, CTLFLAG_RW, "die temperature that cuts to the lowest states");
SF_INT(_kern_sf_thermal, cap_e, kCtlThCapE, CTLFLAG_RD, "highest E state the temperature allows");
SF_INT(_kern_sf_thermal, cap_p, kCtlThCapP, CTLFLAG_RD, "highest P state the temperature allows");
SF_INT(_kern_sf_thermal, fan_rpm, kCtlFanRpm, CTLFLAG_RD, "fan speed (F0Ac)");
SF_INT(_kern_sf_thermal, fan_target_rpm, kCtlFanTarget, CTLFLAG_RD, "fan target (F0Tg)");
SF_INT(_kern_sf_thermal, fan_min_rpm, kCtlFanMin, CTLFLAG_RD, "fan minimum (F0Mn)");
SF_INT(_kern_sf_thermal, fan_max_rpm, kCtlFanMax, CTLFLAG_RD, "fan maximum (F0Mx)");
SF_INT(_kern_sf_thermal, fan_mode, kCtlFanMode, CTLFLAG_RD, "fan mode (F0Md): 0 is the SMC's own control");
SF_INT(_kern_sf_thermal, system_mw, kCtlPowerMw, CTLFLAG_RD, "total system power (PSTR)");
SF_INT(_kern_sf_thermal, polls, kCtlPolls, CTLFLAG_RD, "sensor polls");
SF_INT(_kern_sf_thermal, read_errors, kCtlReadErrors, CTLFLAG_RD, "failed sensor reads");
SF_INT(_kern_sf_thermal, smc_keys, kCtlSmcKeys, CTLFLAG_RD, "keys in the SMC's table");
SF_INT(_kern_sf_thermal, temperature_keys, kCtlTempKeys, CTLFLAG_RD, "temperature keys found");
SF_INT(_kern_sf_thermal, die_keys, kCtlDieKeys, CTLFLAG_RD, "die sensors in use");
SF_INT(_kern_sf_thermal, smc_opens, kCtlSmcOpens, CTLFLAG_RD, "SMC sessions opened");
SF_INT(_kern_sf_thermal, poll_us, kCtlPollUs, CTLFLAG_RD, "duration of the last sensor poll");
SF_INT(_kern_sf_thermal, poll_max_us, kCtlPollMaxUs, CTLFLAG_RD, "longest sensor poll (after the first two)");
SF_INT(_kern_sf_thermal, fan_control, kCtlFanControl, CTLFLAG_RW, "0: the fan is the SMC's; 1: set it from the die temperature");
SF_INT(_kern_sf_thermal, fan_start_c, kCtlFanStartC, CTLFLAG_RW, "fan control: die temperature at which the fan starts");
SF_INT(_kern_sf_thermal, fan_full_c, kCtlFanFullC, CTLFLAG_RW, "fan control: die temperature of the maximum speed");
SF_INT(_kern_sf_thermal, fan_asked_rpm, kCtlFanAsked, CTLFLAG_RD, "fan control: speed last written (0: none)");
SF_INT(_kern_sf_thermal, fan_writes, kCtlFanWrites, CTLFLAG_RD, "fan control: SMC keys written");
SF_INT(_kern_sf_thermal, fan_write_errors, kCtlFanWriteErrors, CTLFLAG_RD, "fan control: failed SMC writes");
SF_INT(_kern_sf_thermal, dump, kCtlDump, CTLFLAG_RW, "write: log every temperature key at the next poll");
SF_INT(_kern_sf_thermal, test_offset_c, kCtlTestOffset, CTLFLAG_RW, "TEST: degrees added to every die reading");
SF_INT(_kern_sf_thermal, test_fail, kCtlTestFail, CTLFLAG_RW, "TEST: 1 makes every poll fail");

static struct sysctl_oid *const gOids[] = {
	&sysctl__kern_sf_cpufreq, &sysctl__kern_sf_thermal,
	&sysctl__kern_sf_cpufreq_mode, &sysctl__kern_sf_cpufreq_period_ms,
	&sysctl__kern_sf_cpufreq_e_pstate, &sysctl__kern_sf_cpufreq_p_pstate,
	&sysctl__kern_sf_cpufreq_e_khz, &sysctl__kern_sf_cpufreq_p_khz,
	&sysctl__kern_sf_cpufreq_e_util, &sysctl__kern_sf_cpufreq_p_util,
	&sysctl__kern_sf_cpufreq_e_cap, &sysctl__kern_sf_cpufreq_p_cap,
	&sysctl__kern_sf_cpufreq_e_max, &sysctl__kern_sf_cpufreq_p_max,
	&sysctl__kern_sf_cpufreq_transitions, &sysctl__kern_sf_cpufreq_busy_timeouts,
	&sysctl__kern_sf_cpufreq_mismatches, &sysctl__kern_sf_cpufreq_stress,
	&sysctl__kern_sf_cpufreq_stress_failures, &sysctl__kern_sf_cpufreq_stress_max_us,
	&sysctl__kern_sf_cpufreq_residency_reset, &sysctl__kern_sf_cpufreq_up_permille,
	&sysctl__kern_sf_cpufreq_down_hold, &sysctl__kern_sf_cpufreq_e_residency,
	&sysctl__kern_sf_cpufreq_p_residency,
	&sysctl__kern_sf_thermal_status, &sysctl__kern_sf_thermal_sensors, &sysctl__kern_sf_thermal_die_key,
	&sysctl__kern_sf_thermal_die_mc, &sysctl__kern_sf_thermal_valid, &sysctl__kern_sf_thermal_limit_c,
	&sysctl__kern_sf_thermal_critical_c, &sysctl__kern_sf_thermal_cap_e, &sysctl__kern_sf_thermal_cap_p,
	&sysctl__kern_sf_thermal_fan_rpm, &sysctl__kern_sf_thermal_fan_target_rpm,
	&sysctl__kern_sf_thermal_fan_min_rpm, &sysctl__kern_sf_thermal_fan_max_rpm,
	&sysctl__kern_sf_thermal_fan_mode, &sysctl__kern_sf_thermal_system_mw, &sysctl__kern_sf_thermal_polls,
	&sysctl__kern_sf_thermal_read_errors, &sysctl__kern_sf_thermal_smc_keys,
	&sysctl__kern_sf_thermal_temperature_keys, &sysctl__kern_sf_thermal_die_keys,
	&sysctl__kern_sf_thermal_smc_opens, &sysctl__kern_sf_thermal_test_offset_c,
	&sysctl__kern_sf_thermal_test_fail,
	&sysctl__kern_sf_thermal_fan_control, &sysctl__kern_sf_thermal_fan_start_c,
	&sysctl__kern_sf_thermal_fan_full_c, &sysctl__kern_sf_thermal_fan_asked_rpm,
	&sysctl__kern_sf_thermal_fan_writes, &sysctl__kern_sf_thermal_fan_write_errors,
	&sysctl__kern_sf_thermal_dump, &sysctl__kern_sf_thermal_poll_us, &sysctl__kern_sf_thermal_poll_max_us,
};

/* ---- the service -------------------------------------------------------------------- */

class SFCpuPower : public IOService
{
	OSDeclareDefaultStructors(SFCpuPower);

public:
	bool start(IOService *provider) APPLE_KEXT_OVERRIDE;
	void systemWillShutdown(IOOptionBits specifier) APPLE_KEXT_OVERRIDE;
};

OSDefineMetaClassAndStructors(SFCpuPower, IOService);

static bool
sf_cluster_setup(Cluster *c, const struct sf_cluster_opps *opps, uint64_t phys, cluster_type_t type)
{
	const ml_topology_info_t *topo = ml_get_topology_info();
	IOMemoryDescriptor *desc;
	int hw;

	bzero(c, sizeof(*c));
	c->opps = opps;
	c->userMax = sf_opp_top(opps);
	c->effectiveCap = sf_opp_fallback(opps);
	for (unsigned i = 0; topo != NULL && i < topo->num_cpus && c->ncpu < MAX_CLUSTER_CPUS; i++) {
		if (topo->cpus[i].cluster_type == type)
			c->cpu[c->ncpu++] = topo->cpus[i].cpu_id;
	}
	desc = IOMemoryDescriptor::withPhysicalAddress((IOPhysicalAddress)phys, CLUSTER_DVFS_LEN, kIODirectionOutIn);
	c->map = desc ? desc->map(kIOMapAnywhere | kIOMapInhibitCache) : NULL;
	OSSafeReleaseNULL(desc);
	if (c->map == NULL)
		return false;
	c->reg = (volatile uint8_t *)c->map->getVirtualAddress();
	hw = sf_opp_index(opps, sf_cluster_current(c));
	c->gov.index = hw >= 0 ? (unsigned)hw : 0;
	LOG("%s cluster: %u processors, registers at 0x%llx, state %u (command 0x%llx), states 1..%u, "
	    "fallback %u", opps->name, c->ncpu, phys, sf_cluster_current(c), reg_read64(c, APPLE_DVFS_CMD),
	    opps->opp[sf_opp_top(opps)].pstate, opps->opp[sf_opp_fallback(opps)].pstate);
	return true;
}

/*
 * Shutdown and restart: give the fan back to the SMC before the system goes, so that
 * whatever runs next does not inherit manual mode. The sensor thread owns the SMC session
 * and does the write; wait for it, but not for long. (A panic and a guest stopped from
 * outside do not come through here.)
 */
void
SFCpuPower::systemWillShutdown(IOOptionBits specifier)
{
	if (gThermalEnabled && gFanManual && OSCompareAndSwap(0, 1, &gShutdown)) {
		for (int i = 0; i < 40 && gShutdown != 2; i++)
			IOSleep(100);
		LOG("shutdown: fan %s", gShutdown == 2 && !gFanManual ? "given back to the SMC" : "NOT given back in 4 s");
	}
	IOService::systemWillShutdown(specifier);
}

bool
SFCpuPower::start(IOService *provider)
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

	if (PE_parse_boot_argn("sfcpufreq", &value, sizeof(value)) && value <= kModeHoldTop)
		gMode = value;
	if (PE_parse_boot_argn("sfcpufreq_period", &value, sizeof(value)) && value >= 10 && value <= 10000)
		gPeriodMs = value;
	PE_parse_boot_argn("sfthermal", &gThermalEnabled, sizeof(gThermalEnabled));
	if (PE_parse_boot_argn("sfthermal_limit", &value, sizeof(value)) && value >= 20 && value <= 110)
		gThermalCfg.limit_mc = (int32_t)value * 1000;
	if (PE_parse_boot_argn("sfthermal_crit", &value, sizeof(value)) && value >= 25 && value <= 115)
		gThermalCfg.critical_mc = (int32_t)value * 1000;
	if (gThermalCfg.critical_mc <= gThermalCfg.limit_mc)
		gThermalCfg.critical_mc = gThermalCfg.limit_mc + 10000;
	if (PE_parse_boot_argn("sfthermal_poll", &value, sizeof(value)) && value >= 100 && value <= 60000)
		gThermalPollMs = value;
	PE_parse_boot_argn("sfthermal_log", &gThermalLogS, sizeof(gThermalLogS));
	PE_parse_boot_argn("sfthermal_delay", &gThermalDelayS, sizeof(gThermalDelayS));
	PE_parse_boot_argn("sfthermal_dump", &gThermalDump, sizeof(gThermalDump));
	PE_parse_boot_argn("sfsmc_verbose", &gSmcKeepVerbose, sizeof(gSmcKeepVerbose));
	if (PE_parse_boot_argn("sffan", &value, sizeof(value)) && value <= 1)
		gFanControl = value;
	if (PE_parse_boot_argn("sffan_start", &value, sizeof(value)) && value >= 30 && value <= 100)
		gFanCfg.start_mc = (int32_t)value * 1000;
	if (PE_parse_boot_argn("sffan_full", &value, sizeof(value)) && value >= 40 && value <= 110 &&
	    (int32_t)value * 1000 > gFanCfg.start_mc)
		gFanCfg.full_mc = (int32_t)value * 1000;
	PE_parse_boot_arg_str("sfthermal_keys", gThermalKeyArg, sizeof(gThermalKeyArg));
	PE_parse_boot_arg_str("sfthermal_suffix", gThermalSuffixArg, sizeof(gThermalSuffixArg));

	gLock = IOLockAlloc();
	if (gLock == NULL)
		return false;
	sf_thermal_init(&gThermal, &sf_t8103_ecluster, &sf_t8103_pcluster);

	gHaveClusters = sf_cluster_setup(&gE, &sf_t8103_ecluster, T8103_ECLUSTER_DVFS, CLUSTER_TYPE_E) &&
	    sf_cluster_setup(&gP, &sf_t8103_pcluster, T8103_PCLUSTER_DVFS, CLUSTER_TYPE_P);
	if (!gHaveClusters)
		LOG("the cluster registers could not be mapped; no performance states are requested");

	for (unsigned i = 0; i < sizeof(gOids) / sizeof(gOids[0]); i++)
		sysctl_register_oid(gOids[i]);

	LOG("mode %u (%s), period %u ms; thermal %s, limit %d C, critical %d C, poll %u ms",
	    gMode, gMode == kModeFollowLoad ? "follow load" : gMode == kModeHoldTop ? "hold top" : "hands off",
	    gPeriodMs, gThermalEnabled ? "on" : "OFF (fallback states only)", gThermalCfg.limit_mc / 1000,
	    gThermalCfg.critical_mc / 1000, gThermalPollMs);

	gArmed = !gThermalEnabled;
	if (gHaveClusters && kernel_thread_start(sf_governor_thread, NULL, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);
	if (gThermalEnabled && kernel_thread_start(sf_thermal_thread, NULL, &thread) == KERN_SUCCESS)
		thread_deallocate(thread);

	registerService();
	return true;
}
