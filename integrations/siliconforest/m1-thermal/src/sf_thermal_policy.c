// SPDX-License-Identifier: GPL-2.0-only
/* See sf_thermal_policy.h. */
#include "sf_thermal_policy.h"

static unsigned
min_u(unsigned a, unsigned b)
{
	return a < b ? a : b;
}

void
sf_thermal_init(struct sf_thermal *th, const struct sf_cluster_opps *e,
    const struct sf_cluster_opps *p)
{
	th->status = SF_THERMAL_NO_SENSOR;
	th->cap_e = sf_opp_fallback(e);
	th->cap_p = sf_opp_fallback(p);
	th->bad = 0;
	th->last_mc = 0;
}

void
sf_thermal_poll(struct sf_thermal *th, const struct sf_thermal_config *cfg,
    const struct sf_cluster_opps *e, const struct sf_cluster_opps *p, int valid, int32_t temp_mc)
{
	unsigned top_e = sf_opp_top(e), top_p = sf_opp_top(p);

	if (!valid) {
		if (th->status == SF_THERMAL_NO_SENSOR)
			return;
		if (++th->bad >= cfg->bad_polls) {
			th->status = SF_THERMAL_NO_SENSOR;
			th->cap_e = min_u(th->cap_e, sf_opp_fallback(e));
			th->cap_p = min_u(th->cap_p, sf_opp_fallback(p));
		}
		return;
	}
	th->bad = 0;
	th->last_mc = temp_mc;

	if (temp_mc >= cfg->critical_mc) {
		th->status = SF_THERMAL_CRITICAL;
		th->cap_e = th->cap_p = 0;
		return;
	}
	if (temp_mc >= cfg->limit_mc) {
		unsigned steps = 1 + (unsigned)(temp_mc - cfg->limit_mc) / 2000;

		if (th->status != SF_THERMAL_CRITICAL)
			th->status = SF_THERMAL_CAPPED;
		while (steps-- > 0) {
			if (th->cap_p > 0)
				th->cap_p--;
			else if (th->cap_e > 0)
				th->cap_e--;
		}
		return;
	}
	if (temp_mc < cfg->limit_mc - cfg->hysteresis_mc) {
		/* out of the fallback, the critical cut or a cap: one state back per poll */
		if (th->cap_e < top_e)
			th->cap_e++;
		else if (th->cap_p < top_p)
			th->cap_p++;
		th->status = (th->cap_e >= top_e && th->cap_p >= top_p) ? SF_THERMAL_OK : SF_THERMAL_CAPPED;
		return;
	}
	/* inside the hysteresis band: hold */
	if (th->status == SF_THERMAL_NO_SENSOR || th->status == SF_THERMAL_OK)
		th->status = (th->cap_e >= top_e && th->cap_p >= top_p) ? SF_THERMAL_OK : SF_THERMAL_CAPPED;
}

const char *
sf_thermal_status_name(enum sf_thermal_status s)
{
	switch (s) {
	case SF_THERMAL_NO_SENSOR: return "no-sensor";
	case SF_THERMAL_OK:        return "ok";
	case SF_THERMAL_CAPPED:    return "capped";
	case SF_THERMAL_CRITICAL:  return "critical";
	}
	return "?";
}

int
sf_smc_flt_to_milli(uint32_t bits, int32_t *milli)
{
	uint32_t exp = (bits >> 23) & 0xff;
	uint64_t mant = bits & 0x7fffff;
	int negative = (bits >> 31) != 0;
	uint64_t scaled;
	int shift;

	if (exp == 0xff)
		return -1;                      /* NaN, infinity */
	if (exp == 0) {
		*milli = 0;                     /* zero and denormals: below a thousandth */
		return 0;
	}
	mant |= 1ULL << 23;                     /* value = mant * 2^(exp - 127 - 23) */
	shift = (int)exp - 127 - 23;
	if (shift > 7)
		return -1;                      /* 2^31 and above: not a sensor value */
	scaled = mant * 1000;
	if (shift >= 0)
		scaled <<= shift;
	else if (shift > -64)
		scaled = (scaled + (1ULL << (-shift - 1))) >> -shift;
	else
		scaled = 0;
	if (scaled > 0x7fffffffULL)
		return -1;
	*milli = negative ? -(int32_t)scaled : (int32_t)scaled;
	return 0;
}

int
sf_smc_ioft_to_milli(uint64_t value, int32_t *milli)
{
	uint64_t whole = value >> 16, frac = value & 0xffff;

	if (whole > 0x7fffffffULL / 1000 - 1)
		return -1;
	*milli = (int32_t)(whole * 1000 + ((frac * 1000 + 0x8000) >> 16));
	return 0;
}

/* macsmc_hwmon_write_f32() */
uint32_t
sf_smc_int_to_flt(int32_t value)
{
	uint32_t neg = value < 0;
	uint64_t val = neg ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
	int exp = 0;

	if (val == 0)
		return 0;
	while ((val >> (exp + 1)) != 0)
		exp++;                          /* __fls() */
	if (exp > 23)
		val >>= exp - 23;
	else
		val <<= 23 - exp;
	return (neg << 31) | ((uint32_t)(exp + 127) << 23) | ((uint32_t)val & 0x7fffff);
}

int32_t
sf_fan_target(const struct sf_fan_config *cfg, int32_t die_mc, int32_t min_rpm, int32_t max_rpm,
    int32_t current_rpm)
{
	int64_t rpm;

	if (min_rpm <= 0 || max_rpm <= min_rpm || cfg->full_mc <= cfg->start_mc)
		return 0;
	if (die_mc < cfg->start_mc - 10000)
		return 0;
	if (die_mc < cfg->start_mc)
		return current_rpm > 0 ? min_rpm : 0;
	if (die_mc >= cfg->full_mc)
		return max_rpm;
	rpm = min_rpm + (int64_t)(max_rpm - min_rpm) * (die_mc - cfg->start_mc) / (cfg->full_mc - cfg->start_mc);
	rpm = (rpm + 50) / 100 * 100;
	if (rpm < min_rpm)
		rpm = min_rpm;
	if (rpm > max_rpm)
		rpm = max_rpm;
	return (int32_t)rpm;
}
