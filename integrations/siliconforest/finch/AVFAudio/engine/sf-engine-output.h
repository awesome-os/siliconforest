/* SPDX-License-Identifier: MIT */
/* Own retained-PD AudioQueue bridge. No device timer or discarded output. */
#include <AudioToolbox/AudioQueue.h>
#include <pthread.h>
#include <dlfcn.h>
#include <stdio.h>

typedef struct {
    AVAudioEngine *engine; /* valid until synchronous queue disposal joins callbacks */
    AudioQueueRef queue;
    AVAudioPCMBuffer *pcm;
    pthread_mutex_t lock;
    BOOL stopping;
    OSStatus error;
    uint64_t queued, returned, nonzero;
    unsigned callbacks;
} SFEngineHAL;

@interface AVAudioEngine (SFHALOutput)
- (void)_sfReturnOutput:(AudioQueueBufferRef)buffer;
- (OSStatus)_sfFillOutput:(AudioQueueBufferRef)buffer;
- (void)_sfStopOutput;
@end

static void sfEngineReturned(void *context, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    (void)queue;
    SFEngineHAL *state = context;
    @autoreleasepool { [state->engine _sfReturnOutput:buffer]; }
}

static double sfEngineDeviceRate(void) {
    /* Own public CoreAudio property declaration, matching the PD implementation. */
    typedef struct {UInt32 selector, scope, element;} Address;
    typedef OSStatus (*Get)(UInt32,const Address *,UInt32,const void *,UInt32 *,void *);
    Get get = (Get)dlsym(RTLD_DEFAULT,"AudioObjectGetPropertyData");
    if (!get) {
        void *image=dlopen("/System/Library/Frameworks/CoreAudio.framework/Versions/A/CoreAudio",RTLD_NOW|RTLD_LOCAL);
        if(image)get=(Get)dlsym(image,"AudioObjectGetPropertyData");
    }
    if(!get)return 0;
    UInt32 device=0,size=sizeof(device); Address address={'dOut','glob',0};
    if(get(1,&address,0,NULL,&size,&device)||!device)return 0;
    double rate=0;size=sizeof(rate);address=(Address){'nsrt','glob',0};
    if(get(device,&address,0,NULL,&size,&rate)||!isfinite(rate)||rate<8000||rate>192000)return 0;
    return rate;
}
