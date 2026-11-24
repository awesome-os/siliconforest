/* SPDX-License-Identifier: MIT */
/*
 * sftone: a sine tone through an AudioQueue, for checking the path from AudioToolbox through
 * coreaudiod to the sound driver without anyone listening.
 *
 *   sftone [-r rate] [-f hertz] [-s seconds] [-d dB below full scale] [-c l|r|b] [-n name]
 *   sftone -u <device UID> [-f hertz] [-s seconds] [-d dB] [-c l|r|b] [-n name]
 *   sftone -l
 *
 * The queue is given signed 16-bit stereo at the rate asked for (default 48000 Hz, 440 Hz,
 * 3 s, 20 dB below full scale, both channels); AudioToolbox converts it to the device's
 * format. At the end it reports how many buffers came back from the queue and at which rate
 * the queue took the frames, measured from the callbacks after the first two seconds (the
 * start of the output device is not steady): that rate is the device's clock as the client
 * sees it.
 *
 * With -u the tone goes to that device whatever the default output is, through an IOProc on
 * it (an AudioQueue always plays on the default), in the device's own format. -l lists the
 * output devices with their UIDs; the default is marked.
 */
#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach_time.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFERS 3

struct tone {
    const char *name;
    double rate, hertz, amplitude, phase;
    int left, right;
    long total, made;           /* frames to make, made so far */
    UInt32 bufferFrames;
    int outstanding, callbacks;
    long returned;              /* frames in the buffers that came back */
    double start, firstAt, lastAt;
    long firstReturned;
    pthread_mutex_t lock;
    pthread_cond_t cond;
};

static double
seconds(void)
{
    static mach_timebase_info_data_t tb;

    if (tb.denom == 0)
        mach_timebase_info(&tb);
    return (double)mach_absolute_time() * tb.numer / tb.denom / 1e9;
}

/* fills and queues the buffer, or lets it go when the tone is complete */
static void
fill(struct tone *t, AudioQueueRef q, AudioQueueBufferRef b)
{
    int16_t *out = b->mAudioData;
    long n = t->total - t->made;

    if (n > (long)t->bufferFrames)
        n = t->bufferFrames;
    for (long i = 0; i < n; i++) {
        int16_t s = (int16_t)(t->amplitude * sin(t->phase));

        t->phase += 2 * M_PI * t->hertz / t->rate;
        if (t->phase > 2 * M_PI)
            t->phase -= 2 * M_PI;
        out[2 * i] = t->left ? s : 0;
        out[2 * i + 1] = t->right ? s : 0;
    }
    t->made += n;
    b->mAudioDataByteSize = (UInt32)n * 4;
    if (n == 0 || AudioQueueEnqueueBuffer(q, b, 0, NULL) != noErr) {
        pthread_mutex_lock(&t->lock);
        t->outstanding--;
        pthread_cond_signal(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
}

/* on the queue's thread, each time a buffer has been taken */
static void
played(void *user, AudioQueueRef q, AudioQueueBufferRef b)
{
    struct tone *t = user;
    double now = seconds();

    t->callbacks++;
    t->returned += b->mAudioDataByteSize / 4;
    if (now - t->start >= 2.0 && t->firstAt == 0) {
        t->firstAt = now;
        t->firstReturned = t->returned;
    }
    t->lastAt = now;
    fill(t, q, b);
}

/* the device's cycle: interleaved float at the device's rate */
static OSStatus
cycle(AudioObjectID device, const AudioTimeStamp *now, const AudioBufferList *input, const AudioTimeStamp *inputTime,
      AudioBufferList *output, const AudioTimeStamp *outputTime, void *user)
{
    struct tone *t = user;
    AudioBuffer *b = &output->mBuffers[0];
    UInt32 ch = b->mNumberChannels, frames = ch ? b->mDataByteSize / (ch * (UInt32)sizeof(float)) : 0;
    float *out = b->mData;
    double at = seconds();

    (void)device; (void)now; (void)input; (void)inputTime; (void)outputTime;
    for (UInt32 i = 0; i < frames; i++) {
        float s = t->made < t->total ? (float)(t->amplitude / 32767.0 * sin(t->phase)) : 0;

        t->phase += 2 * M_PI * t->hertz / t->rate;
        if (t->phase > 2 * M_PI)
            t->phase -= 2 * M_PI;
        if (t->made < t->total)
            t->made++;
        for (UInt32 c = 0; c < ch; c++)
            out[i * ch + c] = c == 0 ? (t->left ? s : 0) : c == 1 ? (t->right ? s : 0) : 0;
    }
    t->callbacks++;
    t->returned += frames;
    if (at - t->start >= 2.0 && t->firstAt == 0) {
        t->firstAt = at;
        t->firstReturned = t->returned;
    }
    t->lastAt = at;
    if (t->made >= t->total) {
        pthread_mutex_lock(&t->lock);
        t->outstanding = 0;
        pthread_cond_signal(&t->cond);
        pthread_mutex_unlock(&t->lock);
    }
    return noErr;
}

static void
copyString(AudioObjectID object, AudioObjectPropertySelector selector, char *out, size_t size)
{
    AudioObjectPropertyAddress a = { selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef s = NULL;
    UInt32 n = sizeof(s);

    out[0] = 0;
    if (AudioObjectGetPropertyData(object, &a, 0, NULL, &n, &s) == noErr && s) {
        CFStringGetCString(s, out, (CFIndex)size, kCFStringEncodingUTF8);
        CFRelease(s);
    }
}

static int
list(void)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    AudioDeviceID devices[16], def = kAudioObjectUnknown;
    UInt32 size = sizeof(devices);

    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &size, devices) != noErr) {
        fprintf(stderr, "sftone: no device list (is coreaudiod running?)\n");
        return 1;
    }
    UInt32 n = size / sizeof(devices[0]), one = sizeof(def);
    a.mSelector = kAudioHardwarePropertyDefaultOutputDevice;
    AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, NULL, &one, &def);
    for (UInt32 i = 0; i < n; i++) {
        char uid[128], name[64];

        copyString(devices[i], kAudioDevicePropertyDeviceUID, uid, sizeof(uid));
        copyString(devices[i], kAudioObjectPropertyName, name, sizeof(name));
        printf("sftone: device %s \"%s\"%s\n", uid, name, devices[i] == def ? " (default output)" : "");
    }
    printf("sftone: %u device%s\n", (unsigned)n, n == 1 ? "" : "s");
    return 0;
}

