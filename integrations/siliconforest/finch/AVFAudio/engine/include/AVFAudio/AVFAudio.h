/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class AVAudioTime, AVAudioChannelLayout, AVAudioFormat, AVAudioBuffer, AVAudioPCMBuffer, AVAudioCompressedBuffer, AVAudioConverter, AVAudioFile, AVAudioConnectionPoint, AVAudioNode, AVAudioMixingDestination, AVAudioMixerNode, AVAudioPlayerNode, AVAudioSourceNode, AVAudioSinkNode, AVAudioIONode, AVAudioInputNode, AVAudioOutputNode, AVAudioEngine;
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

extern NSString *const AVSampleRateConverterAlgorithm_Normal;
extern NSString *const AVSampleRateConverterAlgorithm_Mastering;
extern NSString *const AVSampleRateConverterAlgorithm_MinimumPhase;
extern NSString *const AVAudioEngineConfigurationChangeNotification;
extern NSString *const AVAudioFileTypeKey;
extern NSString *const AVFormatIDKey;
extern NSString *const AVSampleRateKey;
extern NSString *const AVNumberOfChannelsKey;
extern NSString *const AVLinearPCMBitDepthKey;
extern NSString *const AVLinearPCMIsBigEndianKey;
extern NSString *const AVLinearPCMIsFloatKey;
extern NSString *const AVLinearPCMIsNonInterleaved;
extern NSString *const AVChannelLayoutKey;
extern NSString *const AVAudioApplicationInputMuteHandlerChangeNotification;
extern NSString *const AVAudioApplicationInputMuteStateChangeNotification;
extern NSString *const AVAudioApplicationMuteHandlerIsActiveKey;
extern NSString *const AVAudioApplicationMuteStateKey;
extern NSString *const AVAudioBitRateStrategy_Constant;
extern NSString *const AVAudioBitRateStrategy_LongTermAverage;
extern NSString *const AVAudioBitRateStrategy_Variable;
extern NSString *const AVAudioBitRateStrategy_VariableConstrained;
extern NSString *const AVAudioRemoteInputBufferGainKey;
extern NSString *const AVAudioRemoteInputBufferSpeexFrameQualitiesKey;
extern NSString *const AVAudioRemoteInputStartAnchorPointKey;
extern NSString *const AVAudioRemoteInputStartTimeKey;
extern NSString *const AVAudioSequencerInfoDictionaryKeyAlbum;
extern NSString *const AVAudioSequencerInfoDictionaryKeyApproximateDurationInSeconds;
extern NSString *const AVAudioSequencerInfoDictionaryKeyArtist;
extern NSString *const AVAudioSequencerInfoDictionaryKeyChannelLayout;
extern NSString *const AVAudioSequencerInfoDictionaryKeyComments;
extern NSString *const AVAudioSequencerInfoDictionaryKeyComposer;
extern NSString *const AVAudioSequencerInfoDictionaryKeyCopyright;
extern NSString *const AVAudioSequencerInfoDictionaryKeyEncodingApplication;
extern NSString *const AVAudioSequencerInfoDictionaryKeyGenre;
extern NSString *const AVAudioSequencerInfoDictionaryKeyISRC;
extern NSString *const AVAudioSequencerInfoDictionaryKeyKeySignature;
extern NSString *const AVAudioSequencerInfoDictionaryKeyLyricist;
extern NSString *const AVAudioSequencerInfoDictionaryKeyNominalBitRate;
extern NSString *const AVAudioSequencerInfoDictionaryKeyRecordedDate;
extern NSString *const AVAudioSequencerInfoDictionaryKeySourceBitDepth;
extern NSString *const AVAudioSequencerInfoDictionaryKeySourceEncoder;
extern NSString *const AVAudioSequencerInfoDictionaryKeySubTitle;
extern NSString *const AVAudioSequencerInfoDictionaryKeyTempo;
extern NSString *const AVAudioSequencerInfoDictionaryKeyTimeSignature;
extern NSString *const AVAudioSequencerInfoDictionaryKeyTitle;
extern NSString *const AVAudioSequencerInfoDictionaryKeyTrackNumber;
extern NSString *const AVAudioSequencerInfoDictionaryKeyYear;
extern NSString *const AVAudioSessionActivationAccessibilityPreferenceKey;
extern NSString *const AVAudioSessionActivationCallDirectionKey;
extern NSString *const AVAudioSessionActivationCallTypeKey;
extern NSString *const AVAudioSessionActivationDeviceUIDKey;
extern NSString *const AVAudioSessionActivationTriggerKey;
extern NSString *const AVAudioSessionAllowEnhanceDialogueChangeNotification;
extern NSString *const AVAudioSessionAllowEnhanceDialogueStateKey;
extern NSString *const AVAudioSessionAvailableInputsChangeNotification;
extern NSString *const AVAudioSessionAvailableOutputsChangeNotification;
extern NSString *const AVAudioSessionAvailableSampleRatesChangeNotification;
extern NSString *const AVAudioSessionBeaconBudgetChangeNotification;
extern NSString *const AVAudioSessionCategoryAlarm;
extern NSString *const AVAudioSessionCategoryAmbient;
extern NSString *const AVAudioSessionCategoryAudioProcessing;
extern NSString *const AVAudioSessionCategoryDropInCall;
extern NSString *const AVAudioSessionCategoryEARC;
extern NSString *const AVAudioSessionCategoryEmergencyAlert;
extern NSString *const AVAudioSessionCategoryEmergencyAlert_CanBeMuted;
extern NSString *const AVAudioSessionCategoryFindMyDevice;
extern NSString *const AVAudioSessionCategoryFindMyPhone;
extern NSString *const AVAudioSessionCategoryHearingTest;
extern NSString *const AVAudioSessionCategoryMultiRoute;
extern NSString *const AVAudioSessionCategoryPhoneCall;
extern NSString *const AVAudioSessionCategoryPlayAndRecord;
extern NSString *const AVAudioSessionCategoryPlayback;
extern NSString *const AVAudioSessionCategoryPlaybackNoSpeaker;
extern NSString *const AVAudioSessionCategoryRecord;
extern NSString *const AVAudioSessionCategoryRingtone;
extern NSString *const AVAudioSessionCategoryRingtonePreview;
extern NSString *const AVAudioSessionCategorySoloAmbient;
extern NSString *const AVAudioSessionCategorySpeechDetection;
extern NSString *const AVAudioSessionCategorySystemSoundsAndHaptics;
extern NSString *const AVAudioSessionCategoryTTYCall;
extern NSString *const AVAudioSessionCategoryVoiceCommand;
extern NSString *const AVAudioSessionCategoryVoiceMail;
extern NSString *const AVAudioSessionCategoryVoiceOver;
extern NSString *const AVAudioSessionCategoryWaterEjection;
extern NSString *const AVAudioSessionConcurrentAirPlayAudioPreferenceChangeNotification;
extern NSString *const AVAudioSessionConcurrentAirPlayAudioPreferredKey;
extern NSString *const AVAudioSessionConformSessionAudioSessionIDKey;
extern NSString *const AVAudioSessionConformSessionShadowingOptionsKey;
extern NSString *const AVAudioSessionInputMuteChangeNotification;
extern NSString *const AVAudioSessionInputMuteContextExternalProvenance;
extern NSString *const AVAudioSessionInputMuteContextInfoKey;
extern NSString *const AVAudioSessionInputMuteKey;
extern NSString *const AVAudioSessionInputSampleRateChangeNotification;
extern NSString *const AVAudioSessionInterruptionNotification;
extern NSString *const AVAudioSessionInterruptionOptionKey;
extern NSString *const AVAudioSessionInterruptionReasonKey;
extern NSString *const AVAudioSessionInterruptionTypeKey;
extern NSString *const AVAudioSessionInterruptionWasSuspendedKey;
extern NSString *const AVAudioSessionInterruptorBundleIDKey;
extern NSString *const AVAudioSessionInterruptorNameKey;
extern NSString *const AVAudioSessionInterruptorSessionIDKey;
extern NSString *const AVAudioSessionLocationLower;
extern NSString *const AVAudioSessionLocationUpper;
extern NSString *const AVAudioSessionMediaServicesWereLostNotification;
extern NSString *const AVAudioSessionMediaServicesWereResetNotification;
extern NSString *const AVAudioSessionMicrophoneInjectionCapabilitiesChangeNotification;
extern NSString *const AVAudioSessionMicrophoneInjectionIsAvailableKey;
extern NSString *const AVAudioSessionModeBeacon;
extern NSString *const AVAudioSessionModeDefault;
extern NSString *const AVAudioSessionModeDualRoute;
extern NSString *const AVAudioSessionModeEmergencyAlert;
extern NSString *const AVAudioSessionModeGameChat;
extern NSString *const AVAudioSessionModeHapticsConstantVolume;
extern NSString *const AVAudioSessionModeHearingAccessibility;
extern NSString *const AVAudioSessionModeLivePhoto;
extern NSString *const AVAudioSessionModeLocatePhone;
extern NSString *const AVAudioSessionModeLowLatency;
extern NSString *const AVAudioSessionModeMeasurement;
extern NSString *const AVAudioSessionModeMoviePlayback;
extern NSString *const AVAudioSessionModeRaw;
extern NSString *const AVAudioSessionModeRemoteVoiceChat;
extern NSString *const AVAudioSessionModeSOS;
extern NSString *const AVAudioSessionModeShortFormVideo;
extern NSString *const AVAudioSessionModeSleepTimer;
extern NSString *const AVAudioSessionModeSoundRecognition;
extern NSString *const AVAudioSessionModeSpatialCapture;
extern NSString *const AVAudioSessionModeSpatialRecording;
extern NSString *const AVAudioSessionModeSpeechRecognition;
extern NSString *const AVAudioSessionModeSpokenAudio;
extern NSString *const AVAudioSessionModeStereoCapture;
extern NSString *const AVAudioSessionModeTelephonyWebcam;
extern NSString *const AVAudioSessionModeVideoChat;
extern NSString *const AVAudioSessionModeVideoChatForMedia;
extern NSString *const AVAudioSessionModeVideoRecording;
extern NSString *const AVAudioSessionModeVoiceChat;
extern NSString *const AVAudioSessionModeVoiceMessages;
extern NSString *const AVAudioSessionModeVoicePrompt;
extern NSString *const AVAudioSessionMuteStateKey;
extern NSString *const AVAudioSessionOrientationBack;
extern NSString *const AVAudioSessionOrientationBottom;
extern NSString *const AVAudioSessionOrientationFront;
extern NSString *const AVAudioSessionOrientationLeft;
extern NSString *const AVAudioSessionOrientationRight;
extern NSString *const AVAudioSessionOrientationTop;
extern NSString *const AVAudioSessionOutputMuteStateChangeNotification;
extern NSString *const AVAudioSessionOutputSampleRateChangeNotification;
extern NSString *const AVAudioSessionPickableRouteChangeNotification;
extern NSString *const AVAudioSessionPolarPatternCardioid;
extern NSString *const AVAudioSessionPolarPatternOmnidirectional;
extern NSString *const AVAudioSessionPolarPatternStereo;
extern NSString *const AVAudioSessionPolarPatternSubcardioid;
extern NSString *const AVAudioSessionPortAVB;
extern NSString *const AVAudioSessionPortAirPlay;
extern NSString *const AVAudioSessionPortBluetoothA2DP;
extern NSString *const AVAudioSessionPortBluetoothHFP;
extern NSString *const AVAudioSessionPortBluetoothLE;
extern NSString *const AVAudioSessionPortBuiltInMic;
extern NSString *const AVAudioSessionPortBuiltInReceiver;
extern NSString *const AVAudioSessionPortBuiltInSpeaker;
extern NSString *const AVAudioSessionPortCarAudio;
extern NSString *const AVAudioSessionPortContinuityMicrophone;
extern NSString *const AVAudioSessionPortDisplayPort;
extern NSString *const AVAudioSessionPortFireWire;
extern NSString *const AVAudioSessionPortHDMI;
extern NSString *const AVAudioSessionPortHeadphones;
extern NSString *const AVAudioSessionPortHeadsetMic;
extern NSString *const AVAudioSessionPortLineIn;
extern NSString *const AVAudioSessionPortLineOut;
extern NSString *const AVAudioSessionPortPCI;
extern NSString *const AVAudioSessionPortSPDIF;
extern NSString *const AVAudioSessionPortThunderbolt;
extern NSString *const AVAudioSessionPortUSBAudio;
extern NSString *const AVAudioSessionPortVirtual;
extern NSString *const AVAudioSessionRenderingCapabilitiesChangeNotification;
extern NSString *const AVAudioSessionRenderingModeChangeNotification;
extern NSString *const AVAudioSessionRenderingModeNewRenderingModeKey;
extern NSString *const AVAudioSessionRouteChangeNotification;
extern NSString *const AVAudioSessionRouteChangePreviousRouteKey;
extern NSString *const AVAudioSessionRouteChangeReasonKey;
extern NSString *const AVAudioSessionRoutingContextChangeNotification;
extern NSString *const AVAudioSessionSilenceSecondaryAudioHintNotification;
extern NSString *const AVAudioSessionSilenceSecondaryAudioHintTypeKey;
extern NSString *const AVAudioSessionSourceContentBestAvailableContentTypeKey;
extern NSString *const AVAudioSessionSourceContentCurrentlyPlayingChannelCountKey;
extern NSString *const AVAudioSessionSourceContentCurrentlyPlayingFormatKey;
extern NSString *const AVAudioSessionSourceContentCurrentlyPlayingIsEligibleForSpatializationKey;
extern NSString *const AVAudioSessionSourceContentCurrentlyPlayingIsSpatializedKey;
extern NSString *const AVAudioSessionSpatialAudioEnabledKey;
extern NSString *const AVAudioSessionSpatialPlaybackCapabilitiesChangedNotification;
extern NSString *const AVAudioSessionUserIntentToUnmuteOutputNotification;
extern NSString *const AVAudioUnitComponentManagerRegistrationsChangedNotification;
extern NSString *const AVAudioUnitComponentTagsDidChangeNotification;
extern NSString *const AVAudioUnitManufacturerNameApple;
extern NSString *const AVAudioUnitTypeEffect;
extern NSString *const AVAudioUnitTypeFormatConverter;
extern NSString *const AVAudioUnitTypeGenerator;
extern NSString *const AVAudioUnitTypeMIDIProcessor;
extern NSString *const AVAudioUnitTypeMixer;
extern NSString *const AVAudioUnitTypeMusicDevice;
extern NSString *const AVAudioUnitTypeMusicEffect;
extern NSString *const AVAudioUnitTypeOfflineEffect;
extern NSString *const AVAudioUnitTypeOutput;
extern NSString *const AVAudioUnitTypePanner;
extern NSString *const AVEncoderASPFrequencyKey;
extern NSString *const AVEncoderAudioQualityForVBRKey;
extern NSString *const AVEncoderAudioQualityKey;
extern NSString *const AVEncoderBitDepthHintKey;
extern NSString *const AVEncoderBitRateKey;
extern NSString *const AVEncoderBitRatePerChannelKey;
extern NSString *const AVEncoderBitRateStrategyKey;
extern NSString *const AVEncoderContentSourceKey;
extern NSString *const AVEncoderDynamicRangeControlConfigurationKey;
extern NSString *const AVSampleRateConverterAlgorithmKey;
extern NSString *const AVSampleRateConverterAudioQualityKey;
extern NSString *const AVSpeechSynthesisAvailableVoicesDidChangeNotification;
extern NSString *const AVSpeechSynthesisIPANotationAttribute;
extern NSString *const AVSpeechSynthesisVoiceIdentifierAlex;
extern NSString *const AVVCCurrentInputDeviceLatencyKey;
extern NSString *const AVVCCurrentInputSafetyOffsetKey;
extern NSString *const AVVCCurrentOutputDeviceLatencyKey;
extern NSString *const AVVCCurrentOutputSafetyOffsetKey;
extern NSString *const AVVCGetInputChannelInfo_DSPCountKey;
extern NSString *const AVVCGetInputChannelInfo_MicCountKey;
extern NSString *const AVVCGetInputChannelInfo_SpeakerReferenceCountKey;
extern NSString *const AVVCGetInputChannelInfo_UndifferentiatedCountKey;
extern NSString *const AVVoiceActivationDeviceIDKey;
extern NSString *const AVVoiceActivationModeKey;
extern NSString *const AVVoiceControllerATVRemoteInputRoute;
extern NSString *const AVVoiceControllerAlertBehavior;
extern NSString *const AVVoiceControllerBluetoothDoAPRoute;
extern NSString *const AVVoiceControllerMetricAudioSessionSetActiveTime;
extern NSString *const AVVoiceControllerMetricAudioSessionSetInactiveTime;
extern NSString *const AVVoiceControllerMetricDataBeginHostTime;
extern NSString *const AVVoiceControllerMetricDataDurationMilliSeconds;
extern NSString *const AVVoiceControllerMetricDataEndHostTime;
extern NSString *const AVVoiceControllerNoRecordRoute;
extern NSString *const AVVoiceControllerRemoteInputDarwinRoute;
extern NSString *const AVVoiceControllerRemoteInputJarvisRoute;
extern NSString *const AVVoiceControllerStartTimeKey;

