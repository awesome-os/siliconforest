// SPDX-License-Identifier: MIT
// AudioToolbox additions (wrapper over the sound stream's AudioToolbox, re-exported): System
// Sound Services. A system sound is played through the re-exported AudioQueue API, that is through
// coreaudiod and its output device: a sound made from a WAVE file (16-bit PCM) plays that file;
// the alert sound (kAudioServicesPropertyAlertSoundURL, 'aurl') is a short tone made here (there is
// no alert sound file on the system: nothing of macOS's sounds is copied). Where coreaudiod or an
// output device is missing the sound is silent, as before, and an application's bell is then
// visual only (Terminal's "visual bell").
#include <CoreFoundation/CoreFoundation.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
typedef int32_t OSStatus;
typedef uint32_t SystemSoundID;
enum { kAudioServicesUnsupportedPropertyError = 'pty?', kAudioServicesSystemSoundUnspecifiedError = -1500, kAudioServicesBadSystemSoundIDError = -1501 };
enum { kAudioServicesPropertyAlertSoundURL = 'aurl', kSystemSoundID_UserPreferredAlert = 0x1000, kSystemSoundID_FlashScreen = 0x00000FFE };

// the AudioQueue API of the re-exported AudioToolbox (its headers are not in this SDK)
typedef struct AudioStreamBasicDescription {
    double mSampleRate; uint32_t mFormatID, mFormatFlags, mBytesPerPacket, mFramesPerPacket, mBytesPerFrame,
    mChannelsPerFrame, mBitsPerChannel, mReserved;
} AudioStreamBasicDescription;
typedef struct AudioQueueBuffer {
    const uint32_t mAudioDataBytesCapacity; void *const mAudioData; uint32_t mAudioDataByteSize;
    void *mUserData; const uint32_t mPacketDescriptionCapacity; void *const mPacketDescriptions; uint32_t mPacketDescriptionCount;
} AudioQueueBuffer, *AudioQueueBufferRef;
typedef struct OpaqueAudioQueue *AudioQueueRef;
typedef void (*AudioQueueOutputCallback)(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer);
extern OSStatus AudioQueueNewOutput(const AudioStreamBasicDescription *format, AudioQueueOutputCallback callback, void *user,
                                    CFRunLoopRef loop, CFStringRef mode, uint32_t flags, AudioQueueRef *outQueue);
extern OSStatus AudioQueueAllocateBuffer(AudioQueueRef queue, uint32_t size, AudioQueueBufferRef *outBuffer);
extern OSStatus AudioQueueEnqueueBuffer(AudioQueueRef queue, AudioQueueBufferRef buffer, uint32_t packets, const void *descriptions);
extern OSStatus AudioQueueStart(AudioQueueRef queue, const void *startTime);
extern OSStatus AudioQueueStop(AudioQueueRef queue, Boolean immediate);
extern OSStatus AudioQueueDispose(AudioQueueRef queue, Boolean immediate);
#define kAudioFormatLinearPCM 'lpcm'
#define kAudioFormatFlagIsSignedInteger (1u << 2)
#define kAudioFormatFlagIsPacked (1u << 3)

#define BELL_URL "file:///System/Library/Sounds/SiliconForest%20Alert"
#define MAX_SOUNDS 64

typedef struct {
    int used;
    CFURLRef url;
    int16_t *pcm;          // interleaved stereo, 16 bit
    size_t frames;
    double rate;
    void (*completion)(SystemSoundID, void *);
    void *completionData;
    CFRunLoopRef loop;
} Sound;
static Sound sounds[MAX_SOUNDS];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

// the alert: a soft two-tone chime, 160 ms, far below full scale, faded in and out
static void makeAlert(Sound *s) {
    s->rate = 48000;
    s->frames = (size_t)(0.16 * s->rate);
    s->pcm = calloc(s->frames * 2, sizeof(int16_t));
    if (s->pcm == NULL) { s->frames = 0; return; }
    for (size_t i = 0; i < s->frames; i++) {
        double t = i / s->rate, edge = fmin(1.0, fmin(t / 0.01, (s->frames - i) / s->rate / 0.04));
        double hz = t < 0.06 ? 1318.5 : 987.8;
        int16_t v = (int16_t)(32767.0 * 0.06 * edge * sin(2 * M_PI * hz * t));
        s->pcm[2 * i] = s->pcm[2 * i + 1] = v;
    }
}