/* the tone on one device, by its UID */
static int
onDevice(struct tone *t, const char *uid, double length, double db)
{
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyTranslateUIDToDevice, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    CFStringRef cfUID = CFStringCreateWithCString(NULL, uid, kCFStringEncodingUTF8);
    AudioDeviceID device = kAudioObjectUnknown;
    AudioDeviceIOProcID proc = NULL;
    UInt32 size = sizeof(device);
    Float64 rate = 0;
    OSStatus err;

    err = AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, sizeof(cfUID), &cfUID, &size, &device);
    CFRelease(cfUID);
    if (err != noErr || device == kAudioObjectUnknown) {
        fprintf(stderr, "sftone[%s]: no device %s\n", t->name, uid);
        return 1;
    }
    a.mSelector = kAudioDevicePropertyNominalSampleRate;
    size = sizeof(rate);
    if (AudioObjectGetPropertyData(device, &a, 0, NULL, &size, &rate) != noErr || rate <= 0) {
        fprintf(stderr, "sftone[%s]: %s has no sample rate\n", t->name, uid);
        return 1;
    }
    t->rate = rate;
    t->total = (long)(length * rate);
    t->outstanding = 1;
    if ((err = AudioDeviceCreateIOProcID(device, cycle, t, &proc)) != noErr) {
        fprintf(stderr, "sftone[%s]: AudioDeviceCreateIOProcID failed (%d)\n", t->name, (int)err);
        return 1;
    }
    t->start = seconds();
    if ((err = AudioDeviceStart(device, proc)) != noErr) {
        fprintf(stderr, "sftone[%s]: AudioDeviceStart failed (%d)\n", t->name, (int)err);
        return 1;
    }
    fprintf(stderr, "sftone[%s]: %.0f Hz on %s (%.0f Hz) for %.1f s, %.0f dB below full scale\n", t->name, t->hertz, uid,
            rate, length, db);
    pthread_mutex_lock(&t->lock);
    while (t->outstanding > 0)
        pthread_cond_wait(&t->cond, &t->lock);
    pthread_mutex_unlock(&t->lock);
    usleep(400000);
    AudioDeviceStop(device, proc);
    AudioDeviceDestroyIOProcID(device, proc);

    double span = t->lastAt - t->firstAt;
    fprintf(stderr, "sftone[%s]: done: %ld frames in %d cycles on %s, %.2f s from start to the last frame; "
            "taken at %.0f frames/s over the last %.2f s (device %.0f)\n", t->name, t->made, t->callbacks, uid,
            t->lastAt - t->start, t->firstAt > 0 && span > 0 ? (t->returned - t->firstReturned) / span : 0.0,
            t->firstAt > 0 ? span : 0.0, rate);
    return 0;
}