@interface AVAudioTime : NSObject { AudioTimeStamp _ats; double _sampleRate; }
- (instancetype)initWithAudioTimeStamp:(const AudioTimeStamp *)ts sampleRate:(double)sampleRate;
- (instancetype)initWithHostTime:(uint64_t)hostTime;
- (instancetype)initWithSampleTime:(AVAudioFramePosition)sampleTime atRate:(double)sampleRate;
- (instancetype)initWithHostTime:(uint64_t)hostTime sampleTime:(AVAudioFramePosition)sampleTime atRate:(double)sampleRate;
+ (instancetype)timeWithAudioTimeStamp:(const AudioTimeStamp *)ts sampleRate:(double)sampleRate;
+ (instancetype)timeWithHostTime:(uint64_t)hostTime;
+ (instancetype)timeWithSampleTime:(AVAudioFramePosition)sampleTime atRate:(double)sampleRate;
+ (instancetype)timeWithHostTime:(uint64_t)hostTime sampleTime:(AVAudioFramePosition)sampleTime atRate:(double)sampleRate;
+ (uint64_t)hostTimeForSeconds:(NSTimeInterval)seconds;
+ (NSTimeInterval)secondsForHostTime:(uint64_t)hostTime;
- (BOOL)isHostTimeValid;
- (BOOL)isSampleTimeValid;
- (uint64_t)hostTime;
- (AVAudioFramePosition)sampleTime;
- (double)sampleRate;
- (AudioTimeStamp)audioTimeStamp;
- (AVAudioTime *)extrapolateTimeFromAnchor:(AVAudioTime *)anchor;
- (NSString *)description;
@property(readonly,getter=isHostTimeValid) BOOL hostTimeValid;
@property(readonly,getter=isSampleTimeValid) BOOL sampleTimeValid;
@end

