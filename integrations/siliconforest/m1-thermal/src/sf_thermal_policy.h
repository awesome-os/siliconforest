/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Die-temperature limit of SFCpuPower: turns the hottest die sensor into the highest
 * operating point each cluster may use. Plain C; built into the kext and the host test.
 *
 * Fail-safe as apple_fast_die in the pinned Linux drivers/cpufreq/apple-soc-cpufreq.c
 * (GPL-2.0-only, Copyright The Asahi Linux Contributors): while no valid temperature
 * has been read, or readings stop, the clusters are held at the fallback state (the
 * fastest whose power is at most a third of the fastest state's).
 */
#ifndef SF_THERMAL_POLICY_H
#define SF_THERMAL_POLICY_H

#include <stdint.h>
#include "sf_cpufreq_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

enum sf_thermal_status {
	SF_THERMAL_NO_SENSOR = 0,       /* no valid reading (yet, or any more): fallback caps */
	SF_THERMAL_OK,                  /* no cap */
	SF_THERMAL_CAPPED,              /* above the limit, or coming back from it */
	SF_THERMAL_CRITICAL,            /* at or above the critical temperature: lowest states */
};

struct sf_thermal_config {
	int32_t limit_mc;               /* start lowering the caps at this temperature */
	int32_t critical_mc;            /* cut to the lowest states */
	int32_t hysteresis_mc;          /* raise again below limit - hysteresis */
	unsigned bad_polls;             /* failed polls in a row before the fallback caps */
};

struct sf_thermal {
	enum sf_thermal_status status;
	unsigned cap_e, cap_p;          /* highest allowed index into each cluster's opp[] */
	unsigned bad;
	int32_t last_mc;
};

void sf_thermal_init(struct sf_thermal *th, const struct sf_cluster_opps *e,
    const struct sf_cluster_opps *p);

/*
 * One poll. valid: a plausible die temperature was read, temp_mc in millidegrees C.
 * Lowers the P cluster first, then the E cluster, by one state plus one per 2 C over the
 * limit; raises one state per poll (E first) once below limit - hysteresis.
 */
void sf_thermal_poll(struct sf_thermal *th, const struct sf_thermal_config *cfg,
    const struct sf_cluster_opps *e, const struct sf_cluster_opps *p, int valid, int32_t temp_mc);

const char *sf_thermal_status_name(enum sf_thermal_status s);

/*
 * SMC values to thousandths. "flt " is a little-endian IEEE 754 single, "ioft" a
 * little-endian unsigned 48.16 fixed point (the two types the pinned Linux
 * drivers/hwmon/macsmc-hwmon.c reads sensors as). No floating point is used.
 * Returns 0, or -1 for a NaN/infinity or a magnitude above 2^31 / 1000.
 */
int sf_smc_flt_to_milli(uint32_t bits, int32_t *milli);
int sf_smc_ioft_to_milli(uint64_t value, int32_t *milli);
/* An integer as "flt " bits (macsmc_hwmon_write_f32 of the pinned Linux macsmc-hwmon.c). */
uint32_t sf_smc_int_to_flt(int32_t value);

/*
 * Fan speed for a die temperature, for a machine whose SMC leaves the fan off: 0 (off)
 * below start_mc, min_rpm at start_mc rising linearly to max_rpm at full_mc. A running
 * fan keeps running down to start_mc - 10 C (current_rpm is what was last asked for).
 * Rounded to 100 rpm, so that small temperature changes do not rewrite the target.
 */
struct sf_fan_config {
	int32_t start_mc, full_mc;
};
int32_t sf_fan_target(const struct sf_fan_config *cfg, int32_t die_mc, int32_t min_rpm, int32_t max_rpm,
    int32_t current_rpm);

#ifdef __cplusplus
}
#endif
#endif
