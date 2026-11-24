// SPDX-License-Identifier: GPL-2.0-only
/*
 * See sf_cpufreq_policy.h. The tables are arch/arm64/boot/dts/apple/t8103.dtsi
 * (ecluster_opp, pcluster_opp) of the pinned Linux tree, GPL-2.0+ OR MIT,
 * Copyright The Asahi Linux Contributors.
 */
#include "sf_cpufreq_policy.h"

static const struct sf_opp t8103_e[] = {
	{ 1, 0,  600000,   47296 },
	{ 2, 0,  972000,   99715 },
	{ 3, 0, 1332000,  188860 },
	{ 4, 0, 1704000,  288891 },
	{ 5, 0, 2064000,  412979 },
};

static const struct sf_opp t8103_p[] = {
	{  1, 0,  600000,  290230 },
	{  2, 0,  828000,  449013 },
	{  3, 0, 1056000,  647097 },
	{  4, 0, 1284000,  865620 },
	{  5, 0, 1500000, 1112838 },
	{  6, 0, 1728000, 1453271 },
	{  7, 0, 1956000, 1776667 },
	{  8, 0, 2184000, 2366690 },
	{  9, 0, 2388000, 2892193 },
	{ 10, 0, 2592000, 3475417 },
	{ 11, 0, 2772000, 3959410 },
	{ 12, 0, 2988000, 4540620 },
	/* "Not available until CPU deep sleep is implemented" */
	{ 13, 1, 3096000, 4745031 },
	{ 14, 1, 3144000, 4822390 },
	{ 15, 1, 3204000, 4951324 },
};

const struct sf_cluster_opps sf_t8103_ecluster = { "E", t8103_e, sizeof(t8103_e) / sizeof(t8103_e[0]) };
const struct sf_cluster_opps sf_t8103_pcluster = { "P", t8103_p, sizeof(t8103_p) / sizeof(t8103_p[0]) };

unsigned
sf_opp_top(const struct sf_cluster_opps *c)
{
	unsigned top = 0;

	for (unsigned i = 0; i < c->count; i++) {
		if (!c->opp[i].turbo)
			top = i;
	}
	return top;
}

int
sf_opp_index(const struct sf_cluster_opps *c, unsigned pstate)
{
	for (unsigned i = 0; i < c->count; i++) {
		if (c->opp[i].pstate == pstate)
			return (int)i;
	}
	return -1;
}

/* apple_fast_die_init(): fallback = apple_fast_die_fit(power of the fastest state / 3) */
unsigned
sf_opp_fallback(const struct sf_cluster_opps *c)
{
	unsigned top = sf_opp_top(c), index = 0;
	uint32_t budget = c->opp[top].microwatt / 3;

	for (unsigned i = 0; i <= top; i++) {
		if (c->opp[i].microwatt <= budget)
			index = i;
	}
	return index;
}

unsigned
sf_gov_sample(const struct sf_cluster_opps *c, const struct sf_gov_tunables *t,
    struct sf_gov *g, unsigned util_permille, unsigned cap)
{
	unsigned top = sf_opp_top(c), want;

	if (cap > top)
		cap = top;
	if (g->index > top)
		g->index = top;
	if (util_permille > 1000)
		util_permille = 1000;

	if (util_permille >= t->up_permille) {
		want = top;
	} else {
		/* the frequency that would have carried this load with the margin to spare */
		uint64_t need = (uint64_t)c->opp[g->index].khz * util_permille / 1000 *
		    t->margin_permille / 1000;

		for (want = 0; want < top && c->opp[want].khz < need; want++)
			;
	}
	if (want > cap)
		want = cap;

	if (want > g->index) {
		g->index = want;
		g->lower_votes = 0;
	} else if (want < g->index) {
		if (g->index > cap) {
			/* a limit is not voted on */
			g->index = cap;
			g->lower_votes = 0;
		} else if (++g->lower_votes >= t->down_hold) {
			g->index = want;
			g->lower_votes = 0;
		}
	} else {
		g->lower_votes = 0;
	}
	return g->index;
}