@interface AVAudioChannelLayout : NSObject <NSCopying,NSSecureCoding> { AudioChannelLayout *_layout; AudioChannelLayoutTag _layoutTag; }
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithLayoutTag:(AudioChannelLayoutTag)tag;
- (instancetype)initWithLayout:(const AudioChannelLayout *)layout;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (void)dealloc;
- (AudioChannelLayoutTag)layoutTag;
- (const AudioChannelLayout *)layout;
- (AVAudioChannelCount)channelCount;
- (BOOL)isEqual:(id)object;
+ (instancetype)layoutWithLayoutTag:(AudioChannelLayoutTag)tag;
+ (instancetype)layoutWithLayout:(const AudioChannelLayout *)layout;
@end

@interface AVAudioFormat : NSObject <NSCopying,NSSecureCoding> { AudioStreamBasicDescription _asbd; AVAudioCommonFormat _commonFormat; AVAudioChannelLayout *_layout; void *_reserved; }
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithStreamDescription:(const AudioStreamBasicDescription *)asbd;
- (instancetype)initWithStreamDescription:(const AudioStreamBasicDescription *)asbd channelLayout:(AVAudioChannelLayout *)layout;
- (instancetype)initStandardFormatWithSampleRate:(double)rate channels:(AVAudioChannelCount)channels;
- (instancetype)initStandardFormatWithSampleRate:(double)rate channelLayout:(AVAudioChannelLayout *)layout;
- (instancetype)initWithCommonFormat:(AVAudioCommonFormat)format sampleRate:(double)rate channels:(AVAudioChannelCount)channels interleaved:(BOOL)interleaved;
- (instancetype)initWithCommonFormat:(AVAudioCommonFormat)format sampleRate:(double)rate interleaved:(BOOL)interleaved channelLayout:(AVAudioChannelLayout *)layout;
- (instancetype)initWithSettings:(NSDictionary<NSString *,id> *)settings;
- (instancetype)initWithCMAudioFormatDescription:(CMAudioFormatDescriptionRef)description;
- (CMAudioFormatDescriptionRef)formatDescription;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (void)encodeWithCoder:(NSCoder *)coder;
- (void)dealloc;
- (BOOL)isEqual:(id)object;
- (BOOL)isStandard;
- (AVAudioCommonFormat)commonFormat;
- (AVAudioChannelCount)channelCount;
- (double)sampleRate;
- (BOOL)isInterleaved;
- (const AudioStreamBasicDescription *)streamDescription;
- (AVAudioChannelLayout *)channelLayout;
- (NSData *)magicCookie;
- (void)setMagicCookie:(NSData *)cookie;
- (NSString *)description;
- (NSDictionary<NSString *,id> *)settings;
@property(readonly,getter=isStandard) BOOL standard;
@property(readonly,getter=isInterleaved) BOOL interleaved;
@end

