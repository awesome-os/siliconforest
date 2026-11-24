/* SPDX-License-Identifier: MIT */
/*
 * Voice-coil temperature model of the J293's speakers, in the kernel.
 *
 * The model, its equations and its limits are those of the Asahi Linux speaker safety
 * daemon, speakersafetyd 2.0.1 (https://github.com/AsahiLinux/speakersafetyd, MIT,
 * Copyright The Asahi Linux Contributors): src/types.rs, Speaker::new, run_model and
 * skip_model. The parameters are that project's conf/apple/j293.conf (MIT), carried as
 * data in sf_speaker_model.c. ../safetyd holds a port of the daemon itself, kept as the
 * reference this file is checked against on the host (../model-test).
 *
 * This file is plain C without floating point and without any kernel interface, so the
 * same object runs in the kext and in the host tests. The daemon works in periods of 4096
 * frames and this code is called with any number of frames, so it keeps the daemon's
 * period grid (a count of all frames ever played) and is built to be at least as hot and
 * at least as limiting as the daemon at every one of its period ends, whatever the calls:
 *   - the ambient temperature is SF_SPK_MODEL_BIAS_MC higher, products are rounded up;
 *   - the daemon cools for a pause only when its period took more than four periods of
 *     time, and then before the whole period's frames. So the heat of the frames since
 *     the last period boundary is kept apart and not cooled, and a pause cools the rest
 *     only once more than three periods of pause have gathered since that boundary;
 *   - the daemon runs a whole period with the coefficients of the sample rate in force at
 *     its end, so a change of rate inside a period runs that period's frames again;
 *     and the pause that cools is counted in periods of the slower rate;
 *   - while the speakers do not play, the coil cools towards the magnet temperature of
 *     the start of each 1024-sample block instead of the falling one;
 *   - the hysteresis may fall no lower than the highest temperature of the last 4096
 *     frames plus t_hysteresis (the daemon looks once per period), and not at all
 *     during a pause;
 *   - the mean power is judged over every run of four 1024-frame blocks on that grid,
 *     which contains each of the daemon's periods, and a pause does not restart it;
 *   - the gain limit is rounded to the next half dB down, and any reduction at all costs
 *     at least one step (the daemon ignores reductions under 0.01 dB and rounds to the
 *     nearest step).
 * ../model-test compares the two on the host. The caller serialises all calls.
 */
#ifndef _SF_SPEAKER_MODEL_H
#define _SF_SPEAKER_MODEL_H

#include <stdint.h>

#define SF_SPK_MODEL_SPEAKERS   4
#define SF_SPK_MODEL_CHANNELS   8       /* per frame: current, voltage of each speaker */
#define SF_SPK_MODEL_BIAS_MC    50      /* added to the ambient temperature, millidegrees */
#define SF_SPK_MODEL_WINDOW     4096    /* frames over which the mean power is judged, the daemon's period */

/* why run() says the amplifiers must be shut down */
enum {
	kSFSpkModelOk = 0,
	kSFSpkModelCoilTooHot,          /* above t_limit + t_headroom */
	kSFSpkModelMagnetTooHot,
	kSFSpkModelNegativePower,       /* mean power of a window below -0.01 W: the sense data is wrong */
};

#define SF_SPK_MODEL_BLOCK      1024    /* frames; four blocks are one period of the daemon */

struct sf_spk_model_speaker {
	/*
	 * Degrees in Q23.40. The temperature is base + heat: `base` is the state at the last
	 * period boundary carried on without power, `heat` what the frames since then added.
	 */
	int64_t base_coil, base_magnet, heat_coil, heat_magnet;
	int64_t start_coil, start_magnet;       /* the base without the frames since the boundary */
	int64_t t_coil_hyst, t_magnet_hyst;
	int64_t max_coil[4], max_magnet[4];     /* highest temperature in each of the last four blocks */
	int64_t cur_max_coil, cur_max_magnet;   /* and in the open one */
	int64_t sum[4], cur_sum;                /* sums of voltage * current sample products, likewise */
	int32_t power_mw;                       /* mean power of the last four complete blocks, milliwatts */
};

struct sf_spk_model {
	uint32_t rate;
	int64_t  alpha_coil, alpha_magnet;      /* per-sample filter coefficients, Q0.40 */
	int64_t  block_coil, block_magnet;      /* (1 - alpha)^1024, Q0.40, rounded up: idle cooling */
	uint32_t frame_in_block;                /* frames in the open block */
	uint32_t block_in_period;               /* complete blocks since the last period boundary, 0..3 */
	uint32_t blocks;                        /* complete blocks so far, saturating at 4 */
	uint64_t pause_ns;                      /* pause gathered since the last period boundary */
	uint64_t idle_ns;                       /* of that, time that is to cool and has not yet */
	int      cooling;                       /* the pause since the boundary is long enough to cool */
	int      fault;                         /* kSFSpkModel..., sticky */
	unsigned fault_speaker;
	struct sf_spk_model_speaker spk[SF_SPK_MODEL_SPEAKERS];
	/* the sample products of the frames since the last period boundary, for a change of rate */
	int32_t  product[SF_SPK_MODEL_WINDOW][SF_SPK_MODEL_SPEAKERS];
};

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cold start, as the daemon after a boot: coil one degree under the temperature at
 * which limiting starts. `amp_gain_reg` must be the gain the parameters were derived
 * for (0x0f, 18.5 dBV); anything else, or a rate other than 44100 or 48000, returns -1.
 */
int sf_spk_model_init(struct sf_spk_model *m, uint32_t rate, uint32_t amp_gain_reg);
/* A change of sample rate between sessions; the temperatures are kept. -1 for an unsupported rate. */
int sf_spk_model_set_rate(struct sf_spk_model *m, uint32_t rate);
/*
 * `frames` frames of SF_SPK_MODEL_CHANNELS signed 16-bit samples captured while the
 * speakers played. Returns kSFSpkModelOk or the reason the amplifiers must be shut down
 * (which then stays until the next init).
 */
int sf_spk_model_run(struct sf_spk_model *m, const int16_t *sense, unsigned frames);
/* `ns` nanoseconds passed without playback: the speakers cool. */
void sf_spk_model_idle(struct sf_spk_model *m, uint64_t ns);
/*
 * How far every speaker's volume must be below 0 dB now, in half-dB steps (0: no
 * limit). All speakers are one group, as in j293.conf: the hottest decides.
 */
unsigned sf_spk_model_limit_steps(const struct sf_spk_model *m);
/* Temperatures in millidegrees, for display. */
int32_t sf_spk_model_coil_mc(const struct sf_spk_model *m, unsigned speaker);
int32_t sf_spk_model_magnet_mc(const struct sf_spk_model *m, unsigned speaker);

#ifdef __cplusplus
}
#endif

#endif