// a RIFF/WAVE file of 16-bit PCM, mono or stereo
static int loadWave(Sound *s, const char *path) {
    FILE *f = fopen(path, "rb");
    unsigned char h[12], c[8];
    int channels = 0, bits = 0, ok = 0;

    if (f == NULL) return 0;
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return 0; }
    while (fread(c, 1, 8, f) == 8) {
        uint32_t size = c[4] | c[5] << 8 | c[6] << 16 | (uint32_t)c[7] << 24;
        if (!memcmp(c, "fmt ", 4) && size >= 16) {
            unsigned char fm[16];
            if (fread(fm, 1, 16, f) != 16) break;
            if ((fm[0] | fm[1] << 8) != 1) break;
            channels = fm[2] | fm[3] << 8;
            s->rate = fm[4] | fm[5] << 8 | fm[6] << 16 | (uint32_t)fm[7] << 24;
            bits = fm[14] | fm[15] << 8;
            fseek(f, size - 16 + (size & 1), SEEK_CUR);
        } else if (!memcmp(c, "data", 4) && channels >= 1 && channels <= 2 && bits == 16 && size < (64u << 20)) {
            int16_t *in = malloc(size);
            if (in == NULL || fread(in, 1, size, f) != size) { free(in); break; }
            s->frames = size / (2 * channels);
            s->pcm = malloc(s->frames * 4);
            for (size_t i = 0; s->pcm && i < s->frames; i++) {
                s->pcm[2 * i] = in[i * channels];
                s->pcm[2 * i + 1] = in[i * channels + channels - 1];
            }
            free(in);
            ok = s->pcm != NULL;
            break;
        } else
            fseek(f, size + (size & 1), SEEK_CUR);
    }
    fclose(f);
    return ok;
}

OSStatus AudioServicesCreateSystemSoundID(CFURLRef file, SystemSoundID *outID) {
    char path[1024];
    Sound s = {0};

    if (outID) *outID = 0;
    if (file == NULL || outID == NULL) return kAudioServicesSystemSoundUnspecifiedError;
    CFStringRef text = CFURLGetString(file);
    if (text != NULL && CFStringCompare(text, CFSTR(BELL_URL), 0) == kCFCompareEqualTo)
        makeAlert(&s);
    else if (!CFURLGetFileSystemRepresentation(file, true, (UInt8 *)path, sizeof(path)) || !loadWave(&s, path))
        return kAudioServicesSystemSoundUnspecifiedError;
    if (s.frames == 0) return kAudioServicesSystemSoundUnspecifiedError;
    pthread_mutex_lock(&lock);
    for (SystemSoundID i = 1; i < MAX_SOUNDS; i++) {
        if (!sounds[i].used) {
            s.used = 1;
            s.url = CFRetain(file);
            sounds[i] = s;
            *outID = i;
            pthread_mutex_unlock(&lock);
            return 0;
        }
    }
    pthread_mutex_unlock(&lock);
    free(s.pcm);
    return kAudioServicesSystemSoundUnspecifiedError;
}

OSStatus AudioServicesDisposeSystemSoundID(SystemSoundID sound) {
    pthread_mutex_lock(&lock);
    if (sound == 0 || sound >= MAX_SOUNDS || !sounds[sound].used) { pthread_mutex_unlock(&lock); return kAudioServicesBadSystemSoundIDError; }
    // a sound that is playing keeps its samples: they are freed when it has played (see play)
    sounds[sound].used = 0;
    if (sounds[sound].url) CFRelease(sounds[sound].url);
    sounds[sound].url = NULL;
    sounds[sound].completion = NULL;
    pthread_mutex_unlock(&lock);
    return 0;
}

typedef struct { int16_t *pcm; size_t frames; double rate; SystemSoundID id; void (*completion)(SystemSoundID, void *); void *data; CFRunLoopRef loop; int done; pthread_mutex_t m; pthread_cond_t c; } Playing;

static void returned(void *user, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    Playing *p = user;
    pthread_mutex_lock(&p->m);
    p->done = 1;
    pthread_cond_signal(&p->c);
    pthread_mutex_unlock(&p->m);
}

