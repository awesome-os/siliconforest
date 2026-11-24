// SPDX-License-Identifier: MIT
/*
 * Voice-coil temperature model of the J293's speakers. See sf_speaker_model.h.
 *
 * The equations are speakersafetyd's (src/types.rs, MIT, Copyright The Asahi Linux
 * Contributors); per sample, with v and i the sensed voltage and current:
 *     p        = v * i
 *     t_coil   += (t_magnet  + p * tr_coil   - t_coil)   * alpha_coil
 *     t_magnet += (t_ambient + p * tr_magnet - t_magnet) * alpha_magnet
 *     alpha    = (1 / rate) / (tau + 1 / rate)
 * and per evaluation
 *     hyst      = min(max(hyst, t), t + t_hysteresis)       for coil and magnet
 *     reduction = (max(hyst_coil, hyst_magnet) - (t_limit - t_window)) / t_window
 *     gain      = min_gain * max(reduction, 0)               in dB
 *     min_gain  = 10 log10(max_power / peak_power)
 *     max_power = (t_limit - t_ambient) / (tr_coil + tr_magnet)
 *     peak_power = 2 * 10^(amp_gain_dBV / 10) / z_nominal
 */
#include "sf_speaker_model.h"

/*
 * conf/apple/j293.conf of speakersafetyd 2.0.1 (MIT), the same for all four speakers,
 * which form one group. Units here: millidegrees, milliseconds, and so on.
 *   [Globals] t_ambient = 50.0, t_hysteresis = 5.0, t_window = 20.0
 *   [Speaker/...] tr_coil = 38.30, tr_magnet = 49.10, tau_coil = 2.80, tau_magnet = 79.70,
 *               t_limit = 130.0, t_headroom = 10.0, z_nominal = 9.70,
 *               is_scale = 3.75, vs_scale = 14
 *   is_chan/vs_chan: left front 0/1, right front 2/3, left rear 4/5, right rear 6/7
 */
#define T_AMBIENT_MC    50000
#define T_HYSTERESIS_MC 5000
#define T_WINDOW_MC     20000
#define T_LIMIT_MC      130000
#define T_HEADROOM_MC   10000
#define TR_COIL_MK_W    38300           /* millikelvin per watt */
#define TR_MAGNET_MK_W  49100
#define TAU_COIL_MS     2800
#define TAU_MAGNET_MS   79700
#define IS_SCALE_MA     3750            /* amperes at full scale, in mA */
#define VS_SCALE_MV     14000
/*
 * min_gain for the amplifier gain 18.5 dBV (register 0x0f) and z_nominal 9.70 ohm:
 * max_power = 80 / 87.4 = 0.91533 W, peak_power = 2 * 10^1.85 / 9.7 = 14.5968 W,
 * 10 log10 of the ratio = -12.0268 dB (the daemon logs "Min gain: -12.03 dB").
 * In hundredths of a dB, magnitude rounded up.
 */
#define AMP_GAIN_REG    0x0f
#define MIN_GAIN_CB     1203

#define Q               40
#define ONE             ((int64_t)1 << Q)
/* millidegrees to Q23.40; everything here stays within 64 bits except the products in mul_up() */
#define DEG(mc)         ((((int64_t)(mc)) << Q) / 1000)
/* milliwatts that the product of two full-scale samples (2^30) stands for */
#define FULL_SCALE_MW   ((int64_t)VS_SCALE_MV * IS_SCALE_MA / 1000)

/* a * b >> Q, rounded towards plus infinity */
static int64_t
mul_up(int64_t a, int64_t b)
{
	__int128 p = (__int128)a * b;
	return (int64_t)((p + (ONE - 1)) >> Q);
}

/* 1 / (tau * rate + 1) in Q0.40; tau * rate is an integer for both rates */
static int64_t
alpha(uint32_t tau_ms, uint32_t rate)
{
	uint64_t n = (uint64_t)tau_ms * rate / 1000 + 1;
	return (int64_t)(((uint64_t)ONE + n / 2) / n);
}

/* (1 - a)^1024 in Q0.40, rounded up at every squaring */
static int64_t
block_factor(int64_t a)
{
	int64_t f = ONE - a;
	for (int i = 0; i < 10; i++)
		f = mul_up(f, f);
	return f;
}

