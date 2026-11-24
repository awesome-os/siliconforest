// SPDX-License-Identifier: GPL-2.0-only
/*
 * Host test of the operating points, the load-following policy, the die-temperature
 * limit and the SMC value conversions. Build and run: see ./run.sh.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sf_cpufreq_policy.h"
#include "sf_thermal_policy.h"

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static const struct sf_cluster_opps *E = &sf_t8103_ecluster, *P = &sf_t8103_pcluster;
static const struct sf_gov_tunables T = { 800, 1250, 3 };

static void
test_opps(void)
{
	CHECK(E->count == 5 && P->count == 15);
	CHECK(E->opp[sf_opp_top(E)].pstate == 5 && E->opp[sf_opp_top(E)].khz == 2064000);
	/* the three turbo states are never the top */
	CHECK(P->opp[sf_opp_top(P)].pstate == 12 && P->opp[sf_opp_top(P)].khz == 2988000);
	/* a third of 4540620 uW is 1513540: P6 (1453271) fits, P7 (1776667) does not */
	CHECK(P->opp[sf_opp_fallback(P)].pstate == 6);
	/* a third of 412979 uW is 137659: E2 (99715) fits, E3 (188860) does not */
	CHECK(E->opp[sf_opp_fallback(E)].pstate == 2);
	CHECK(sf_opp_index(P, 12) == 11 && sf_opp_index(P, 0) == -1 && sf_opp_index(E, 6) == -1);
	for (unsigned i = 1; i < P->count; i++)
		CHECK(P->opp[i].khz > P->opp[i - 1].khz && P->opp[i].pstate == P->opp[i - 1].pstate + 1);
}

static void
test_governor(void)
{
	struct sf_gov g = { 0, 0 };
	unsigned top = sf_opp_top(P);

	/* busy: straight to the top, never into turbo, whatever cap is passed */
	CHECK(sf_gov_sample(P, &T, &g, 1000, 99) == top);
	CHECK(sf_gov_sample(P, &T, &g, 800, 99) == top);
	/* idle: holds for down_hold - 1 samples, then drops to the bottom */
	CHECK(sf_gov_sample(P, &T, &g, 0, top) == top);
	CHECK(sf_gov_sample(P, &T, &g, 0, top) == top);
	CHECK(sf_gov_sample(P, &T, &g, 0, top) == 0);
	/* a busy sample in between restarts the count */
	g.index = top; g.lower_votes = 0;
	sf_gov_sample(P, &T, &g, 0, top);
	sf_gov_sample(P, &T, &g, 0, top);
	CHECK(sf_gov_sample(P, &T, &g, 900, top) == top);
	CHECK(sf_gov_sample(P, &T, &g, 0, top) == top);
	/* half load at the top: 2988 MHz * 0.5 * 1.25 = 1867.5 MHz -> P7 (1956 MHz) */
	g.index = top; g.lower_votes = 0;
	sf_gov_sample(P, &T, &g, 500, top);
	sf_gov_sample(P, &T, &g, 500, top);
	CHECK(P->opp[sf_gov_sample(P, &T, &g, 500, top)].pstate == 7);
	/* 70 % at the bottom: 600 * 0.7 * 1.25 = 525 MHz: stays; 79 %: 592.5: stays */
	g.index = 0; g.lower_votes = 0;
	CHECK(sf_gov_sample(P, &T, &g, 700, top) == 0);
	CHECK(sf_gov_sample(P, &T, &g, 799, top) == 0);
	/* the cap: applied at once on the way down, and a ceiling on the way up */
	g.index = top; g.lower_votes = 0;
	CHECK(sf_gov_sample(P, &T, &g, 1000, 3) == 3);
	CHECK(sf_gov_sample(P, &T, &g, 1000, 3) == 3);
	CHECK(sf_gov_sample(P, &T, &g, 1000, 0) == 0);
	CHECK(sf_gov_sample(P, &T, &g, 1000, top) == top);
	/* a start index in turbo (whatever the boot loader left) is pulled into range */
	g.index = 14; g.lower_votes = 0;
	CHECK(sf_gov_sample(P, &T, &g, 1000, 99) == top);
	/* over-range utilisation is clamped */
	g.index = 0;
	CHECK(sf_gov_sample(E, &T, &g, 5000, 99) == sf_opp_top(E));
}

