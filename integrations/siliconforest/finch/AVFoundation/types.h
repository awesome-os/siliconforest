/* SPDX-License-Identifier: MIT */
#import <CoreMedia/CoreMedia.h>
#import <CoreGraphics/CoreGraphics.h>
#import <AVFAudio/AVFAudio.h>
#import <AVFoundation/Numeric.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
typedef NSString *AVMediaType,*AVFileType,*AVMediaCharacteristic,*AVMetadataIdentifier,*AVMetadataKeySpace,*AVMetadataFormat,*AVTrackAssociationType,*AVAudioTimePitchAlgorithm,*AVCustomMediaSelectionScheme;
typedef id AVMetadataKey;
typedef NSInteger AVKeyValueStatus,AVAssetReaderStatus,AVAssetWriterStatus;
typedef NSUInteger AVAssetReferenceRestrictions;
@class UTType,AVFMetadataEntry,AVAudioMix,AVAssetCache,AVAssetResourceLoader,AVAssetVariant,AVCompositionTrack,AVAssetWriterInputGroup;
typedef struct {BOOL sampleIsFullSync,sampleIsPartialSync,sampleIsDroppable;} AVSampleCursorSyncInfo;
typedef struct {BOOL sampleIndicatesWhetherItHasDependentSamples,sampleHasDependentSamples,sampleIndicatesWhetherItDependsOnOthers,sampleDependsOnOthers,sampleIndicatesWhetherItHasRedundantCoding,sampleHasRedundantCoding;} AVSampleCursorDependencyInfo;
typedef struct {BOOL audioSampleIsIndependentlyDecodable;NSInteger audioSamplePacketRefreshCount;} AVSampleCursorAudioDependencyInfo;
typedef struct {int64_t offset,length;} AVSampleCursorStorageRange;
typedef struct {int64_t chunkSampleCount;BOOL chunkHasUniformSampleSizes,chunkHasUniformSampleDurations,chunkHasUniformFormatDescriptions;} AVSampleCursorChunkInfo;

/* Implemented by retained PD Foundation compatibility additions. */
@interface NSCoder (SFMediaSecureDecodeDeclarations)
- (id)decodeObjectOfClass:(Class)cls forKey:(NSString *)key;
@end
