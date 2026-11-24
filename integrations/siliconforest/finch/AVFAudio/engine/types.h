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

/* Own engine/node declarations from public API facts (objc2-avf-audio0.3.2). */
@class AVAudioEngine,AVAudioNode,AVAudioConnectionPoint,AVAudioMixingDestination,AVAudioMixerNode,AVAudioPlayerNode,AVAudioSourceNode,AVAudioSinkNode,AVAudioIONode,AVAudioInputNode,AVAudioOutputNode,AUAudioUnit;
typedef NSUInteger AVAudioNodeBus,AVAudioPlayerNodeBufferOptions;
typedef NSInteger AVAudioEngineManualRenderingStatus,AVAudioEngineManualRenderingMode,AVAudioPlayerNodeCompletionCallbackType;
typedef NSInteger AVAudio3DMixingRenderingAlgorithm,AVAudio3DMixingSourceMode,AVAudio3DMixingPointSourceInHeadMode,AVAudioVoiceProcessingSpeechActivityEvent,AVAudioVoiceProcessingOtherAudioDuckingLevel;
typedef struct {float x,y,z;} AVAudio3DPoint;
typedef struct {BOOL enableAdvancedDucking;AVAudioVoiceProcessingOtherAudioDuckingLevel duckingLevel;} AVAudioVoiceProcessingOtherAudioDuckingConfiguration;
typedef struct OpaqueAudioComponentInstance *AudioUnit;
typedef void (^AVAudioNodeTapBlock)(AVAudioPCMBuffer *,AVAudioTime *);
typedef void (^AVAudioPlayerNodeCompletionHandler)(AVAudioPlayerNodeCompletionCallbackType);
typedef OSStatus (^AVAudioSourceNodeRenderBlock)(BOOL *,const AudioTimeStamp *,AVAudioFrameCount,AudioBufferList *);
typedef OSStatus (^AVAudioSinkNodeReceiverBlock)(const AudioTimeStamp *,AVAudioFrameCount,const AudioBufferList *);
typedef const AudioBufferList *(^AVAudioIONodeInputBlock)(AVAudioFrameCount);
typedef AVAudioEngineManualRenderingStatus (^AVAudioEngineManualRenderingBlock)(AVAudioFrameCount,AudioBufferList *,OSStatus *);
typedef OSStatus (^AUMIDIOutputEventBlock)(int64_t,uint8_t,NSInteger,const uint8_t *);
#define kAudioUnitErr_InvalidElement (-10877)
#define kAudioUnitErr_FormatNotSupported (-10868)
#define kAudioUnitErr_NoConnection (-10876)
@protocol AVAudioMixing <NSObject>
@property float volume,pan,rate,reverbBlend,obstruction,occlusion;
@property AVAudio3DPoint position;
@property AVAudio3DMixingRenderingAlgorithm renderingAlgorithm;
@property AVAudio3DMixingSourceMode sourceMode;
@property AVAudio3DMixingPointSourceInHeadMode pointSourceInHeadMode;
- (AVAudioMixingDestination *)destinationForMixer:(AVAudioNode *)mixer bus:(AVAudioNodeBus)bus;
@end

@class CASpatialAudioExperience;
typedef void (^AVAudioNodeCompletionHandler)(void);
typedef struct OpaqueMusicSequence *MusicSequence;
typedef struct MIDIEventList MIDIEventList;
typedef OSStatus (^AUMIDIEventListBlock)(UInt8,const MIDIEventList *);
