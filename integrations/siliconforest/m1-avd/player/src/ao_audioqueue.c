/* SPDX-License-Identifier: MIT */
/*
 * sfvideo audio output through an AudioQueue: AudioToolbox -> coreaudiod -> the sound driver,
 * the path every sound client of the system uses (see m1-sound/sysaudio/DESIGN).
 *
 * The clock: the queue reports how many frames the device's cycle has taken from it
 * (AudioQueueGetCurrentTime, in frames at the device's rate). What was handed to the queue and
 * not taken yet is still to come; what was taken is heard one device latency later.
 *   heard now = stream time at the end of what was handed over
 *               - (frames handed over - frames taken) / rate - device latency
 */
#include <AudioToolbox/AudioToolbox.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libavutil/time.h>
#include "ao.h"

#define AQ_BUFFERS 3
#define AQ_BUFFER_MS 40

struct ao {
    AudioQueueRef q;
    AudioQueueBufferRef bufs[AQ_BUFFERS];
    int rate, buffer_bytes;
    double device_rate, latency;
    ao_fill_fn fill;
    void *user;
    pthread_mutex_t lock;
    int64_t frames_handed;      /* frames given to the queue, silence included */
    double pts_handed;          /* stream time at the end of them */
    int outstanding;            /* buffers the queue holds */
    int ended, closing;
    long underruns;
};

static AudioDeviceID default_output(void)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDefaultOutputDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    AudioDeviceID dev = kAudioObjectUnknown;
    UInt32 size = sizeof(dev);

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, &dev) != noErr)
        return kAudioObjectUnknown;
    return dev;
}

static UInt32 device_u32(AudioDeviceID dev, AudioObjectPropertySelector sel)
{
    AudioObjectPropertyAddress a = { sel, kAudioObjectPropertyScopeOutput, kAudioObjectPropertyElementMain };
    UInt32 v = 0, size = sizeof(v);

    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &v) != noErr)
        return 0;
    return v;
}

/* The frameworks are linked weakly: on a system without them the functions are not there. */
static int frameworks_present(void)
{
    void *volatile queue = (void *)&AudioQueueNewOutput, *volatile hardware = (void *)&AudioObjectGetPropertyData;

    return queue != NULL && hardware != NULL;
}

