/* SPDX-License-Identifier: MIT */
/* Deliberately minimal HOST MOCK types, not a native framework ABI/header. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <time.h>
#include <dlfcn.h>
typedef void *AudioUnit;
typedef int32_t OSStatus;
typedef uint32_t UInt32,AudioObjectID,AudioDeviceID,AudioUnitPropertyID,AudioUnitScope,AudioUnitElement,AudioUnitRenderActionFlags;
typedef struct {double mSampleTime;uint64_t mHostTime;double mRateScalar;unsigned mFlags;} AudioTimeStamp;
typedef struct {unsigned mNumberChannels,mDataByteSize;void *mData;} AudioBuffer;
typedef struct {unsigned mNumberBuffers;AudioBuffer mBuffers[1];} AudioBufferList;
typedef OSStatus (*AURenderCallback)(void *,AudioUnitRenderActionFlags *,const AudioTimeStamp *,UInt32,UInt32,AudioBufferList *);
typedef struct {AURenderCallback inputProc;void *inputProcRefCon;} AURenderCallbackStruct;
enum {kAudioOutputUnitProperty_CurrentDevice=2000,kAudioUnitScope_Global=0,kAudioUnitProperty_SetRenderCallback=23,kSFAudioRingVersion=2};
struct sf_audio_ring {uint32_t version,channels,frames,periodFrames;volatile uint32_t running;uint32_t sampleRate;volatile uint64_t writeFrame,readFrame,readHostTime;float data[];};
struct sf_audio_session {struct sf_audio_ring *ring;};
static uint64_t mach_absolute_time(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec;}
static void *mock_dlsym(void *,const char *);
static int mock_dladdr(const void *,Dl_info *);
#define dlsym mock_dlsym
#define dladdr mock_dladdr