static void *play(void *arg) {
    Playing *p = arg;
    AudioStreamBasicDescription fmt = { p->rate, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked, 4, 1, 4, 2, 16, 0 };
    AudioQueueRef q = NULL;
    AudioQueueBufferRef b = NULL;

    if (AudioQueueNewOutput(&fmt, returned, p, NULL, NULL, 0, &q) == 0 && AudioQueueAllocateBuffer(q, (uint32_t)(p->frames * 4), &b) == 0) {
        memcpy(b->mAudioData, p->pcm, p->frames * 4);
        b->mAudioDataByteSize = (uint32_t)(p->frames * 4);
        if (AudioQueueEnqueueBuffer(q, b, 0, NULL) == 0 && AudioQueueStart(q, NULL) == 0) {
            pthread_mutex_lock(&p->m);
            while (!p->done) pthread_cond_wait(&p->c, &p->m);
            pthread_mutex_unlock(&p->m);
            usleep(200000);   // what the queue took is still on its way through the server
        }
    } else if (getenv("SF_SYSTEM_SOUND_TRACE"))
        fprintf(stderr, "AudioToolbox: system sound %u: no output (is coreaudiod running with an output device?)\n", p->id);
    if (q) { AudioQueueStop(q, true); AudioQueueDispose(q, true); }
    if (p->completion) {
        void (*completion)(SystemSoundID, void *) = p->completion;
        SystemSoundID id = p->id;
        void *data = p->data;
        if (p->loop) {
            CFRunLoopPerformBlock(p->loop, kCFRunLoopCommonModes, ^{ completion(id, data); });
            CFRunLoopWakeUp(p->loop);
            CFRelease(p->loop);
        } else
            completion(id, data);
    }
    free(p->pcm);
    free(p);
    return NULL;
}

static void start(SystemSoundID sound) {
    Playing *p;
    pthread_t thread;

    pthread_mutex_lock(&lock);
    if (sound == 0 || sound >= MAX_SOUNDS || !sounds[sound].used || (p = calloc(1, sizeof(*p))) == NULL) { pthread_mutex_unlock(&lock); return; }
    p->pcm = malloc(sounds[sound].frames * 4);
    if (p->pcm == NULL) { free(p); pthread_mutex_unlock(&lock); return; }
    memcpy(p->pcm, sounds[sound].pcm, sounds[sound].frames * 4);
    p->frames = sounds[sound].frames;
    p->rate = sounds[sound].rate;
    p->id = sound;
    p->completion = sounds[sound].completion;
    p->data = sounds[sound].completionData;
    p->loop = sounds[sound].loop ? (CFRunLoopRef)CFRetain(sounds[sound].loop) : NULL;
    pthread_mutex_init(&p->m, NULL);
    pthread_cond_init(&p->c, NULL);
    pthread_mutex_unlock(&lock);
    if (pthread_create(&thread, NULL, play, p) == 0)
        pthread_detach(thread);
    else {
        free(p->pcm);
        free(p);
    }
}

void AudioServicesPlayAlertSound(SystemSoundID sound) { start(sound); }

OSStatus AudioServicesAddSystemSoundCompletion(SystemSoundID sound, CFRunLoopRef loop, CFStringRef mode, void (*routine)(SystemSoundID, void *), void *clientData) {
    pthread_mutex_lock(&lock);
    if (sound == 0 || sound >= MAX_SOUNDS || !sounds[sound].used) { pthread_mutex_unlock(&lock); return kAudioServicesBadSystemSoundIDError; }
    sounds[sound].completion = routine;
    sounds[sound].completionData = clientData;
    sounds[sound].loop = loop ? loop : CFRunLoopGetCurrent();
    pthread_mutex_unlock(&lock);
    return 0;
}

void AudioServicesRemoveSystemSoundCompletion(SystemSoundID sound) {
    pthread_mutex_lock(&lock);
    if (sound > 0 && sound < MAX_SOUNDS) sounds[sound].completion = NULL;
    pthread_mutex_unlock(&lock);
}

OSStatus AudioServicesGetProperty(uint32_t property, uint32_t specifierSize, const void *specifier, uint32_t *ioSize, void *outData) {
    // the alert sound: a URL this library makes its tone for (the caller releases it)
    if (property == kAudioServicesPropertyAlertSoundURL && ioSize != NULL && *ioSize >= sizeof(CFURLRef) && outData != NULL) {
        *(CFURLRef *)outData = CFURLCreateWithString(NULL, CFSTR(BELL_URL), NULL);
        *ioSize = sizeof(CFURLRef);
        return 0;
    }
    return kAudioServicesUnsupportedPropertyError;
}
