// SPDX-License-Identifier: MIT
/*
 * Host stand-in for the decoder and the OS under sf_avd_core.c: three register areas in
 * memory, a deterministic IOVA allocator, and a "decoder" that answers the two mailbox
 * messages of a frame (VP done, PP done) as soon as the END command of a job was written.
 * Every write to the VP instruction FIFO and to the submit register is appended to a log,
 * which is what the host tests compare.
 */
#include "host_os.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "../kernel/sf_avd_core.h"

uint8_t host_code[0xc000];
uint8_t host_mbox[0x4000];
uint8_t host_ctrl[0x10000];

static FILE *g_log;
static int g_verbose;
static uint64_t g_iova = 0x10000000ull;
static int g_end_seen, g_submit_seen;
static unsigned g_live_allocs;
static size_t g_live_bytes, g_peak_bytes;

struct host_dma { void *cpu; size_t bytes; };

void host_set_log(FILE *f, int verbose) { g_log = f; g_verbose = verbose; }
unsigned host_live_allocs(void) { return g_live_allocs; }
size_t host_peak_bytes(void) { return g_peak_bytes; }

void *sf_avd_os_zalloc(size_t bytes) { return calloc(1, bytes ? bytes : 1); }
void sf_avd_os_free(void *p) { free(p); }

void sf_avd_os_log(int level, const char *fmt, ...)
{
	va_list ap;

	if (level > g_verbose)
		return;
	va_start(ap, fmt);
	fprintf(stderr, "avd[%d]: ", level);
	vfprintf(stderr, fmt, ap);
	fputc('\n', stderr);
	va_end(ap);
}

void sf_avd_os_usleep(uint32_t us) { (void)us; }
uint64_t sf_avd_os_now_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + ts.tv_nsec / 1000;
}

static int in(const void *a, const uint8_t *base, size_t len, uint32_t *off)
{
	const uint8_t *p = a;

	if (p < base || p >= base + len)
		return 0;
	*off = (uint32_t)(p - base);
	return 1;
}

uint32_t sf_avd_os_readl(const void *addr)
{
	uint32_t off, v;

	if (in(addr, host_mbox, sizeof(host_mbox), &off)) {
		if (off == 0x90)      /* AVD_REG_FLAG0_SET: the CM3 has booted */
			return 1;
		memcpy(&v, host_mbox + off, 4);
		return v;
	}
	if (in(addr, host_ctrl, sizeof(host_ctrl), &off)) {
		/* submit queue: max 16 entries, never filling */
		if (off >= 0x4018 && off < 0x402c)
			return 16;
		if (off >= 0x402c && off < 0x4040)
			return 0;
		memcpy(&v, host_ctrl + off, 4);
		return v;
	}
	fprintf(stderr, "host_os: read outside the register areas\n");
	abort();
}

void sf_avd_os_writel(uint32_t value, void *addr)
{
	uint32_t off;

	if (in(addr, host_mbox, sizeof(host_mbox), &off)) {
		memcpy(host_mbox + off, &value, 4);
		return;
	}
	if (in(addr, host_ctrl, sizeof(host_ctrl), &off)) {
		memcpy(host_ctrl + off, &value, 4);
		if (off >= 0x4004 && off < 0x4014) {         /* VP instruction FIFOs */
			if (g_log)
				fprintf(g_log, "vp%u %08x\n", (off - 0x4004) / 4, value);
			if ((value >> 24) == 0x2b && (value & (1u << 10)))
				g_end_seen = 1;
		} else if (off == 0x4014) {                  /* PP submit */
			if (g_log)
				fprintf(g_log, "pp %08x\n", value);
			g_submit_seen = 1;
		} else if (g_log) {
			fprintf(g_log, "reg %04x %08x\n", off, value);
		}
		return;
	}
	fprintf(stderr, "host_os: write outside the register areas\n");
	abort();
}

void sf_avd_os_memcpy_toio(void *to, const void *from, size_t bytes) { memcpy(to, from, bytes); }

void *sf_avd_os_lock_alloc(void) { return calloc(1, 1); }
void sf_avd_os_lock_free(void *lock) { free(lock); }
unsigned long sf_avd_os_lock(void *lock) { (void)lock; return 0; }
void sf_avd_os_unlock(void *lock, unsigned long flags) { (void)lock; (void)flags; }

void *sf_avd_os_dma_alloc(size_t bytes, uint64_t *iova, void **cookie)
{
	struct host_dma *d = calloc(1, sizeof(*d));
	size_t rounded = (bytes + 0x3fff) & ~(size_t)0x3fff;

	d->cpu = calloc(1, rounded);
	d->bytes = rounded;
	*iova = g_iova;
	g_iova += rounded;
	*cookie = d;
	g_live_allocs++;
	g_live_bytes += rounded;
	if (g_live_bytes > g_peak_bytes)
		g_peak_bytes = g_live_bytes;
	return d->cpu;
}

void sf_avd_os_dma_free(void *cookie)
{
	struct host_dma *d = cookie;

	g_live_allocs--;
	g_live_bytes -= d->bytes;
	free(d->cpu);
	free(d);
}

static int g_wakes;
static int g_fail_next;

void host_fail_next_job(void) { g_fail_next = 1; }

void sf_avd_os_job_wake(struct sf_avd_device *dev) { (void)dev; g_wakes++; }

bool sf_avd_os_job_wait(struct sf_avd_device *dev, uint32_t timeout_ms)
{
	uint32_t word;

	(void)timeout_ms;
	if (g_wakes > 0) {
		g_wakes--;
		return true;
	}
	if (!g_end_seen || g_fail_next) {
		g_fail_next = 0;
		g_end_seen = 0;
		return false;           /* the decoder never answers: the watchdog's case */
	}
	g_end_seen = 0;
	g_submit_seen = 0;
	word = 0x100;                   /* a VP is done */
	memcpy(host_mbox + 0x64, &word, 4);
	sf_avd_irq(dev);
	if (!g_submit_seen)
		return false;
	word = 0x1000;                  /* the PP is done */
	memcpy(host_mbox + 0x64, &word, 4);
	sf_avd_irq(dev);
	if (g_wakes > 0) {
		g_wakes--;
		return true;
	}
	return false;
}

int sf_avd_os_reset(struct sf_avd_device *dev)
{
	(void)dev;
	if (g_log)
		fprintf(g_log, "reset\n");
	return 0;
}
