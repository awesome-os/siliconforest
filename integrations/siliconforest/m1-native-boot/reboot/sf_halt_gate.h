/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Two small rules of SFAppleReboot.cpp in plain C, so that the host test compiles the same
 * text the kext does (test/test_halt_gate.c):
 *
 *  1. The SMC write gate of the restart / power-off path. Linux macsmc-reboot.c writes two
 *     keys and nothing else: MBSE (four characters: 'rest' or 'offw' as the notification,
 *     'phra' or 'off1' as the action) and NTAP (one byte, 0: no more notifications). The
 *     halt path's writes pass sf_smc_halt_gate() directly before the one writeKey() call it
 *     has; anything else is refused and counted. The two power-off values are admitted only
 *     when power-off was allowed at boot (sfpoweroff=1).
 *
 *  2. The wall-clock time from the SMC's free-running counter and the offset Linux keeps in
 *     the PMU (rtc-macsmc.c macsmc_rtc_get_time(), the 48-bit form used on T8103: key CLKM,
 *     six bytes, 32768 counts a second; NVMEM cell rtc_offset, six bytes at 0xd100 of the
 *     PMU). Both are READ; this system never writes the offset (it cannot set the hardware
 *     clock, and says so).
 */
#ifndef _SF_HALT_GATE_H
#define _SF_HALT_GATE_H

#include <stdint.h>

#define SF_HALT_KEY(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

enum {
	SF_SMC_HALT_OK = 0,
	SF_SMC_HALT_KEY = 1,            /* not MBSE or NTAP */
	SF_SMC_HALT_LENGTH = 2,
	SF_SMC_HALT_VALUE = 3,
	SF_SMC_HALT_POWEROFF = 4,       /* a power-off value without sfpoweroff=1 */
};

/* May `length` bytes at `value` be written to SMC key `key` by the halt path? */
static inline int
sf_smc_halt_gate(uint32_t key, const void *value, uint32_t length, int poweroff_allowed)
{
	const uint8_t *p = (const uint8_t *)value;
	uint32_t v;

	if (p == 0)
		return SF_SMC_HALT_VALUE;
	if (key == SF_HALT_KEY('N', 'T', 'A', 'P')) {
		if (length != 1)
			return SF_SMC_HALT_LENGTH;
		return p[0] == 0 ? SF_SMC_HALT_OK : SF_SMC_HALT_VALUE;
	}
	if (key != SF_HALT_KEY('M', 'B', 'S', 'E'))
		return SF_SMC_HALT_KEY;
	if (length != 4)
		return SF_SMC_HALT_LENGTH;
	/* the value is a 32-bit number in the SMC's (little-endian) byte order */
	v = (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	if (v == SF_HALT_KEY('r', 'e', 's', 't') || v == SF_HALT_KEY('p', 'h', 'r', 'a'))
		return SF_SMC_HALT_OK;
	if (v == SF_HALT_KEY('o', 'f', 'f', 'w') || v == SF_HALT_KEY('o', 'f', 'f', '1'))
		return poweroff_allowed ? SF_SMC_HALT_OK : SF_SMC_HALT_POWEROFF;
	return SF_SMC_HALT_VALUE;
}

/* rtc-macsmc.c */
#define SF_RTC_BYTES            6
#define SF_RTC_SEC_SHIFT        15
#define SF_T8103_RTC_OFFSET     0xd100          /* t8103.dtsi rtc_offset: reg = <0xd100 0x6> */
/* A time outside these is not believed: 1 January 2024 and 1 January 2100, UTC. */
#define SF_RTC_EARLIEST         1704067200LL
#define SF_RTC_LATEST           4102444800LL

enum {
	SF_RTC_OK = 0,
	SF_RTC_NO_OFFSET = 1,           /* all six bytes zero: nothing was ever stored */
	SF_RTC_IMPLAUSIBLE = 2,
};

/*
 * Linux: now = sign_extend64(ctr + off, 47) >> 15, counter and offset each six
 * little-endian bytes. Also gives the fraction of the second in microseconds.
 */
static inline int
sf_rtc_time(const uint8_t ctr6[SF_RTC_BYTES], const uint8_t off6[SF_RTC_BYTES], int64_t *seconds, uint32_t *micros)
{
	uint64_t ctr = 0, off = 0, sum;
	int64_t ext;
	int i;

	for (i = SF_RTC_BYTES - 1; i >= 0; i--) {
		ctr = ctr << 8 | ctr6[i];
		off = off << 8 | off6[i];
	}
	if (off == 0)
		return SF_RTC_NO_OFFSET;
	sum = (ctr + off) & 0xffffffffffffULL;
	ext = (int64_t)(sum << 16) >> 16;               /* sign extend from 48 bits */
	*seconds = ext >> SF_RTC_SEC_SHIFT;
	*micros = (uint32_t)((((uint64_t)ext & 0x7fff) * 1000000ULL) >> SF_RTC_SEC_SHIFT);
	if (*seconds < SF_RTC_EARLIEST || *seconds >= SF_RTC_LATEST)
		return SF_RTC_IMPLAUSIBLE;
	return SF_RTC_OK;
}

#endif
