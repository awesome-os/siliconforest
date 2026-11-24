/* SPDX-License-Identifier: MIT */
/* Own ABI declarations based on Finch definitions and public type facts. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreAudioTypes/CoreAudioTypes.h>
#include <CoreVideo/CoreVideo.h>
#include <dispatch/dispatch.h>
#include <stdbool.h>
#include <stdint.h>
#include <CoreMedia/Numeric.h>
#define kCMTimeFlags_ImpliedValueFlagsMask 28
#define kCMTimeRoundingMethod_Default kCMTimeRoundingMethod_RoundHalfAwayFromZero
typedef int64_t CMTimeValue,CMTimeEpoch;
typedef int32_t CMTimeScale,CMPersistentTrackID;
typedef uint32_t CMTimeFlags,CMTimeRoundingMethod,CMBlockBufferFlags,CMAttachmentMode;
typedef uint32_t CMMediaType,CMPixelFormatType,CMVideoCodecType,CMAudioCodecType,CMMuxedStreamType;
typedef uint32_t CMClosedCaptionFormatType,CMTextFormatType,CMSubtitleFormatType,CMTimeCodeFormatType,CMMetadataFormatType;
typedef uint32_t CMAudioFormatDescriptionMask,CMTextDisplayFlags;
typedef int8_t CMTextJustificationValue;
typedef CFStringRef CMImageDescriptionFlavor,CMSoundDescriptionFlavor,CMTextDescriptionFlavor,CMClosedCaptionDescriptionFlavor,CMTimeCodeDescriptionFlavor,CMMetadataDescriptionFlavor;
typedef CFIndex CMItemCount,CMItemIndex;
#pragma pack(push,4)
typedef struct {int64_t value;int32_t timescale;uint32_t flags;int64_t epoch;} CMTime;
#pragma pack(pop)
typedef struct {CMTime start,duration;} CMTimeRange;
typedef struct {CMTimeRange source,target;} CMTimeMapping;
typedef struct {int32_t width,height;} CMVideoDimensions;
typedef struct {CMTime duration,presentationTimeStamp,decodeTimeStamp;} CMSampleTimingInfo;
typedef struct OpaqueCMBlockBuffer *CMBlockBufferRef;
typedef const struct opaqueCMFormatDescription *CMFormatDescriptionRef;
typedef CMFormatDescriptionRef CMVideoFormatDescriptionRef,CMAudioFormatDescriptionRef,CMMuxedFormatDescriptionRef,CMClosedCaptionFormatDescriptionRef,CMTextFormatDescriptionRef,CMTimeCodeFormatDescriptionRef,CMMetadataFormatDescriptionRef,CMTaggedBufferGroupFormatDescriptionRef;
typedef struct opaqueCMSampleBuffer *CMSampleBufferRef;
typedef struct OpaqueCMClock *CMClockRef;
typedef struct OpaqueCMTimebase *CMTimebaseRef;
typedef CFTypeRef CMClockOrTimebaseRef,CMAttachmentBearerRef,CMBufferRef;
typedef struct OpaqueCMNotificationCenter *CMNotificationCenterRef;
typedef void (*CMNotificationCallback)(CMNotificationCenterRef,const void*,CFStringRef,const void*,CFTypeRef);
typedef struct opaqueCMSimpleQueue *CMSimpleQueueRef;
typedef struct opaqueCMBufferQueue *CMBufferQueueRef;
typedef struct OpaqueCMMemoryPool *CMMemoryPoolRef;
typedef struct opaqueCMBufferQueueTriggerToken *CMBufferQueueTriggerToken;
typedef int32_t CMBufferQueueTriggerCondition;
#pragma pack(push,4)
typedef struct {uint32_t version;void *(*AllocateBlock)(void*,size_t);void (*FreeBlock)(void*,void*,size_t);void *refCon;} CMBlockBufferCustomBlockSource;
#pragma pack(pop)
typedef OSStatus (*CMSampleBufferMakeDataReadyCallback)(CMSampleBufferRef,void*);
typedef OSStatus (^CMSampleBufferMakeDataReadyHandler)(CMSampleBufferRef);
typedef void (*CMSampleBufferInvalidateCallback)(CMSampleBufferRef,uint64_t);
typedef void (^CMSampleBufferInvalidateHandler)(CMSampleBufferRef);
typedef CMTime (*CMBufferGetTimeCallback)(CMBufferRef,void*);
typedef Boolean (*CMBufferGetBooleanCallback)(CMBufferRef,void*);
typedef CFComparisonResult (*CMBufferCompareCallback)(CMBufferRef,CMBufferRef,void*);
typedef size_t (*CMBufferGetSizeCallback)(CMBufferRef,void*);
typedef CMTime (^CMBufferGetTimeHandler)(CMBufferRef);
typedef Boolean (^CMBufferGetBooleanHandler)(CMBufferRef);
typedef CFComparisonResult (^CMBufferCompareHandler)(CMBufferRef,CMBufferRef);
typedef size_t (^CMBufferGetSizeHandler)(CMBufferRef);
typedef struct {uint32_t version;void *refcon;CMBufferGetTimeCallback getDecodeTimeStamp,getPresentationTimeStamp,getDuration;CMBufferGetBooleanCallback isDataReady;CMBufferCompareCallback compare;CFStringRef dataBecameReadyNotification;CMBufferGetSizeCallback getSize;} CMBufferCallbacks;
typedef struct {size_t version;CMBufferGetTimeHandler getDecodeTimeStamp,getPresentationTimeStamp,getDuration;CMBufferGetBooleanHandler isDataReady;CMBufferCompareHandler compare;CFStringRef dataBecameReadyNotification;CMBufferGetSizeHandler getSize;} CMBufferHandlers;
typedef void (*CMBufferQueueTriggerCallback)(void*,CMBufferQueueTriggerToken);
typedef void (^CMBufferQueueTriggerHandler)(CMBufferQueueTriggerToken);
typedef OSStatus (*CMBufferValidationCallback)(CMBufferQueueRef,CMBufferRef,void*);
typedef OSStatus (^CMBufferValidationHandler)(CMBufferQueueRef,CMBufferRef);
typedef uint32_t CMTagCategory,CMTagDataType;
typedef uint64_t CMTagValue,CMStereoViewComponents,CMStereoViewInterpretationOptions,CMProjectionType,CMPackingType;
typedef struct {CMTagCategory category;CMTagDataType dataType;CMTagValue value;} CMTag;
typedef const struct OpaqueCMTagCollection *CMTagCollectionRef;
typedef struct OpaqueCMTagCollection *CMMutableTagCollectionRef;
typedef struct OpaqueCMTaggedBufferGroup *CMTaggedBufferGroupRef;
typedef void (*CMTagCollectionApplierFunction)(CMTag,void*);
typedef Boolean (*CMTagCollectionTagFilterFunction)(CMTag,void*);
#define CMTIME_IS_VALID(t) (((t).flags & kCMTimeFlags_Valid)!=0)
#define CMTIME_IS_INVALID(t) (!CMTIME_IS_VALID(t))
#define CMTIME_IS_POSITIVE_INFINITY(t) (CMTIME_IS_VALID(t)&&((t).flags&kCMTimeFlags_PositiveInfinity))
#define CMTIME_IS_NEGATIVE_INFINITY(t) (CMTIME_IS_VALID(t)&&((t).flags&kCMTimeFlags_NegativeInfinity))
#define CMTIME_IS_INDEFINITE(t) (CMTIME_IS_VALID(t)&&((t).flags&kCMTimeFlags_Indefinite))
#define CMTIME_IS_NUMERIC(t) (CMTIME_IS_VALID(t)&&!((t).flags&kCMTimeFlags_ImpliedValueFlagsMask))
#ifndef SF_MEDIA_PREPROCESS
#include <CoreMedia/Generated.h>
#endif