static void
set_coefficients(struct sf_spk_model *m, uint32_t rate)
{
	m->rate = rate;
	m->alpha_coil = alpha(TAU_COIL_MS, rate);
	m->alpha_magnet = alpha(TAU_MAGNET_MS, rate);
	m->block_coil = block_factor(m->alpha_coil);
	m->block_magnet = block_factor(m->alpha_magnet);
}

static int64_t
coil_of(const struct sf_spk_model_speaker *s)
{
	return s->base_coil + s->heat_coil;
}

static int64_t
magnet_of(const struct sf_spk_model_speaker *s)
{
	return s->base_magnet + s->heat_magnet;
}

/* One frame of one speaker with the product p of its two samples. */
static void
step(struct sf_spk_model *m, struct sf_spk_model_speaker *s, int64_t p)
{
	/*
	 * A product of two samples is at most 2^30 and stands for vs_scale * is_scale
	 * watts at 2^30. Degrees per unit of product, in Q.40 (the J293 values give
	 * 2059008 and 2639616 exactly); a positive product is scaled by one unit more,
	 * so heating is never understated and cooling by negative products never overstated.
	 */
	const int64_t k_coil = (((int64_t)VS_SCALE_MV * IS_SCALE_MA * TR_COIL_MK_W) << (Q - 30)) / 1000000000;
	const int64_t k_magnet = (((int64_t)VS_SCALE_MV * IS_SCALE_MA * TR_MAGNET_MK_W) << (Q - 30)) / 1000000000;
	const int64_t amb = DEG(T_AMBIENT_MC + SF_SPK_MODEL_BIAS_MC);

	/* the two parts of the one linear system: the base without power, the heat without ambient */
	s->base_coil += mul_up(s->base_magnet - s->base_coil, m->alpha_coil);
	s->base_magnet += mul_up(amb - s->base_magnet, m->alpha_magnet);
	s->heat_coil += mul_up(s->heat_magnet + p * (k_coil + (p > 0)) - s->heat_coil, m->alpha_coil);
	s->heat_magnet += mul_up(p * (k_magnet + (p > 0)) - s->heat_magnet, m->alpha_magnet);
}

/* Note the temperature after a frame; nonzero when it is beyond the limit. */
static int
note(struct sf_spk_model *m, unsigned i)
{
	const int64_t too_hot = DEG(T_LIMIT_MC + T_HEADROOM_MC);
	struct sf_spk_model_speaker *s = &m->spk[i];
	int64_t coil = coil_of(s), magnet = magnet_of(s);

	if (coil > s->cur_max_coil)
		s->cur_max_coil = coil;
	if (magnet > s->cur_max_magnet)
		s->cur_max_magnet = magnet;
	if (coil <= too_hot && magnet <= too_hot)
		return 0;
	if (m->fault == kSFSpkModelOk) {
		m->fault = coil > too_hot ? kSFSpkModelCoilTooHot : kSFSpkModelMagnetTooHot;
		m->fault_speaker = i;
	}
	return 1;
}

int
sf_spk_model_set_rate(struct sf_spk_model *m, uint32_t rate)
{
	unsigned frames = m->block_in_period * SF_SPK_MODEL_BLOCK + m->frame_in_block;

	if (rate != 44100 && rate != 48000)
		return -1;
	if (rate == m->rate)
		return 0;
	set_coefficients(m, rate);
	/* the frames since the period boundary again, as the daemon will run them: at this rate */
	for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
		struct sf_spk_model_speaker *s = &m->spk[i];

		s->base_coil = s->start_coil;
		s->base_magnet = s->start_magnet;
		s->heat_coil = 0;
		s->heat_magnet = 0;
		for (unsigned f = 0; f < frames; f++) {
			step(m, s, m->product[f][i]);
			(void)note(m, i);
		}
	}
	return 0;
}

