/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cluster operating points and the load-following policy of SFCpuPower. Plain C, no
 * kernel dependencies: built into the kext and into the host test (../test).
 *
 * The operating-point tables are the T8103 tables of the pinned Linux device tree
 * (arch/arm64/boot/dts/apple/t8103.dtsi, GPL-2.0+ OR MIT, Copyright The Asahi Linux
 * Contributors); the fallback rule is apple_fast_die's in drivers/cpufreq/
 * apple-soc-cpufreq.c (GPL-2.0-only, Copyright The Asahi Linux Contributors).
 */
#ifndef SF_CPUFREQ_POLICY_H
#define SF_CPUFREQ_POLICY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sf_opp {
	uint8_t  pstate;        /* opp-level: the hardware P-state number */
	uint8_t  turbo;         /* turbo-mode: never requested (see t8103.dtsi) */
	uint32_t khz;           /* opp-hz / 1000 */
	uint32_t microwatt;     /* opp-microwatt */
};

struct sf_cluster_opps {
	const char *name;
	const struct sf_opp *opp;       /* ascending */
	unsigned count;
};

extern const struct sf_cluster_opps sf_t8103_ecluster;
extern const struct sf_cluster_opps sf_t8103_pcluster;

/* Index (into opp[]) of the fastest state that is not turbo. */
unsigned sf_opp_top(const struct sf_cluster_opps *c);
/* Index of the hardware P-state, or -1. */
int sf_opp_index(const struct sf_cluster_opps *c, unsigned pstate);
/*
 * Index of the state to hold while nothing vouches for the die temperature: the
 * fastest state whose power is at most a third of the fastest (non-turbo) state's.
 */
unsigned sf_opp_fallback(const struct sf_cluster_opps *c);

struct sf_gov_tunables {
	unsigned up_permille;   /* at or above this utilisation: straight to the top */
	unsigned margin_permille; /* headroom over the measured need: 1250 is schedutil's 1.25 */
	unsigned down_hold;     /* consecutive samples asking for less before stepping down */
};

struct sf_gov {
	unsigned index;         /* current operating point (index into opp[]) */
	unsigned lower_votes;
};

/*
 * One sample. util_permille is the utilisation of the cluster's busiest processor over
 * the sample, measured at the current operating point. cap is the highest index allowed
 * now (thermal and user limits already combined). Returns the index to run at; raises
 * at once, lowers only after down_hold samples in a row asked for less, and never
 * returns more than cap.
 */
unsigned sf_gov_sample(const struct sf_cluster_opps *c, const struct sf_gov_tunables *t,
    struct sf_gov *g, unsigned util_permille, unsigned cap);

#ifdef __cplusplus
}
#endif
#endif
