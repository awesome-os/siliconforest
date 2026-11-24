// SPDX-License-Identifier: MIT
// Edge inputs for the kernel model under -fsanitize=undefined,address, C only, every
// buffer of the exact size on the heap: extreme samples in every combination, calls
// of every length, long idle times, rate changes, calls after a fault.
#include <stdio.h>
#include <stdlib.h>
#include "../kernel/sf_speaker_model.h"

static uint64_t rng = 88172645463325252ULL;
static uint64_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }

int main(void)
{
	static const int16_t ext[] = { -32768, -32767, -1, 0, 1, 32767 };
	struct sf_spk_model *m = malloc(sizeof(*m));
	unsigned long calls = 0, faults = 0;

	if (sf_spk_model_init(m, 48000, 0x10) != -1 || sf_spk_model_init(m, 96000, 0x0f) != -1 || sf_spk_model_init(m, 0, 0x0f) != -1)
		return 1;
	/* constant extreme pairs, long enough to fault or to settle */
	for (unsigned a = 0; a < 6; a++) for (unsigned b = 0; b < 6; b++) for (unsigned rate = 0; rate < 2; rate++) {
		if (sf_spk_model_init(m, rate ? 48000 : 44100, 0x0f) != 0) return 1;
		for (unsigned call = 0; call < 3000; call++) {
			unsigned n = 1 + (unsigned)(rnd() % 2048);
			int16_t *buf = malloc(n * SF_SPK_MODEL_CHANNELS * sizeof(int16_t));
			for (unsigned i = 0; i < n * SF_SPK_MODEL_CHANNELS; i++)
				buf[i] = (i & 1) ? ext[a] : ext[b];
			int f = sf_spk_model_run(m, buf, n);
			free(buf);
			calls++;
			(void)sf_spk_model_limit_steps(m);
			for (unsigned s = 0; s < 6; s++) { (void)sf_spk_model_coil_mc(m, s); (void)sf_spk_model_magnet_mc(m, s); }
			if (f) { faults++; if (call > 2990) break; }
			if (call % 97 == 0) sf_spk_model_idle(m, rnd() % 3000000000ULL);
			if (call % 211 == 0) sf_spk_model_set_rate(m, (rnd() & 1) ? 48000 : 44100);
		}
	}
	/* random extremes with short streams and pauses, so that no power window completes */
	for (unsigned round = 0; round < 200; round++) {
		if (sf_spk_model_init(m, (round & 1) ? 48000 : 44100, 0x0f) != 0) return 1;
		for (unsigned call = 0; call < 2000; call++) {
			unsigned n = (unsigned)(rnd() % 1500);
			int16_t *buf = malloc((n ? n : 1) * SF_SPK_MODEL_CHANNELS * sizeof(int16_t));
			for (unsigned i = 0; i < n * SF_SPK_MODEL_CHANNELS; i++)
				buf[i] = (rnd() & 3) ? ext[rnd() % 6] : (int16_t)rnd();
			(void)sf_spk_model_run(m, buf, n);
			free(buf);
			calls++;
			sf_spk_model_idle(m, rnd() % 200000000ULL);
			(void)sf_spk_model_limit_steps(m);
		}
	}
	/* long idle times: an hour in one call, a day in 100 ms slices, zero */
	if (sf_spk_model_init(m, 48000, 0x0f) != 0) return 1;
	sf_spk_model_idle(m, 0);
	sf_spk_model_idle(m, 3600ULL * 1000000000ULL);
	for (unsigned i = 0; i < 864000; i++) sf_spk_model_idle(m, 100000000ULL);
	printf("san-edges: %lu calls, %lu returned a fault, coil %d mC magnet %d mC after a day idle, limit %u: no sanitizer report\n",
	    calls, faults, sf_spk_model_coil_mc(m, 0), sf_spk_model_magnet_mc(m, 0), sf_spk_model_limit_steps(m));
	free(m);
	return 0;
}
