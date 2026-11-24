/*
 * Clock and audio/video synchronisation logic of ffplay, taken from fftools/ffplay.c of
 * FFmpeg 8.1.2 and reduced to what sfvideo uses (no serials, no pause, no speed change).
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef SFVIDEO_FFSYNC_H
#define SFVIDEO_FFSYNC_H

#include <math.h>
#include <libavutil/common.h>
#include <libavutil/time.h>

/* no AV sync correction is done if below the minimum AV sync threshold */
#define AV_SYNC_THRESHOLD_MIN 0.04
/* AV sync correction is done if above the maximum AV sync threshold */
#define AV_SYNC_THRESHOLD_MAX 0.1
/* If a frame duration is longer than this, it will not be duplicated to compensate AV sync */
#define AV_SYNC_FRAMEDUP_THRESHOLD 0.1
/* no AV correction is done if too big error */
#define AV_NOSYNC_THRESHOLD 10.0
/* polls for possible required screen refresh at least this often, should be less than 1/fps */
#define REFRESH_RATE 0.01

typedef struct Clock {
    double pts;           /* clock base */
    double pts_drift;     /* clock base minus time at which we updated the clock */
    double last_updated;
} Clock;

static inline double get_clock(const Clock *c)
{
    double time = av_gettime_relative() / 1000000.0;
    return c->pts_drift + time;
}

static inline void set_clock_at(Clock *c, double pts, double time)
{
    c->pts = pts;
    c->last_updated = time;
    c->pts_drift = c->pts - time;
}

static inline void set_clock(Clock *c, double pts)
{
    double time = av_gettime_relative() / 1000000.0;
    set_clock_at(c, pts, time);
}

static inline void init_clock(Clock *c)
{
    set_clock(c, NAN);
}

/* delay: nominal duration of the frame on screen; vidclk: pts of that frame as a clock;
 * master: the clock video follows (NAN while it has not started). */
static inline double compute_target_delay(double delay, double vidclk, double master, double max_frame_duration)
{
    double sync_threshold, diff = 0;

    /* if video is slave, we try to correct big delays by
       duplicating or deleting a frame */
    diff = vidclk - master;

    /* skip or repeat frame. We take into account the
       delay to compute the threshold. I still don't know
       if it is the best guess */
    sync_threshold = FFMAX(AV_SYNC_THRESHOLD_MIN, FFMIN(AV_SYNC_THRESHOLD_MAX, delay));
    if (!isnan(diff) && fabs(diff) < max_frame_duration) {
        if (diff <= -sync_threshold)
            delay = FFMAX(0, delay + diff);
        else if (diff >= sync_threshold && delay > AV_SYNC_FRAMEDUP_THRESHOLD)
            delay = delay + diff;
        else if (diff >= sync_threshold)
            delay = 2 * delay;
    }
    return delay;
}

/* duration of frame `pts` given the next frame's pts */
static inline double vp_duration(double pts, double fallback, double next_pts, double max_frame_duration)
{
    double duration = next_pts - pts;
    if (isnan(duration) || duration <= 0 || duration > max_frame_duration)
        return fallback;
    return duration;
}

#endif
