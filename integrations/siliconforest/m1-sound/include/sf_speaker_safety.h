/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * NOT PROVIDED BY THE KERNEL ANY MORE. The speaker protection model now runs inside the
 * kext (../kernel/sf_speaker_model.c) and nothing in userspace can raise a speaker
 * volume. This header remains as the device contract of ../safetyd, the port of
 * speakersafetyd that is kept as the reference the kernel model is tested against on
 * the host (../model-test); boots sd06 to sd08 ran against a kext that had the device.
 *
 * /dev/sfspeaker-safety: the kernel side of the speaker protection split of the J293
 * (four TAS5770L amplifiers), for the speaker safety daemon and nobody else.
 *
 * It carries what the Linux macaudio machine driver gives speakersafetyd through ALSA:
 *   the "Speaker Volume Unlock" control      SFSPK_IOC_PING
 *   the four "Speaker Playback Volume"s      SFSPK_IOC_SET_VOLUME
 *   "Speaker Sample Rate", amplifier gain    SFSPK_IOC_INFO
 *   the "Speaker Sense" capture PCM          read()
 *
 * Rules, the same as macaudio's (macaudio_vlimit_*, macaudio_slk_*):
 * - One opener at a time; it is the owner of the unlock control and of all four volumes
 *   (the ELEM_LOCKs of Linux). Root only.
 * - Without an owner, and whenever the owner's lease has run out, every volume is held
 *   at control value 1 (-100 dB) and nothing can raise it.
 * - A ping with the magic value renews the lease to SFSPK_LEASE_MS. The lease only runs
 *   down while the speakers play; it is frozen while they do not.
 * - When the lease runs out the volumes go back to 1 and the next ping fails once with
 *   ETIMEDOUT, so the owner knows it lost the lock. Closing the device locks at once.
 * - The amplifier gain is fixed by the kernel (SFSPK_AMP_GAIN_REG) and not settable.
 *
 * Speaker order everywhere: 0 left front, 1 right front, 2 left rear, 3 right rear.
 * read() returns whole frames of SFSPK_SENSE_CHANNELS signed 16-bit little-endian
 * samples at the playback rate: current then voltage of each speaker in that order
 * (the is_chan/vs_chan of speakersafetyd's j293.conf). It blocks while the speakers do
 * not play, and returns EINTR on a signal.
 */
#ifndef _SF_SPEAKER_SAFETY_H
#define _SF_SPEAKER_SAFETY_H

#include <stdint.h>
#include <sys/ioccom.h>

#define SFSPK_DEVICE            "/dev/sfspeaker-safety"
#define SFSPK_MAGIC             0xdec1be15u     /* SPEAKER_MAGIC_VALUE */
#define SFSPK_LEASE_MS          250             /* SPEAKER_LOCK_TIMEOUT */
#define SFSPK_SPEAKERS          4
#define SFSPK_SENSE_CHANNELS    8
#define SFSPK_VOLUME_MAX        201             /* control value of 0 dB; dB = -100.5 + 0.5 * value */
#define SFSPK_VOLUME_LOCKED     1               /* -100 dB */
#define SFSPK_AMP_GAIN_REG      0x0f            /* 18.5 dBV; dBV = 11 + 0.5 * value */

struct sfspk_info {
	uint32_t version;                       /* 1 */
	uint32_t sample_rate;                   /* 0 while the speakers do not play */
	uint32_t unlocked;                      /* 1 while the volumes may be raised */
	uint32_t amp_gain;                      /* register value, see SFSPK_AMP_GAIN_REG */
	uint32_t volume_max;                    /* highest control value accepted now */
	uint32_t volume[SFSPK_SPEAKERS];        /* control values in force */
	int32_t  die_millicelsius[SFSPK_SPEAKERS]; /* amplifier die temperature, INT32_MIN unknown */
	uint32_t tripped;                       /* non-zero: the kernel shut the amplifiers down (fault) */
	uint32_t reserved[4];
};

struct sfspk_volume {
	uint32_t volume[SFSPK_SPEAKERS];        /* control values, each at most volume_max: EINVAL otherwise */
};

#define SFSPK_IOC_INFO          _IOR('S', 1, struct sfspk_info)
#define SFSPK_IOC_PING          _IOW('S', 2, uint32_t)
#define SFSPK_IOC_SET_VOLUME    _IOW('S', 3, struct sfspk_volume)

#endif