@interface AVAudioBuffer : NSObject { @protected void *_impl; }
- (AVAudioFormat *)format;
- (const AudioBufferList *)audioBufferList;
- (AudioBufferList *)mutableAudioBufferList;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
- (void)dealloc;
@end

@interface AVAudioPCMBuffer : AVAudioBuffer
- (instancetype)init;
- (instancetype)initWithPCMFormat:(AVAudioFormat *)format frameCapacity:(AVAudioFrameCount)capacity;
- (instancetype)initWithPCMFormat:(AVAudioFormat *)format bufferListNoCopy:(const AudioBufferList *)list deallocator:(void (^)(const AudioBufferList *))deallocator;
- (AVAudioFrameCount)frameCapacity;
- (AVAudioFrameCount)frameLength;
- (void)setFrameLength:(AVAudioFrameCount)length;
- (NSUInteger)stride;
- (float *const *)floatChannelData;
- (int16_t *const *)int16ChannelData;
- (int32_t *const *)int32ChannelData;
- (id)copyWithZone:(NSZone *)zone;
- (id)mutableCopyWithZone:(NSZone *)zone;
@end

@interface AVAudioCompressedBuffer : AVAudioBuffer
- (instancetype)init;
- (instancetype)initWithFormat:(AVAudioFormat *)format packetCapacity:(AVAudioPacketCount)capacity;
- (instancetype)initWithFormat:(AVAudioFormat *)format packetCapacity:(AVAudioPacketCount)capacity maximumPacketSize:(NSInteger)maximumPacketSize;
- (AVAudioPacketCount)packetCapacity;
- (AVAudioPacketCount)packetCount;
- (void)setPacketCount:(AVAudioPacketCount)count;
- (NSInteger)maximumPacketSize;
- (void *)data;
- (uint32_t)byteCapacity;
- (uint32_t)byteLength;
- (void)setByteLength:(uint32_t)length;
- (AudioStreamPacketDescription *)packetDescriptions;
- (AudioStreamPacketDependencyDescription *)packetDependencies;
@end