int
sf_spk_model_init(struct sf_spk_model *m, uint32_t rate, uint32_t amp_gain_reg)
{
	const int64_t amb = DEG(T_AMBIENT_MC + SF_SPK_MODEL_BIAS_MC);
	/* "warm but not warm enough to limit" */
	const int64_t coil = DEG(T_LIMIT_MC - T_WINDOW_MC - 1000 + SF_SPK_MODEL_BIAS_MC);

	if (amp_gain_reg != AMP_GAIN_REG || (rate != 44100 && rate != 48000))
		return -1;
	set_coefficients(m, rate);
	m->frame_in_block = 0;
	m->block_in_period = 0;
	m->blocks = 0;
	m->pause_ns = 0;
	m->idle_ns = 0;
	m->cooling = 0;
	m->fault = kSFSpkModelOk;
	m->fault_speaker = 0;
	for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
		struct sf_spk_model_speaker *s = &m->spk[i];

		s->base_coil = coil;
		/* t_ambient + (t_coil - t_ambient) * tr_magnet / (tr_magnet + tr_coil), rounded up */
		s->base_magnet = amb + ((coil - amb) * TR_MAGNET_MK_W + (TR_MAGNET_MK_W + TR_COIL_MK_W - 1)) /
		    (TR_MAGNET_MK_W + TR_COIL_MK_W);
		s->start_coil = s->base_coil;
		s->start_magnet = s->base_magnet;
		s->heat_coil = 0;
		s->heat_magnet = 0;
		s->t_coil_hyst = 0;
		s->t_magnet_hyst = 0;
		for (unsigned b = 0; b < 4; b++) {
			s->max_coil[b] = s->base_coil;
			s->max_magnet[b] = s->base_magnet;
			s->sum[b] = 0;
		}
		s->cur_max_coil = s->base_coil;
		s->cur_max_magnet = s->base_magnet;
		s->cur_sum = 0;
		s->power_mw = 0;
	}
	return 0;
}

/* hyst = min(max(hyst, t), highest t of the last four blocks and the open one + t_hysteresis) */
static void
hysteresis(int64_t *hyst, int64_t t, const int64_t max[4], int64_t cur_max)
{
	int64_t top = cur_max;

	for (unsigned b = 0; b < 4; b++)
		if (max[b] > top)
			top = max[b];
	if (*hyst < t)
		*hyst = t;
	if (*hyst > top + DEG(T_HYSTERESIS_MC))
		*hyst = top + DEG(T_HYSTERESIS_MC);
}

/* A block of the grid is complete: its sums and maxima, the power check, the period boundary. */
static int
end_block(struct sf_spk_model *m)
{
	unsigned at = m->block_in_period;

	m->frame_in_block = 0;
	m->block_in_period = (at + 1) % 4;
	if (m->blocks < 4)
		m->blocks++;
	for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
		struct sf_spk_model_speaker *s = &m->spk[i];
		int64_t scaled;

		s->sum[at] = s->cur_sum;
		s->max_coil[at] = s->cur_max_coil;
		s->max_magnet[at] = s->cur_max_magnet;
		s->cur_sum = 0;
		s->cur_max_coil = coil_of(s);
		s->cur_max_magnet = magnet_of(s);
		if (m->blocks < 4)
			continue;
		/* milliwatts = sum / window * full scale / 2^30; the sum is below 2^43, the product below 2^59 */
		scaled = (s->sum[0] + s->sum[1] + s->sum[2] + s->sum[3]) * FULL_SCALE_MW;
		s->power_mw = (int32_t)(scaled / ((int64_t)SF_SPK_MODEL_WINDOW << 30));
		/* "this really shouldn't happen other than rounding error": below -0.01 W */
		if (scaled < -((int64_t)SF_SPK_MODEL_WINDOW << 30) * 10 && m->fault == kSFSpkModelOk) {
			m->fault = kSFSpkModelNegativePower;
			m->fault_speaker = i;
		}
	}
	if (m->block_in_period == 0) {
		/* a period of the daemon ends: the heat joins the base, the pause count starts again */
		for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
			struct sf_spk_model_speaker *s = &m->spk[i];

			s->base_coil += s->heat_coil;
			s->base_magnet += s->heat_magnet;
			s->heat_coil = 0;
			s->heat_magnet = 0;
			s->start_coil = s->base_coil;
			s->start_magnet = s->base_magnet;
		}
		m->pause_ns = 0;
		m->idle_ns = 0;
		m->cooling = 0;
	}
	return m->fault;
}