static void
test_thermal(void)
{
	const struct sf_thermal_config cfg = { 92000, 102000, 4000, 3 };
	struct sf_thermal th;
	unsigned top_e = sf_opp_top(E), top_p = sf_opp_top(P);

	sf_thermal_init(&th, E, P);
	CHECK(th.status == SF_THERMAL_NO_SENSOR);
	CHECK(E->opp[th.cap_e].pstate == 2 && P->opp[th.cap_p].pstate == 6);
	/* no reading: stays at the fallback for ever */
	for (int i = 0; i < 100; i++)
		sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	CHECK(th.status == SF_THERMAL_NO_SENSOR && P->opp[th.cap_p].pstate == 6);
	/* cool readings: one state per poll, E first, until both are at the top */
	int polls = 0;
	while (th.status != SF_THERMAL_OK && polls < 100) {
		sf_thermal_poll(&th, &cfg, E, P, 1, 50000);
		polls++;
	}
	CHECK(polls == 3 + 6 && th.cap_e == top_e && th.cap_p == top_p);
	/* in the band below the limit nothing changes */
	sf_thermal_poll(&th, &cfg, E, P, 1, 90000);
	CHECK(th.status == SF_THERMAL_OK && th.cap_p == top_p);
	/* at the limit: one P state per poll; 5 C over: 1 + 2 states */
	sf_thermal_poll(&th, &cfg, E, P, 1, 92000);
	CHECK(th.status == SF_THERMAL_CAPPED && th.cap_p == top_p - 1 && th.cap_e == top_e);
	sf_thermal_poll(&th, &cfg, E, P, 1, 97000);
	CHECK(th.cap_p == top_p - 4);
	/* held in the hysteresis band, released below it */
	sf_thermal_poll(&th, &cfg, E, P, 1, 89000);
	CHECK(th.status == SF_THERMAL_CAPPED && th.cap_p == top_p - 4);
	sf_thermal_poll(&th, &cfg, E, P, 1, 87999);
	CHECK(th.cap_p == top_p - 3);
	/* P runs out before E is touched */
	for (int i = 0; i < 40; i++)
		sf_thermal_poll(&th, &cfg, E, P, 1, 93000);
	CHECK(th.cap_p == 0 && th.cap_e == 0 && th.status == SF_THERMAL_CAPPED);
	/* critical: both at the bottom at once, and it stays critical until below the band */
	sf_thermal_init(&th, E, P);
	for (int i = 0; i < 20; i++)
		sf_thermal_poll(&th, &cfg, E, P, 1, 50000);
	CHECK(th.status == SF_THERMAL_OK);
	sf_thermal_poll(&th, &cfg, E, P, 1, 102000);
	CHECK(th.status == SF_THERMAL_CRITICAL && th.cap_e == 0 && th.cap_p == 0);
	sf_thermal_poll(&th, &cfg, E, P, 1, 95000);
	CHECK(th.status == SF_THERMAL_CRITICAL && th.cap_p == 0);
	sf_thermal_poll(&th, &cfg, E, P, 1, 89000);
	CHECK(th.status == SF_THERMAL_CRITICAL && th.cap_p == 0);
	sf_thermal_poll(&th, &cfg, E, P, 1, 80000);
	CHECK(th.status == SF_THERMAL_CAPPED && th.cap_e == 1);
	/* sensors going away: two bad polls are tolerated, the third falls back */
	sf_thermal_init(&th, E, P);
	for (int i = 0; i < 20; i++)
		sf_thermal_poll(&th, &cfg, E, P, 1, 50000);
	sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	CHECK(th.status == SF_THERMAL_OK && th.cap_p == top_p);
	sf_thermal_poll(&th, &cfg, E, P, 1, 50000);
	sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	CHECK(th.status == SF_THERMAL_OK);
	sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	CHECK(th.status == SF_THERMAL_NO_SENSOR && P->opp[th.cap_p].pstate == 6 && E->opp[th.cap_e].pstate == 2);
	/* the fallback never raises a cap that was already lower */
	sf_thermal_init(&th, E, P);
	for (int i = 0; i < 20; i++)
		sf_thermal_poll(&th, &cfg, E, P, 1, 50000);
	sf_thermal_poll(&th, &cfg, E, P, 1, 103000);
	for (int i = 0; i < 3; i++)
		sf_thermal_poll(&th, &cfg, E, P, 0, 0);
	CHECK(th.status == SF_THERMAL_NO_SENSOR && th.cap_p == 0 && th.cap_e == 0);
}