@interface AVAudioConverter : NSObject { void *_impl; }
- (instancetype)initFromFormat:(AVAudioFormat *)fromFormat toFormat:(AVAudioFormat *)toFormat;
- (void)reset;
- (AVAudioFormat *)inputFormat;
- (AVAudioFormat *)outputFormat;
- (NSArray<NSNumber *> *)channelMap;
- (void)setChannelMap:(NSArray<NSNumber *> *)channelMap;
- (NSData *)magicCookie;
- (void)setMagicCookie:(NSData *)magicCookie;
- (BOOL)downmix;
- (void)setDownmix:(BOOL)downmix;
- (BOOL)dither;
- (void)setDither:(BOOL)dither;
- (NSInteger)sampleRateConverterQuality;
- (void)setSampleRateConverterQuality:(NSInteger)quality;
- (NSString *)sampleRateConverterAlgorithm;
- (void)setSampleRateConverterAlgorithm:(NSString *)algorithm;
- (AVAudioConverterPrimeMethod)primeMethod;
- (void)setPrimeMethod:(AVAudioConverterPrimeMethod)primeMethod;
- (AVAudioConverterPrimeInfo)primeInfo;
- (void)setPrimeInfo:(AVAudioConverterPrimeInfo)primeInfo;
- (BOOL)convertToBuffer:(AVAudioPCMBuffer *)outputBuffer fromBuffer:(const AVAudioPCMBuffer *)inputBuffer error:(NSError **)outError;
- (AVAudioConverterOutputStatus)convertToBuffer:(AVAudioBuffer *)outputBuffer error:(NSError **)outError withInputFromBlock:(AVAudioConverterInputBlock)inputBlock;
- (NSInteger)bitRate;
- (void)setBitRate:(NSInteger)bitRate;
- (NSString *)bitRateStrategy;
- (void)setBitRateStrategy:(NSString *)strategy;
- (NSInteger)maximumOutputPacketSize;
- (NSArray<NSNumber *> *)availableEncodeBitRates;
- (NSArray<NSNumber *> *)applicableEncodeBitRates;
- (NSArray<NSNumber *> *)availableEncodeSampleRates;
- (NSArray<NSNumber *> *)applicableEncodeSampleRates;
- (NSArray<NSNumber *> *)availableEncodeChannelLayoutTags;
- (void)dealloc;
@end

