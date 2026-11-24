/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The built-in speakers of the J293: four TAS5770L amplifiers, their TDM stream, the
 * current/voltage sense capture and the volume lock that keeps them safe. See
 * sf_speakers.cpp for the rules and ../include/sf_speaker_safety.h for the interface of
 * the safety daemon.
 */
#ifndef _SF_SPEAKERS_H
#define _SF_SPEAKERS_H

#include "sf_dart_fixed.h"
#include "sf_gpio.h"

#define SF_SPK_PLAY_RING_BYTES   (8 * 1024 * 16)    /* 8 periods of 1024 frames, 4 channels of 32 bits */
#define SF_SPK_SENSE_RING_BYTES  (8 * 1024 * 16)    /* 8 periods of 1024 frames, 8 channels of 16 bits */

/*
 * Probes the amplifiers (they are powered up for that and put back into hardware
 * shutdown), sets up the streams and starts the speaker thread. `playRing`/`senseRing`
 * are DMA memory behind the audio DART (kernel address and device address).
 * 0, or an errno value when the speakers stay unavailable.
 */
int sf_speakers_bring_up(SFGpio *gpio, SFDartFixed *dart, uint8_t *playRing, uint64_t playIova, uint8_t *senseRing,
    uint64_t senseIova);
/* Mutes and shuts the amplifiers down for good (system shutdown). */
void sf_speakers_quiesce(void);

#endif
