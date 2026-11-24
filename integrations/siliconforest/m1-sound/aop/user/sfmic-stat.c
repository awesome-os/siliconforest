/* SPDX-License-Identifier: MIT */
/*
 * sfmic-stat: reads the built-in microphones from /dev/sfmic (SFAopAudio.kext) and prints
 * level statistics per microphone.
 *
 * The device delivers what the hardware does: 48000 frames per second of three 32-bit
 * little-endian floats (12 bytes per frame), one per microphone. Opening it switches the
 * microphones on, closing it switches them off.
 *
 *   sfmic-stat [-s <seconds>] [-i <interval ms>] [-x <frames>] [-o <frame>] [-k <skip ms>]
 *     -s  how long to read (default 3)
 *     -i  one line per microphone every <interval> ms (default 1000; 0: only the total)
 *     -x  print <frames> frames as raw hexadecimal words and as numbers, starting at
 *     -o  frame <frame> of the stream (default 0); -x and -o may be given up to four times
 *     -k  leave the first <skip> ms out of the total (default 0; the firmware's filters
 *         settle for about 1.2 s after the microphones are switched on)
 * Lines:
 *   SFMIC int   <t ms> mic<n> mean=.. rms=.. (dBFS) peak=.. (dBFS)      per interval
 *   SFMIC total mic<n> frames=.. mean=.. rms=.. peak=.. min=.. max=.. zeros=.. repeats=..
 *               nonfinite=.. negative=.. distinct=<yes|no>
 *   SFMIC pair  <a>/<b> differ=<frames> corr=<correlation coefficient>
 *   SFMIC verdict <ok|NOT OK ...>   not ok when the read ended early, a microphone's
 *               samples are constant or not numbers, a microphone has no noise floor at
 *               all (peak below 1e-20 after the skipped part), or two microphones deliver
 *               identical samples
 * dBFS is relative to 1.0. No libm: the square root and the logarithm are local.
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHANNELS 3
#define RATE     48000

static double
my_sqrt(double x)
{
	double scale = 1;

	if (x <= 0)
		return 0;
	/* bring x into [1, 4) so that Newton's iteration converges in a few steps */
	while (x < 1) { x *= 4; scale *= 0.5; }
	while (x >= 4) { x *= 0.25; scale *= 2; }
	double r = x;
	for (int i = 0; i < 30; i++)
		r = 0.5 * (r + x / r);
	return r * scale;
}

/* log10 by range reduction to [1, 2) and the series of atanh */
static double
my_log10(double x)
{
	int e = 0;

	if (x <= 0)
		return -99.0;
	while (x >= 2) { x *= 0.5; e++; }
	while (x < 1) { x *= 2; e--; }
	double z = (x - 1) / (x + 1), z2 = z * z, term = z, ln = 0;
	for (int k = 1; k < 40; k += 2) {
		ln += term / k;
		term *= z2;
	}
	return (2 * ln + e * 0.6931471805599453) / 2.302585092994046;
}

static double db(double v) { return v > 0 ? 20 * my_log10(v) : -999.0; }

struct acc {
	double sum, sq, min, max, peak;
	uint64_t n, zeros, repeats, nonfinite;
	uint32_t last;
	int have;
};

static void
acc_add(struct acc *a, uint32_t bits)
{
	float f;
	double v;

	memcpy(&f, &bits, 4);
	if (((bits >> 23) & 0xff) == 0xff) {
		a->nonfinite++;
		f = 0;
	}
	v = f;
	if (!a->have) {
		a->min = a->max = v;
		a->have = 1;
	} else if (bits == a->last) {
		a->repeats++;
	}
	a->last = bits;
	if ((bits << 1) == 0)
		a->zeros++;
	a->sum += v;
	a->sq += v * v;
	if (v < a->min) a->min = v;
	if (v > a->max) a->max = v;
	if (v > a->peak) a->peak = v;
	if (-v > a->peak) a->peak = -v;
	a->n++;
}