@interface AVAudioFile : NSObject
- (instancetype)init;
- (instancetype)initForReading:(NSURL *)fileURL error:(NSError **)outError;
- (instancetype)initForReading:(NSURL *)fileURL commonFormat:(AVAudioCommonFormat)format interleaved:(BOOL)interleaved error:(NSError **)outError;
- (instancetype)initForWriting:(NSURL *)fileURL settings:(NSDictionary<NSString *,id> *)settings error:(NSError **)outError;
- (instancetype)initForWriting:(NSURL *)fileURL settings:(NSDictionary<NSString *,id> *)settings commonFormat:(AVAudioCommonFormat)commonFormat interleaved:(BOOL)interleaved error:(NSError **)outError;
- (void)close;
- (BOOL)isOpen;
- (NSURL *)url;
- (AVAudioFormat *)fileFormat;
- (AVAudioFormat *)processingFormat;
- (AVAudioFramePosition)length;
- (AVAudioFramePosition)framePosition;
- (void)setFramePosition:(AVAudioFramePosition)position;
- (BOOL)readIntoBuffer:(AVAudioPCMBuffer *)buffer error:(NSError **)outError;
- (BOOL)readIntoBuffer:(AVAudioPCMBuffer *)buffer frameCount:(AVAudioFrameCount)frames error:(NSError **)outError;
- (BOOL)writeFromBuffer:(const AVAudioPCMBuffer *)buffer error:(NSError **)outError;
- (void)dealloc;
@property(readonly,getter=isOpen) BOOL open;
@end

@interface AVAudioConnectionPoint : NSObject { AVAudioNode *_node; AVAudioNodeBus _bus; }
- (instancetype)initWithNode:(AVAudioNode *)node bus:(AVAudioNodeBus)bus;
- (AVAudioNode *)node;
- (AVAudioNodeBus)bus;
@end

@interface AVAudioNode : NSObject <AVAudioMixing>
- (instancetype)init;
- (void)dealloc;
- (AVAudioEngine *)engine;
- (NSUInteger)numberOfInputs;
- (NSUInteger)numberOfOutputs;
- (AVAudioTime *)lastRenderTime;
- (NSTimeInterval)latency;
- (NSTimeInterval)outputPresentationLatency;
- (AUAudioUnit *)AUAudioUnit;
- (void)reset;
- (AVAudioFormat *)inputFormatForBus:(AVAudioNodeBus)bus;
- (AVAudioFormat *)outputFormatForBus:(AVAudioNodeBus)bus;
- (NSString *)nameForInputBus:(AVAudioNodeBus)bus;
- (NSString *)nameForOutputBus:(AVAudioNodeBus)bus;
- (void)installTapOnBus:(AVAudioNodeBus)bus bufferSize:(AVAudioFrameCount)bufferSize format:(AVAudioFormat *)format block:(AVAudioNodeTapBlock)tapBlock;
- (void)removeTapOnBus:(AVAudioNodeBus)bus;
@end

@interface AVAudioMixingDestination : NSObject
- (void)dealloc;
- (AVAudioConnectionPoint *)connectionPoint;
- (AVAudioMixingDestination *)destinationForMixer:(AVAudioNode *)mixer bus:(AVAudioNodeBus)bus;
- (float)volume;
- (void)setVolume:(float)v;
- (float)pan;
- (void)setPan:(float)v;
- (float)rate;
- (void)setRate:(float)v;
- (float)reverbBlend;
- (void)setReverbBlend:(float)v;
- (float)obstruction;
- (void)setObstruction:(float)v;
- (float)occlusion;
- (void)setOcclusion:(float)v;
- (AVAudio3DPoint)position;
- (void)setPosition:(AVAudio3DPoint)v;
- (AVAudio3DMixingRenderingAlgorithm)renderingAlgorithm;
- (void)setRenderingAlgorithm:(AVAudio3DMixingRenderingAlgorithm)v;
- (AVAudio3DMixingSourceMode)sourceMode;
- (void)setSourceMode:(AVAudio3DMixingSourceMode)v;
- (AVAudio3DMixingPointSourceInHeadMode)pointSourceInHeadMode;
- (void)setPointSourceInHeadMode:(AVAudio3DMixingPointSourceInHeadMode)v;
@end

