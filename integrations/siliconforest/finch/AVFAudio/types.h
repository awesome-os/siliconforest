/* SPDX-License-Identifier: MIT */
#include <AudioToolbox/AudioToolbox.h>
#include <AVFAudio/Numeric.h>
#define AVAUDIOFORMAT_HAVE_CMFORMATDESCRIPTION 1
@class AVAudioTime,AVAudioFormat,AVAudioChannelLayout,AVAudioBuffer,AVAudioPCMBuffer,AVAudioCompressedBuffer,AVAudioConverter,AVAudioFile;
typedef uint32_t AVAudioFrameCount,AVAudioChannelCount,AVAudioPacketCount;
typedef int64_t AVAudioFramePosition;
typedef NSUInteger AVAudioCommonFormat,AVAudioConverterPrimeMethod;
typedef NSInteger AVAudioConverterInputStatus,AVAudioConverterOutputStatus,AVAudioQuality;
typedef AudioConverterPrimeInfo AVAudioConverterPrimeInfo;
typedef AVAudioBuffer *(^AVAudioConverterInputBlock)(AVAudioPacketCount,AVAudioConverterInputStatus*);

/* Three public CM audio-description calls used by AVAudioFormat.m. */
typedef const struct opaqueCMFormatDescription *CMAudioFormatDescriptionRef;
const AudioStreamBasicDescription *CMAudioFormatDescriptionGetStreamBasicDescription(CMAudioFormatDescriptionRef);
const AudioChannelLayout *CMAudioFormatDescriptionGetChannelLayout(CMAudioFormatDescriptionRef,size_t *);
OSStatus CMAudioFormatDescriptionCreate(CFAllocatorRef,const AudioStreamBasicDescription *,size_t,const AudioChannelLayout *,size_t,const void *,CFDictionaryRef,CMAudioFormatDescriptionRef *);