static void
test_values(void)
{
	static const float samples[] = { 0.0f, 1.0f, -1.0f, 27.6f, 36.5625f, 99.999f, 104.25f, 0.001f, 0.0004f,
		1199.0f, 7199.0f, 1234567.0f, -40.5f, 2147483.0f };
	int32_t milli;

	for (unsigned i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
		uint32_t bits;

		memcpy(&bits, &samples[i], 4);
		CHECK(sf_smc_flt_to_milli(bits, &milli) == 0);
		CHECK(milli == (int32_t)lround((double)samples[i] * 1000.0));
	}
	CHECK(sf_smc_flt_to_milli(0x7fc00000, &milli) == -1);    /* NaN */
	CHECK(sf_smc_flt_to_milli(0x7f800000, &milli) == -1);    /* infinity */
	CHECK(sf_smc_flt_to_milli(0x4f000000, &milli) == -1);    /* 2^31 */
	CHECK(sf_smc_flt_to_milli(0x00000001, &milli) == 0 && milli == 0);
	CHECK(sf_smc_ioft_to_milli(0x00000000002a8000ULL, &milli) == 0 && milli == 42500);
	CHECK(sf_smc_ioft_to_milli(0x0000000000000001ULL, &milli) == 0 && milli == 0);
	CHECK(sf_smc_ioft_to_milli(0xffffffffffffffffULL, &milli) == -1);
}

static void
test_fan(void)
{
	const struct sf_fan_config cfg = { 70000, 95000 };
	static const int32_t ints[] = { 0, 1, 2, 3, 1199, 1200, 4000, 7199, 16777216, 16777217, -5 };
	int32_t milli;

	for (unsigned i = 0; i < sizeof(ints) / sizeof(ints[0]); i++) {
		float f = (float)ints[i];
		uint32_t bits;

		memcpy(&bits, &f, 4);
		/* 16777217 is not a float: the Linux conversion truncates, the compiler rounds */
		if (ints[i] != 16777217)
			CHECK(sf_smc_int_to_flt(ints[i]) == bits);
		/* back again, where the value times 1000 still fits */
		if (ints[i] < 2000000)
			CHECK(sf_smc_flt_to_milli(sf_smc_int_to_flt(ints[i]), &milli) == 0 && milli == ints[i] * 1000);
	}
	CHECK(sf_smc_flt_to_milli(sf_smc_int_to_flt(4200), &milli) == 0 && milli == 4200000);
	/* off when cool, minimum at the start, maximum at the top, linear between */
	CHECK(sf_fan_target(&cfg, 50000, 1199, 7199, 0) == 0);
	CHECK(sf_fan_target(&cfg, 69999, 1199, 7199, 0) == 0);
	CHECK(sf_fan_target(&cfg, 70000, 1199, 7199, 0) == 1200);       /* rounded to 100 rpm */
	CHECK(sf_fan_target(&cfg, 82500, 1199, 7199, 0) == 4200);
	CHECK(sf_fan_target(&cfg, 95000, 1199, 7199, 0) == 7199);
	CHECK(sf_fan_target(&cfg, 140000, 1199, 7199, 0) == 7199);
	/* a running fan keeps its minimum down to 10 C below the start */
	CHECK(sf_fan_target(&cfg, 65000, 1199, 7199, 3000) == 1199);
	CHECK(sf_fan_target(&cfg, 60000, 1199, 7199, 1199) == 1199);
	CHECK(sf_fan_target(&cfg, 59999, 1199, 7199, 1199) == 0);
	/* unknown limits: never a target */
	CHECK(sf_fan_target(&cfg, 90000, -1, -1, 0) == 0);
	CHECK(sf_fan_target(&cfg, 90000, 1199, 1199, 0) == 0);
	for (int32_t t = 0; t < 150000; t += 250) {
		int32_t rpm = sf_fan_target(&cfg, t, 1199, 7199, 0);

		CHECK(rpm == 0 || (rpm >= 1199 && rpm <= 7199));
	}
}

int
main(void)
{
	test_fan();
	test_opps();
	test_governor();
	test_thermal();
	test_values();
	printf(failures ? "FAILED: %d checks\n" : "all policy checks passed\n", failures);
	return failures != 0;
}