@interface AVAudioMixerNode : AVAudioNode
- (instancetype)init;
- (void)dealloc;
- (float)outputVolume;
- (void)setOutputVolume:(float)v;
- (NSUInteger)numberOfInputs;
- (AVAudioNodeBus)nextAvailableInputBus;
- (void)reset;
@end

@interface AVAudioPlayerNode : AVAudioNode
- (instancetype)init;
- (void)dealloc;
- (BOOL)isPlaying;
- (void)scheduleBuffer:(AVAudioPCMBuffer *)buffer completionHandler:(AVAudioNodeCompletionHandler)completionHandler;
- (void)scheduleBuffer:(AVAudioPCMBuffer *)buffer completionCallbackType:(AVAudioPlayerNodeCompletionCallbackType)callbackType completionHandler:(AVAudioPlayerNodeCompletionHandler)completionHandler;
- (void)scheduleBuffer:(AVAudioPCMBuffer *)buffer atTime:(AVAudioTime *)when options:(AVAudioPlayerNodeBufferOptions)options completionHandler:(AVAudioNodeCompletionHandler)completionHandler;
- (void)scheduleBuffer:(AVAudioPCMBuffer *)buffer atTime:(AVAudioTime *)when options:(AVAudioPlayerNodeBufferOptions)options completionCallbackType:(AVAudioPlayerNodeCompletionCallbackType)callbackType completionHandler:(AVAudioPlayerNodeCompletionHandler)completionHandler;
- (void)scheduleSegment:(AVAudioFile *)file startingFrame:(AVAudioFramePosition)startFrame frameCount:(AVAudioFrameCount)numberFrames atTime:(AVAudioTime *)when completionHandler:(AVAudioNodeCompletionHandler)completionHandler;
- (void)scheduleSegment:(AVAudioFile *)file startingFrame:(AVAudioFramePosition)startFrame frameCount:(AVAudioFrameCount)numberFrames atTime:(AVAudioTime *)when completionCallbackType:(AVAudioPlayerNodeCompletionCallbackType)callbackType completionHandler:(AVAudioPlayerNodeCompletionHandler)completionHandler;
- (void)scheduleFile:(AVAudioFile *)file atTime:(AVAudioTime *)when completionHandler:(AVAudioNodeCompletionHandler)completionHandler;
- (void)scheduleFile:(AVAudioFile *)file atTime:(AVAudioTime *)when completionCallbackType:(AVAudioPlayerNodeCompletionCallbackType)callbackType completionHandler:(AVAudioPlayerNodeCompletionHandler)completionHandler;
- (void)prepareWithFrameCount:(AVAudioFrameCount)frameCount;
- (void)play;
- (void)playAtTime:(AVAudioTime *)when;
- (void)pause;
- (void)stop;
- (void)reset;
- (AVAudioTime *)nodeTimeForPlayerTime:(AVAudioTime *)playerTime;
- (AVAudioTime *)playerTimeForNodeTime:(AVAudioTime *)nodeTime;
@property(readonly,getter=isPlaying) BOOL playing;
@end

@interface AVAudioSourceNode : AVAudioNode
- (instancetype)initWithRenderBlock:(AVAudioSourceNodeRenderBlock)block;
- (instancetype)initWithFormat:(AVAudioFormat *)format renderBlock:(AVAudioSourceNodeRenderBlock)block;
- (void)dealloc;
@end

@interface AVAudioSinkNode : AVAudioNode
- (instancetype)initWithReceiverBlock:(AVAudioSinkNodeReceiverBlock)block;
- (void)dealloc;
@end

@interface AVAudioIONode : AVAudioNode
- (NSTimeInterval)presentationLatency;
- (AudioUnit)audioUnit;
- (BOOL)isVoiceProcessingEnabled;
- (BOOL)setVoiceProcessingEnabled:(BOOL)enabled error:(NSError **)outError;
@property(readonly,getter=isVoiceProcessingEnabled) BOOL voiceProcessingEnabled;
@end

@interface AVAudioInputNode : AVAudioIONode
- (void)dealloc;
- (BOOL)setManualRenderingInputPCMFormat:(AVAudioFormat *)format inputBlock:(AVAudioIONodeInputBlock)block;
- (BOOL)isVoiceProcessingBypassed;
- (void)setVoiceProcessingBypassed:(BOOL)v;
- (BOOL)isVoiceProcessingAGCEnabled;
- (void)setVoiceProcessingAGCEnabled:(BOOL)v;
- (BOOL)isVoiceProcessingInputMuted;
- (void)setVoiceProcessingInputMuted:(BOOL)v;
- (BOOL)setMutedSpeechActivityEventListener:(void (^)(AVAudioVoiceProcessingSpeechActivityEvent))listenerBlock;
- (AVAudioVoiceProcessingOtherAudioDuckingConfiguration)voiceProcessingOtherAudioDuckingConfiguration;
- (void)setVoiceProcessingOtherAudioDuckingConfiguration:(AVAudioVoiceProcessingOtherAudioDuckingConfiguration)v;
@property(readonly,getter=isVoiceProcessingBypassed) BOOL voiceProcessingBypassed;
@property(readonly,getter=isVoiceProcessingAGCEnabled) BOOL voiceProcessingAGCEnabled;
@property(readonly,getter=isVoiceProcessingInputMuted) BOOL voiceProcessingInputMuted;
@end

