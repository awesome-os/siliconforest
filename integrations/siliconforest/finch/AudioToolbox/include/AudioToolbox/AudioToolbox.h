/* SPDX-License-Identifier: MIT */
/* Device-independent AudioToolbox declarations authored from public type facts. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <CoreAudioTypes/CoreAudioTypes.h>
#include <CoreServices/CoreServices.h>
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <AudioToolbox/Numeric.h>
#include <AudioToolbox/ExtNumeric.h>
#include <AudioToolbox/PrimeNumeric.h>
#include <AudioToolbox/DitherNumeric.h>
#define kAFInfoDictionary_ApproximateDurationInSeconds "approximate duration in seconds"
typedef struct {UInt32 mIsIndependentlyDecodable,mPreRollCount,mFlags,mReserved;} AudioStreamPacketDependencyDescription;
typedef struct OpaqueAudioFileID *AudioFileID;
typedef struct OpaqueAudioFileStreamID *AudioFileStreamID;
typedef struct OpaqueAudioConverter *AudioConverterRef;
typedef struct OpaqueExtAudioFile *ExtAudioFileRef;
typedef uint32_t AudioFileTypeID,AudioFilePropertyID,AudioFileFlags,AudioFileStreamPropertyID,AudioFileStreamParseFlags,AudioFileStreamPropertyFlags,AudioFileStreamSeekFlags;
typedef uint32_t AudioConverterPropertyID,AudioConverterOptions,AudioFormatPropertyID,ExtAudioFilePropertyID;
typedef uint32_t AudioPanningMode,AudioBalanceFadeType,AudioBytePacketTranslationFlags;
typedef int8_t AudioFilePermissions;
typedef struct {OSType mType,mSubType,mManufacturer;} AudioClassDescription;
typedef struct {UInt32 leadingFrames,trailingFrames;} AudioConverterPrimeInfo;
typedef struct {AudioStreamBasicDescription mASBD;const void *mMagicCookie;UInt32 mMagicCookieSize;} AudioFormatInfo;
typedef struct {SInt64 mNumberValidFrames;SInt32 mPrimingFrames,mRemainderFrames;} AudioFilePacketTableInfo;
typedef struct {SInt64 mFrame,mPacket;UInt32 mFrameOffsetInPacket;} AudioFramePacketTranslation;
typedef struct {SInt64 mByte,mPacket;UInt32 mByteOffsetInPacket;AudioBytePacketTranslationFlags mFlags;} AudioBytePacketTranslation;
typedef struct {AudioPanningMode mPanningMode;UInt32 mCoordinateFlags;Float32 mCoordinates[3],mGainScale;const AudioChannelLayout *mOutputChannelMap;} AudioPanningInfo;
typedef struct {Float32 mLeftRightBalance,mBackFrontFade;AudioBalanceFadeType mType;const AudioChannelLayout *mChannelLayout;} AudioBalanceFade;
typedef OSStatus (*AudioConverterComplexInputDataProc)(AudioConverterRef,UInt32*,AudioBufferList*,AudioStreamPacketDescription**,void*);
typedef AudioConverterComplexInputDataProc AudioConverterComplexInputDataProcRealtimeSafe;
typedef OSStatus (*AudioConverterInputDataProc)(AudioConverterRef,UInt32*,void**,void*);
typedef OSStatus (*AudioFile_ReadProc)(void*,SInt64,UInt32,void*,UInt32*);
typedef OSStatus (*AudioFile_WriteProc)(void*,SInt64,UInt32,const void*,UInt32*);
typedef SInt64 (*AudioFile_GetSizeProc)(void*);
typedef OSStatus (*AudioFile_SetSizeProc)(void*,SInt64);
typedef void (*AudioFileStream_PropertyListenerProc)(void*,AudioFileStreamID,AudioFileStreamPropertyID,AudioFileStreamPropertyFlags*);
typedef void (*AudioFileStream_PacketsProc)(void*,UInt32,UInt32,const void*,AudioStreamPacketDescription*);
#ifndef SF_MEDIA_PREPROCESS
#include <AudioToolbox/Generated.h>
#endif
