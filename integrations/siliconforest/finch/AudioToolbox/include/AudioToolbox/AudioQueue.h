/* SPDX-License-Identifier: MIT */
/* Own public ABI declarations; field facts checked against open PD source. */
#pragma once
#include <AudioToolbox/AudioToolbox.h>
typedef struct OpaqueAudioQueue *AudioQueueRef;
typedef struct OpaqueAudioQueueTimeline *AudioQueueTimelineRef;
typedef struct AudioQueueBuffer {
    const UInt32 mAudioDataBytesCapacity;
    void *const mAudioData;
    UInt32 mAudioDataByteSize;
    void *mUserData;
    const UInt32 mPacketDescriptionCapacity;
    AudioStreamPacketDescription *const mPacketDescriptions;
    UInt32 mPacketDescriptionCount;
} AudioQueueBuffer, *AudioQueueBufferRef;
typedef UInt32 AudioQueuePropertyID, AudioQueueParameterID;
typedef Float32 AudioQueueParameterValue;
typedef void (*AudioQueueOutputCallback)(void *,AudioQueueRef,AudioQueueBufferRef);
typedef void (*AudioQueueInputCallback)(void *,AudioQueueRef,AudioQueueBufferRef,const AudioTimeStamp *,UInt32,const AudioStreamPacketDescription *);
typedef void (*AudioQueuePropertyListenerProc)(void *,AudioQueueRef,AudioQueuePropertyID);
#define kAudioQueueParam_Volume 1
#define kAudioQueueProperty_IsRunning 0x6171726e
#define kAudioQueueProperty_StreamDescription 0x61716674
#define kAudioQueueErr_InvalidDevice (-66679)
#define kAudioQueueErr_InvalidParameter (-66680)
#define kAudioQueueErr_InvalidProperty (-66683)
#define kAudioQueueErr_BufferEmpty (-66686)
#define kAudioQueueErr_InvalidBuffer (-66687)
OSStatus AudioQueueNewOutput(const AudioStreamBasicDescription *,AudioQueueOutputCallback,void *,CFRunLoopRef,CFStringRef,UInt32,AudioQueueRef *);
OSStatus AudioQueueNewInput(const AudioStreamBasicDescription *,AudioQueueInputCallback,void *,CFRunLoopRef,CFStringRef,UInt32,AudioQueueRef *);
OSStatus AudioQueueAllocateBuffer(AudioQueueRef,UInt32,AudioQueueBufferRef *);
OSStatus AudioQueueAllocateBufferWithPacketDescriptions(AudioQueueRef,UInt32,UInt32,AudioQueueBufferRef *);
OSStatus AudioQueueFreeBuffer(AudioQueueRef,AudioQueueBufferRef);
OSStatus AudioQueueEnqueueBuffer(AudioQueueRef,AudioQueueBufferRef,UInt32,const AudioStreamPacketDescription *);
OSStatus AudioQueueStart(AudioQueueRef,const AudioTimeStamp *);
OSStatus AudioQueueStop(AudioQueueRef,Boolean);
OSStatus AudioQueuePause(AudioQueueRef);
OSStatus AudioQueueReset(AudioQueueRef);
OSStatus AudioQueueFlush(AudioQueueRef);
OSStatus AudioQueueDispose(AudioQueueRef,Boolean);
OSStatus AudioQueueSetParameter(AudioQueueRef,AudioQueueParameterID,AudioQueueParameterValue);
OSStatus AudioQueueGetParameter(AudioQueueRef,AudioQueueParameterID,AudioQueueParameterValue *);
OSStatus AudioQueueGetProperty(AudioQueueRef,AudioQueuePropertyID,void *,UInt32 *);
OSStatus AudioQueueGetPropertySize(AudioQueueRef,AudioQueuePropertyID,UInt32 *);
OSStatus AudioQueueAddPropertyListener(AudioQueueRef,AudioQueuePropertyID,AudioQueuePropertyListenerProc,void *);
OSStatus AudioQueueRemovePropertyListener(AudioQueueRef,AudioQueuePropertyID,AudioQueuePropertyListenerProc,void *);
OSStatus AudioQueueCreateTimeline(AudioQueueRef,AudioQueueTimelineRef *);
OSStatus AudioQueueDisposeTimeline(AudioQueueRef,AudioQueueTimelineRef);
OSStatus AudioQueueGetCurrentTime(AudioQueueRef,AudioQueueTimelineRef,AudioTimeStamp *,Boolean *);

OSStatus AudioQueueSetProperty(AudioQueueRef,AudioQueuePropertyID,const void *,UInt32);