static int aq_preferred_rate(void)
{
    AudioObjectPropertyAddress a = { kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    AudioDeviceID dev;
    Float64 rate = 0;
    UInt32 size = sizeof(rate);

    if (!frameworks_present())
        return 0;
    if ((dev = default_output()) == kAudioObjectUnknown)
        return 0;
    if (AudioObjectGetPropertyData(dev, &a, 0, NULL, &size, &rate) != noErr || rate < 8000 || rate > 192000)
        return 48000;
    return (int)rate;
}

/* hands one buffer to the queue, or keeps it when the stream has ended */
static void feed(struct ao *ao, AudioQueueBufferRef b)
{
    double end = NAN;
    int n = ao->closing ? -1 : ao->fill(ao->user, b->mAudioData, ao->buffer_bytes, &end);
    int silence = 0;

    if (n < 0) {
        pthread_mutex_lock(&ao->lock);
        ao->ended = 1;
        ao->outstanding--;
        pthread_mutex_unlock(&ao->lock);
        return;
    }
    if (n == 0) {
        /* nothing decoded yet: 10 ms of silence keeps the queue asking */
        n = ao->rate / 100 * 4;
        memset(b->mAudioData, 0, (size_t)n);
        silence = 1;
    }
    b->mAudioDataByteSize = (UInt32)n;
    pthread_mutex_lock(&ao->lock);
    ao->frames_handed += n / 4;
    if (!isnan(end))
        ao->pts_handed = end;
    if (silence && !isnan(ao->pts_handed))
        ao->underruns++;
    pthread_mutex_unlock(&ao->lock);
    if (AudioQueueEnqueueBuffer(ao->q, b, 0, NULL) != noErr) {
        pthread_mutex_lock(&ao->lock);
        ao->outstanding--;
        pthread_mutex_unlock(&ao->lock);
    }
}

static void aq_callback(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
    (void)q;
    feed(user, b);
}

static struct ao *aq_open(int rate, double volume, ao_fill_fn fill, void *user)
{
    AudioStreamBasicDescription fmt = { 0 };
    struct ao *ao = calloc(1, sizeof(*ao));
    AudioDeviceID dev = default_output();
    Float64 device_rate = 0;
    UInt32 size = sizeof(device_rate);
    OSStatus err;

    if (!ao)
        return NULL;
    ao->rate = rate;
    ao->fill = fill;
    ao->user = user;
    ao->pts_handed = NAN;
    ao->buffer_bytes = rate * AQ_BUFFER_MS / 1000 * 4;
    pthread_mutex_init(&ao->lock, NULL);
    fmt.mSampleRate = rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    if ((err = AudioQueueNewOutput(&fmt, aq_callback, ao, NULL, NULL, 0, &ao->q)) != noErr) {
        fprintf(stderr, "sfvideo: AudioQueueNewOutput failed (%d): no audio output (is coreaudiod running with a device?)\n", (int)err);
        free(ao);
        return NULL;
    }
    if (AudioQueueGetProperty(ao->q, kAudioQueueDeviceProperty_SampleRate, &device_rate, &size) != noErr || device_rate <= 0)
        device_rate = rate;
    ao->device_rate = device_rate;
    if (dev != kAudioObjectUnknown) {
        UInt32 frames = device_u32(dev, kAudioDevicePropertyLatency) + device_u32(dev, kAudioDevicePropertySafetyOffset) +
                        device_u32(dev, kAudioDevicePropertyBufferFrameSize);
        ao->latency = frames / device_rate;
    }
    AudioQueueSetParameter(ao->q, kAudioQueueParam_Volume, (AudioQueueParameterValue)volume);
    for (int i = 0; i < AQ_BUFFERS; i++) {
        if (AudioQueueAllocateBuffer(ao->q, (UInt32)ao->buffer_bytes, &ao->bufs[i]) != noErr) {
            fprintf(stderr, "sfvideo: AudioQueueAllocateBuffer failed\n");
            AudioQueueDispose(ao->q, true);
            free(ao);
            return NULL;
        }
    }
    return ao;
}

static int aq_start(struct ao *ao)
{
    OSStatus err;

    for (int i = 0; i < AQ_BUFFERS; i++) {
        pthread_mutex_lock(&ao->lock);
        ao->outstanding++;
        pthread_mutex_unlock(&ao->lock);
        feed(ao, ao->bufs[i]);
    }
    if ((err = AudioQueueStart(ao->q, NULL)) != noErr) {
        fprintf(stderr, "sfvideo: AudioQueueStart failed (%d)\n", (int)err);
        return -1;
    }
    return 0;
}

static int aq_clock(struct ao *ao, double *pts, double *at)
{
    AudioTimeStamp ts;
    double handed_pts, taken;
    int64_t handed;

    if (AudioQueueGetCurrentTime(ao->q, NULL, &ts, NULL) != noErr)
        return 0;
    *at = av_gettime_relative() / 1000000.0;
    pthread_mutex_lock(&ao->lock);
    handed = ao->frames_handed;
    handed_pts = ao->pts_handed;
    pthread_mutex_unlock(&ao->lock);
    if (isnan(handed_pts))
        return 0;
    taken = ts.mSampleTime * ao->rate / ao->device_rate;
    *pts = handed_pts - ((double)handed - taken) / ao->rate - ao->latency;
    return 1;
}

static int aq_drained(struct ao *ao)
{
    pthread_mutex_lock(&ao->lock);
    int done = ao->ended && ao->outstanding <= 0;
    pthread_mutex_unlock(&ao->lock);
    return done;
}

static long aq_underruns(struct ao *ao)
{
    return ao->underruns;
}

static double aq_latency(struct ao *ao)
{
    return ao->latency;
}

static void aq_close(struct ao *ao)
{
    ao->closing = 1;
    AudioQueueStop(ao->q, true);
    AudioQueueDispose(ao->q, true);
    pthread_mutex_destroy(&ao->lock);
    free(ao);
}

const struct ao_driver ao_audioqueue = {
    .name = "audioqueue", .preferred_rate = aq_preferred_rate, .open = aq_open, .start = aq_start, .clock = aq_clock,
    .drained = aq_drained, .underruns = aq_underruns, .latency = aq_latency, .close = aq_close,
};