int
sf_spk_model_run(struct sf_spk_model *m, const int16_t *sense, unsigned frames)
{
	if (m->fault != kSFSpkModelOk)
		return m->fault;
	for (unsigned f = 0; f < frames; f++) {
		unsigned at = m->block_in_period * SF_SPK_MODEL_BLOCK + m->frame_in_block;
		int hot = 0;

		for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
			struct sf_spk_model_speaker *s = &m->spk[i];
			int32_t p = (int32_t)sense[SF_SPK_MODEL_CHANNELS * f + 2 * i + 1] * sense[SF_SPK_MODEL_CHANNELS * f + 2 * i];

			m->product[at][i] = p;
			step(m, s, p);
			s->cur_sum += p;
			hot |= note(m, i);
		}
		if (hot)
			return m->fault;
		if (++m->frame_in_block == SF_SPK_MODEL_BLOCK && end_block(m) != kSFSpkModelOk)
			return m->fault;
	}
	for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
		struct sf_spk_model_speaker *s = &m->spk[i];

		hysteresis(&s->t_coil_hyst, coil_of(s), s->max_coil, s->cur_max_coil);
		hysteresis(&s->t_magnet_hyst, magnet_of(s), s->max_magnet, s->cur_max_magnet);
	}
	return kSFSpkModelOk;
}

void
sf_spk_model_idle(struct sf_spk_model *m, uint64_t ns)
{
	const int64_t amb = DEG(T_AMBIENT_MC + SF_SPK_MODEL_BIAS_MC);
	/* one block is 1024 sample periods */
	const uint64_t block_ns = 1024ULL * 1000000000ULL / m->rate + 1;
	/*
	 * The daemon cools when its period took more than four periods: more than three of
	 * pause, by the rate at the period's end, which may yet become the slower one.
	 */
	const uint64_t hold_ns = 3ULL * SF_SPK_MODEL_WINDOW * 1000000000ULL / 44100 + 1;

	m->pause_ns += ns;
	if (m->pause_ns <= hold_ns)
		return;
	if (!m->cooling) {
		m->cooling = 1;
		m->idle_ns = m->pause_ns;
	} else {
		m->idle_ns += ns;
	}
	while (m->idle_ns >= block_ns) {
		m->idle_ns -= block_ns;
		for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
			struct sf_spk_model_speaker *s = &m->spk[i];

			/* the coil first, towards the magnet as it is before this block's cooling */
			s->base_coil = s->base_magnet + mul_up(s->base_coil - s->base_magnet, m->block_coil);
			s->base_magnet = amb + mul_up(s->base_magnet - amb, m->block_magnet);
			s->start_coil = s->start_magnet + mul_up(s->start_coil - s->start_magnet, m->block_coil);
			s->start_magnet = amb + mul_up(s->start_magnet - amb, m->block_magnet);
		}
	}
}

unsigned
sf_spk_model_limit_steps(const struct sf_spk_model *m)
{
	const int64_t start = DEG(T_LIMIT_MC - T_WINDOW_MC);
	int64_t hottest = 0, over;

	for (unsigned i = 0; i < SF_SPK_MODEL_SPEAKERS; i++) {
		const struct sf_spk_model_speaker *s = &m->spk[i];

		if (s->t_coil_hyst > hottest)
			hottest = s->t_coil_hyst;
		if (s->t_magnet_hyst > hottest)
			hottest = s->t_magnet_hyst;
		/* the hysteresis values are 0 until the first evaluation */
		if (coil_of(s) > hottest)
			hottest = coil_of(s);
		if (magnet_of(s) > hottest)
			hottest = magnet_of(s);
	}
	over = hottest - start;
	if (over <= 0)
		return 0;
	/* gain = MIN_GAIN * over / t_window, in half-dB steps (50 hundredths), rounded up */
	{
		int64_t num = over * MIN_GAIN_CB, den = DEG(T_WINDOW_MC) * 50;
		return (unsigned)((num + den - 1) / den);
	}
}

int32_t
sf_spk_model_coil_mc(const struct sf_spk_model *m, unsigned speaker)
{
	return speaker < SF_SPK_MODEL_SPEAKERS ? (int32_t)((coil_of(&m->spk[speaker]) * 1000) >> Q) : 0;
}

int32_t
sf_spk_model_magnet_mc(const struct sf_spk_model *m, unsigned speaker)
{
	return speaker < SF_SPK_MODEL_SPEAKERS ? (int32_t)((magnet_of(&m->spk[speaker]) * 1000) >> Q) : 0;
}