int
main(int argc, char **argv)
{
    struct tone t = { .name = "tone", .rate = 48000, .hertz = 440, .left = 1, .right = 1 };
    double length = 3, db = 20;
    AudioStreamBasicDescription fmt = { 0 };
    AudioQueueRef q = NULL;
    Float64 deviceRate = 0;
    UInt32 size = sizeof(deviceRate);
    OSStatus err;
    const char *uid = NULL;
    int c;

    while ((c = getopt(argc, argv, "r:f:s:d:c:n:u:l")) != -1) {
        switch (c) {
        case 'r': t.rate = atof(optarg); break;
        case 'f': t.hertz = atof(optarg); break;
        case 's': length = atof(optarg); break;
        case 'd': db = atof(optarg); break;
        case 'c': t.left = optarg[0] != 'r'; t.right = optarg[0] != 'l'; break;
        case 'n': t.name = optarg; break;
        case 'u': uid = optarg; break;
        case 'l': return list();
        default:
            fprintf(stderr, "usage: sftone [-r rate | -u device UID] [-f hertz] [-s seconds] [-d dB below full scale] "
                    "[-c l|r|b] [-n name]\n       sftone -l\n");
            return 2;
        }
    }
    if (t.rate < 8000 || t.rate > 192000 || length <= 0 || length > 600 || db < 6) {
        fprintf(stderr, "sftone: rate 8000..192000, up to 600 s, at least 6 dB below full scale\n");
        return 2;
    }
    t.amplitude = 32767.0 * pow(10.0, -db / 20.0);
    t.total = (long)(length * t.rate);
    t.bufferFrames = (UInt32)(t.rate / 10);
    pthread_mutex_init(&t.lock, NULL);
    pthread_cond_init(&t.cond, NULL);
    if (uid)
        return onDevice(&t, uid, length, db);

    fmt.mSampleRate = t.rate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    fmt.mBytesPerPacket = fmt.mBytesPerFrame = 4;
    fmt.mFramesPerPacket = 1;
    fmt.mChannelsPerFrame = 2;
    fmt.mBitsPerChannel = 16;
    if ((err = AudioQueueNewOutput(&fmt, played, &t, NULL, NULL, 0, &q)) != noErr) {
        fprintf(stderr, "sftone[%s]: AudioQueueNewOutput failed (%d): is coreaudiod running with an output device?\n",
                t.name, (int)err);
        return 1;
    }
    AudioQueueGetProperty(q, kAudioQueueDeviceProperty_SampleRate, &deviceRate, &size);
    for (int i = 0; i < BUFFERS; i++) {
        AudioQueueBufferRef b = NULL;

        if (AudioQueueAllocateBuffer(q, t.bufferFrames * 4, &b) != noErr)
            break;
        pthread_mutex_lock(&t.lock);
        t.outstanding++;
        pthread_mutex_unlock(&t.lock);
        fill(&t, q, b);
    }
    t.start = seconds();
    if ((err = AudioQueueStart(q, NULL)) != noErr) {
        fprintf(stderr, "sftone[%s]: AudioQueueStart failed (%d)\n", t.name, (int)err);
        return 1;
    }
    fprintf(stderr, "sftone[%s]: %.0f Hz at %.0f Hz for %.1f s, %.0f dB below full scale; the device runs at %.0f Hz\n",
            t.name, t.hertz, t.rate, length, db, deviceRate);
    pthread_mutex_lock(&t.lock);
    while (t.outstanding > 0)
        pthread_cond_wait(&t.cond, &t.lock);
    pthread_mutex_unlock(&t.lock);
    /* what the queue has taken is still on its way through the server and the driver */
    usleep(400000);
    AudioQueueStop(q, true);
    AudioQueueDispose(q, true);

    double span = t.lastAt - t.firstAt;
    fprintf(stderr, "sftone[%s]: done: %ld frames in %d buffers, %.2f s from start to the last buffer; "
            "taken at %.0f frames/s over the last %.2f s (asked %.0f)\n", t.name, t.returned, t.callbacks,
            t.lastAt - t.start, t.firstAt > 0 && span > 0 ? (t.returned - t.firstReturned) / span : 0.0,
            t.firstAt > 0 ? span : 0.0, t.rate);
    return 0;
}