@interface AVAudioOutputNode : AVAudioIONode
- (void)dealloc;
- (CASpatialAudioExperience *)intendedSpatialExperience;
- (void)setIntendedSpatialExperience:(CASpatialAudioExperience *)e;
@end

@interface AVAudioEngine : NSObject
- (instancetype)init;
- (void)dealloc;
- (NSSet *)attachedNodes;
- (void)attachNode:(AVAudioNode *)node;
- (void)detachNode:(AVAudioNode *)node;
- (void)connect:(AVAudioNode *)node1 to:(AVAudioNode *)node2 fromBus:(AVAudioNodeBus)bus1 toBus:(AVAudioNodeBus)bus2 format:(AVAudioFormat *)format;
- (void)connect:(AVAudioNode *)node1 to:(AVAudioNode *)node2 format:(AVAudioFormat *)format;
- (void)connect:(AVAudioNode *)sourceNode toConnectionPoints:(NSArray<AVAudioConnectionPoint *> *)destNodes fromBus:(AVAudioNodeBus)sourceBus format:(AVAudioFormat *)format;
- (void)disconnectNodeInput:(AVAudioNode *)node bus:(AVAudioNodeBus)bus;
- (void)disconnectNodeInput:(AVAudioNode *)node;
- (void)disconnectNodeOutput:(AVAudioNode *)node bus:(AVAudioNodeBus)bus;
- (void)disconnectNodeOutput:(AVAudioNode *)node;
- (AVAudioConnectionPoint *)inputConnectionPointForNode:(AVAudioNode *)node inputBus:(AVAudioNodeBus)bus;
- (NSArray<AVAudioConnectionPoint *> *)outputConnectionPointsForNode:(AVAudioNode *)node outputBus:(AVAudioNodeBus)bus;
- (AVAudioOutputNode *)outputNode;
- (AVAudioInputNode *)inputNode;
- (AVAudioMixerNode *)mainMixerNode;
- (MusicSequence)musicSequence;
- (void)setMusicSequence:(MusicSequence)s;
- (BOOL)isRunning;
- (BOOL)isAutoShutdownEnabled;
- (void)setAutoShutdownEnabled:(BOOL)v;
- (void)prepare;
- (BOOL)startAndReturnError:(NSError **)outError;
- (void)pause;
- (void)stop;
- (void)reset;
- (BOOL)enableManualRenderingMode:(AVAudioEngineManualRenderingMode)mode format:(AVAudioFormat *)pcmFormat maximumFrameCount:(AVAudioFrameCount)maximumFrameCount error:(NSError **)outError;
- (void)disableManualRenderingMode;
- (BOOL)isInManualRenderingMode;
- (AVAudioEngineManualRenderingMode)manualRenderingMode;
- (AVAudioFormat *)manualRenderingFormat;
- (AVAudioFrameCount)manualRenderingMaximumFrameCount;
- (AVAudioFramePosition)manualRenderingSampleTime;
- (AVAudioEngineManualRenderingStatus)renderOffline:(AVAudioFrameCount)numberOfFrames toBuffer:(AVAudioPCMBuffer *)buffer error:(NSError **)outError;
- (AVAudioEngineManualRenderingBlock)manualRenderingBlock;
- (void)connectMIDI:(AVAudioNode *)sourceNode to:(AVAudioNode *)destinationNode format:(AVAudioFormat *)format block:(AUMIDIOutputEventBlock)tapBlock;
- (void)connectMIDI:(AVAudioNode *)sourceNode to:(AVAudioNode *)destinationNode format:(AVAudioFormat *)format eventListBlock:(AUMIDIEventListBlock)tapBlock;
- (void)connectMIDI:(AVAudioNode *)sourceNode toNodes:(NSArray<AVAudioNode *> *)destinationNodes format:(AVAudioFormat *)format block:(AUMIDIOutputEventBlock)tapBlock;
- (void)connectMIDI:(AVAudioNode *)sourceNode toNodes:(NSArray<AVAudioNode *> *)destinationNodes format:(AVAudioFormat *)format eventListBlock:(AUMIDIEventListBlock)tapBlock;
- (void)disconnectMIDI:(AVAudioNode *)sourceNode from:(AVAudioNode *)destinationNode;
- (void)disconnectMIDI:(AVAudioNode *)sourceNode fromNodes:(NSArray<AVAudioNode *> *)destinationNodes;
- (void)disconnectMIDIInput:(AVAudioNode *)node;
- (void)disconnectMIDIOutput:(AVAudioNode *)node;
@property(readonly,getter=isRunning) BOOL running;
@property(readonly,getter=isAutoShutdownEnabled) BOOL autoShutdownEnabled;
@property(readonly,getter=isInManualRenderingMode) BOOL inManualRenderingMode;
@end

@interface AVAudioNode (SFMixingProperties) <AVAudioMixing>
@end
extern NSString *const AVAudioEngineConfigurationChangeNotification;