int
main(int argc, char **argv)
{
	int seconds = 3, interval = 1000, skip = 0, c, ndump = 0;
	uint64_t dumpAt[4] = { 0, 0, 0, 0 }, dumpLen[4] = { 0, 0, 0, 0 }, negative[CHANNELS] = { 0, 0, 0 };
	static uint32_t buf[CHANNELS * 1024];
	struct acc total[CHANNELS], part[CHANNELS];
	double cross[3] = { 0, 0, 0 };
	uint64_t differ[3] = { 0, 0, 0 }, frames = 0, want, partFrames = 0, counted = 0;

	while ((c = getopt(argc, argv, "s:i:x:k:o:")) != -1) {
		switch (c) {
		case 's': seconds = atoi(optarg); break;
		case 'i': interval = atoi(optarg); break;
		case 'x': if (ndump < 4) dumpLen[ndump++] = (uint64_t)atoi(optarg); break;
		case 'o': if (ndump < 4) dumpAt[ndump] = (uint64_t)atoi(optarg); break;
		case 'k': skip = atoi(optarg); break;
		default:
			fprintf(stderr, "usage: sfmic-stat [-s seconds] [-i interval ms] [-o frame] [-x frames] [-k skip ms]\n");
			return 2;
		}
	}
	if (seconds < 1 || seconds > 3600 || interval < 0 || skip < 0)
		return 2;
	memset(total, 0, sizeof(total));
	memset(part, 0, sizeof(part));
	int fd = open("/dev/sfmic", O_RDONLY);
	if (fd < 0) {
		printf("SFMIC verdict cannot open /dev/sfmic: %s\n", strerror(errno));
		return 1;
	}
	want = (uint64_t)seconds * RATE;
	uint64_t every = (uint64_t)interval * RATE / 1000, skipFrames = (uint64_t)skip * RATE / 1000;
	size_t have = 0;

	while (frames < want) {
		ssize_t n = read(fd, (char *)buf + have, sizeof(buf) - have);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0) {
			printf("SFMIC read ended after %llu frames: %s\n", (unsigned long long)frames,
			    n < 0 ? strerror(errno) : "end of stream");
			break;
		}
		have += (size_t)n;
		size_t whole = have / (CHANNELS * 4);
		for (size_t f = 0; f < whole && frames < want; f++, frames++) {
			const uint32_t *fr = buf + f * CHANNELS;

			for (int d = 0; d < ndump; d++) {
				if (frames >= dumpAt[d] && frames < dumpAt[d] + dumpLen[d]) {
					float v[CHANNELS];
					memcpy(v, fr, sizeof(v));
					printf("SFMIC raw %6llu %08x %08x %08x  %+.9e %+.9e %+.9e\n", (unsigned long long)frames,
					    fr[0], fr[1], fr[2], (double)v[0], (double)v[1], (double)v[2]);
				}
			}
			for (int ch = 0; ch < CHANNELS; ch++)
				negative[ch] += fr[ch] >> 31;
			for (int ch = 0; ch < CHANNELS; ch++)
				acc_add(&part[ch], fr[ch]);
			partFrames++;
			if (frames >= skipFrames) {
				float a, b, d;
				for (int ch = 0; ch < CHANNELS; ch++)
					acc_add(&total[ch], fr[ch]);
				memcpy(&a, &fr[0], 4); memcpy(&b, &fr[1], 4); memcpy(&d, &fr[2], 4);
				cross[0] += (double)a * b; cross[1] += (double)a * d; cross[2] += (double)b * d;
				differ[0] += fr[0] != fr[1]; differ[1] += fr[0] != fr[2]; differ[2] += fr[1] != fr[2];
				counted++;
			}
			if (every != 0 && partFrames == every) {
				for (int ch = 0; ch < CHANNELS; ch++) {
					double rms = my_sqrt(part[ch].sq / (double)part[ch].n);
					printf("SFMIC int %6llu mic%d mean=%+.6f rms=%.6f (%.1f dBFS) peak=%.6f (%.1f dBFS)\n",
					    (unsigned long long)((frames + 1) * 1000 / RATE), ch, part[ch].sum / (double)part[ch].n,
					    rms, db(rms), part[ch].peak, db(part[ch].peak));
				}
				fflush(stdout);
				memset(part, 0, sizeof(part));
				partFrames = 0;
			}
		}
		size_t used = whole * CHANNELS * 4;
		memmove(buf, (char *)buf + used, have - used);
		have -= used;
	}
	close(fd);

	int ok = counted > 0, alive = 1, differs = 1, silent = 0;
	for (int ch = 0; ch < CHANNELS && counted > 0; ch++) {
		struct acc *a = &total[ch];
		double mean = a->sum / (double)a->n, rms = my_sqrt(a->sq / (double)a->n);
		int distinct = a->max > a->min && a->repeats < a->n / 2;

		printf("SFMIC total mic%d frames=%llu mean=%+.6f rms=%.6f (%.1f dBFS) peak=%.6f (%.1f dBFS) min=%+.6f "
		    "max=%+.6f zeros=%llu repeats=%llu nonfinite=%llu negative=%llu distinct=%s\n", ch, (unsigned long long)a->n, mean,
		    rms, db(rms), a->peak, db(a->peak), a->min, a->max, (unsigned long long)a->zeros,
		    (unsigned long long)a->repeats, (unsigned long long)a->nonfinite, (unsigned long long)negative[ch],
		    distinct ? "yes" : "no");
		if (!distinct || a->nonfinite != 0)
			alive = 0;
		/*
		 * A microphone always has a noise floor. With its data line held constant, the
		 * firmware's filters decay to the smallest normal floats (1e-38) within 1.5 s.
		 */
		if (a->peak < 1e-20)
			silent = 1;
	}
	static const int pa[3] = { 0, 0, 1 }, pb[3] = { 1, 2, 2 };
	for (int p = 0; p < 3 && counted > 0; p++) {
		double ra = my_sqrt(total[pa[p]].sq), rb = my_sqrt(total[pb[p]].sq);
		double corr = ra > 0 && rb > 0 ? cross[p] / (ra * rb) : 0;

		printf("SFMIC pair %d/%d differ=%llu of %llu corr=%+.4f\n", pa[p], pb[p], (unsigned long long)differ[p],
		    (unsigned long long)counted, corr);
		if (differ[p] < counted / 2)
			differs = 0;
	}
	if (frames < want)
		ok = 0;
	printf("SFMIC verdict %s%s%s%s%s\n", ok && alive && differs && !silent ? "ok" : "NOT OK", ok ? "" : " short-read",
	    alive ? "" : " constant-or-invalid-data", silent ? " numerically-silent(no-noise-floor)" : "",
	    differs ? "" : " microphones-identical");
	return ok && alive && differs && !silent ? 0 : 1;
}
