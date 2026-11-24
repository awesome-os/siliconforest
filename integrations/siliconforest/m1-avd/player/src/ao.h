/* SPDX-License-Identifier: MIT */
/* Audio output of sfvideo: interleaved signed 16-bit stereo, pulled by the output. */
#ifndef SFVIDEO_AO_H
#define SFVIDEO_AO_H
#include <stdint.h>

/* Fills dst with up to `bytes` of audio and returns how many were written (0: nothing ready
 * yet, -1: the stream has ended). *end_pts is the stream time at the end of what was written. */
typedef int (*ao_fill_fn)(void *user, uint8_t *dst, int bytes, double *end_pts);

struct ao;
struct ao_driver {
    const char *name;
    /* the rate the device runs at, 0 when there is no usable device */
    int (*preferred_rate)(void);
    struct ao *(*open)(int rate, double volume, ao_fill_fn fill, void *user);
    int (*start)(struct ao *);
    /* stream time being heard now and the time (av_gettime_relative seconds) it was true at;
     * returns 0 when not known yet */
    int (*clock)(struct ao *, double *pts, double *at);
    /* 1 once everything handed over has been taken by the device (after the fill said -1) */
    int (*drained)(struct ao *);
    long (*underruns)(struct ao *);
    double (*latency)(struct ao *);
    void (*close)(struct ao *);
};
extern const struct ao_driver ao_null;
#ifdef __APPLE__
extern const struct ao_driver ao_audioqueue;
#endif
#endif
