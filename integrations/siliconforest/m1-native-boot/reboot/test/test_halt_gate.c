// SPDX-License-Identifier: GPL-2.0
/*
 * Host test of ../sf_halt_gate.h: the SMC write gate of the restart / power-off path and the
 * clock arithmetic of Linux rtc-macsmc.c.
 *   cc -Wall -Werror -I.. -o t test_halt_gate.c && ./t
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "sf_halt_gate.h"

static int
gate32(uint32_t key, uint32_t value, int off)
{
	uint8_t b[4] = { (uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16), (uint8_t)(value >> 24) };
	return sf_smc_halt_gate(key, b, 4, off);
}

static void
put48(uint8_t *p, uint64_t v)
{
	for (int i = 0; i < 6; i++)
		p[i] = (uint8_t)(v >> (8 * i));
}

int
main(void)
{
	const uint32_t MBSE = SF_HALT_KEY('M', 'B', 'S', 'E'), NTAP = SF_HALT_KEY('N', 'T', 'A', 'P');
	uint8_t zero = 0, one = 1, two[2] = { 0, 0 };
	unsigned admitted = 0, cases = 0;

	/* the four values Linux writes */
	assert(gate32(MBSE, SF_HALT_KEY('r', 'e', 's', 't'), 0) == SF_SMC_HALT_OK);
	assert(gate32(MBSE, SF_HALT_KEY('p', 'h', 'r', 'a'), 0) == SF_SMC_HALT_OK);
	assert(gate32(MBSE, SF_HALT_KEY('o', 'f', 'f', 'w'), 0) == SF_SMC_HALT_POWEROFF);
	assert(gate32(MBSE, SF_HALT_KEY('o', 'f', 'f', '1'), 0) == SF_SMC_HALT_POWEROFF);
	assert(gate32(MBSE, SF_HALT_KEY('o', 'f', 'f', 'w'), 1) == SF_SMC_HALT_OK);
	assert(gate32(MBSE, SF_HALT_KEY('o', 'f', 'f', '1'), 1) == SF_SMC_HALT_OK);
	/* byte order: the characters reversed are not the value */
	assert(gate32(MBSE, SF_HALT_KEY('t', 's', 'e', 'r'), 1) == SF_SMC_HALT_VALUE);
	assert(gate32(MBSE, 0, 1) == SF_SMC_HALT_VALUE);
	assert(gate32(MBSE, SF_HALT_KEY('s', 'l', 'p', 'w'), 1) == SF_SMC_HALT_VALUE);     /* Linux's sleep value: not ours */
	assert(sf_smc_halt_gate(MBSE, &zero, 1, 1) == SF_SMC_HALT_LENGTH);
	assert(sf_smc_halt_gate(MBSE, 0, 4, 1) == SF_SMC_HALT_VALUE);
	/* NTAP: one byte, zero */
	assert(sf_smc_halt_gate(NTAP, &zero, 1, 0) == SF_SMC_HALT_OK);
	assert(sf_smc_halt_gate(NTAP, &one, 1, 1) == SF_SMC_HALT_VALUE);
	assert(sf_smc_halt_gate(NTAP, two, 2, 1) == SF_SMC_HALT_LENGTH);
	/* keys other streams write, and the clock's: never from here */
	static const char *const others[] = { "gP0d", "F0Md", "F0Tg", "CLKM", "MBSW", "NTAp", "mBSE", "#KEY", "CHWA", "BCLM" };
	for (unsigned i = 0; i < sizeof(others) / sizeof(others[0]); i++) {
		const char *k = others[i];
		assert(gate32(SF_HALT_KEY(k[0], k[1], k[2], k[3]), SF_HALT_KEY('r', 'e', 's', 't'), 1) == SF_SMC_HALT_KEY);
		assert(sf_smc_halt_gate(SF_HALT_KEY(k[0], k[1], k[2], k[3]), &zero, 1, 1) == SF_SMC_HALT_KEY);
	}
	/* every key that differs from the two in one bit; a sweep of values */
	for (int bit = 0; bit < 32; bit++) {
		assert(gate32(MBSE ^ (1u << bit), SF_HALT_KEY('r', 'e', 's', 't'), 1) == SF_SMC_HALT_KEY);
		assert(sf_smc_halt_gate(NTAP ^ (1u << bit), &zero, 1, 1) == SF_SMC_HALT_KEY);
	}
	uint32_t x = 0x12345678;
	for (unsigned i = 0; i < 4000000; i++) {
		x ^= x << 13; x ^= x >> 17; x ^= x << 5;
		uint32_t v = (i & 3) == 0 ? SF_HALT_KEY('r', 'e', 's', 't') ^ (1u << (x % 32)) : x;
		cases++;
		if (gate32(MBSE, v, 1) == SF_SMC_HALT_OK) {
			admitted++;
			assert(v == SF_HALT_KEY('r', 'e', 's', 't') || v == SF_HALT_KEY('p', 'h', 'r', 'a') ||
			    v == SF_HALT_KEY('o', 'f', 'f', 'w') || v == SF_HALT_KEY('o', 'f', 'f', '1'));
		}
	}

	/* the clock */
	uint8_t ctr[6], off[6];
	int64_t s = 0;
	uint32_t us = 0;
	const int64_t t = 1794500000LL;                 /* a time in November 2026 */
	const uint64_t counter = 123456789ULL << 15 | 0x4000;   /* some seconds since the PMU first ran, plus half a second */
	put48(ctr, counter);
	put48(off, ((uint64_t)t << 15) - (123456789ULL << 15));
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_OK && s == t && us == 500000);
	/* an offset that is "negative" in 48 bits (the counter is ahead of the time) */
	put48(ctr, ((uint64_t)t + 1000) << 15);
	put48(off, (uint64_t)(-(1000LL << 15)) & 0xffffffffffffULL);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_OK && s == t && us == 0);
	/* nothing stored */
	memset(off, 0, sizeof(off));
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_NO_OFFSET);
	/* implausible: 1970 plus the counter, and a negative time */
	put48(ctr, 5000ULL << 15); put48(off, 1);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_IMPLAUSIBLE);
	put48(ctr, 0); put48(off, 0x800000000000ULL);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_IMPLAUSIBLE);
	/* the bounds themselves */
	put48(ctr, 0); put48(off, (uint64_t)SF_RTC_EARLIEST << 15);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_OK && s == SF_RTC_EARLIEST);
	put48(off, ((uint64_t)SF_RTC_EARLIEST << 15) - 1);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_IMPLAUSIBLE);
	put48(off, (uint64_t)SF_RTC_LATEST << 15);
	assert(sf_rtc_time(ctr, off, &s, &us) == SF_RTC_IMPLAUSIBLE);

	printf("HALTGATE PASSED: %u random MBSE values, %u admitted, all among the four; clock arithmetic 8 cases\n", cases, admitted);
	return 0;
}
