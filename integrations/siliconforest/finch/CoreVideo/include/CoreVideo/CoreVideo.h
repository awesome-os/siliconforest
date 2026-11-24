/* SPDX-License-Identifier: MIT */
/* Our ABI declarations, from Finch definitions and public type facts. No SDK. */
#pragma once
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <CoreVideo/Numeric.h>
#define kCVReturnError kCVReturnFirst
typedef int32_t CVReturn;
typedef uintptr_t MTLPixelFormat;
typedef uint64_t CVOptionFlags, CVPixelBufferLockFlags, CVPixelBufferPoolFlushFlags;
typedef uint32_t CVAttachmentMode;
typedef struct __CVBuffer *CVBufferRef;
typedef CVBufferRef CVImageBufferRef,CVPixelBufferRef,CVOpenGLBufferRef,CVOpenGLTextureRef;
typedef struct __CVPixelBufferPool *CVPixelBufferPoolRef;
typedef struct __CVDisplayLink *CVDisplayLinkRef;
typedef struct __CVOpenGLBufferPool *CVOpenGLBufferPoolRef;
typedef struct __CVOpenGLTextureCache *CVOpenGLTextureCacheRef;
typedef struct __CVMetalTexture *CVMetalTextureRef;
typedef struct __CVMetalTextureCache *CVMetalTextureCacheRef;
typedef struct __CVMetalBuffer *CVMetalBufferRef;
typedef struct __CVMetalBufferCache *CVMetalBufferCacheRef;
typedef struct {int16_t subframes,subframeDivisor;uint32_t counter,type,flags;int16_t hours,minutes,seconds,frames;} CVSMPTETime;
typedef struct {int64_t timeValue;int32_t timeScale,flags;} CVTime;
typedef struct {uint32_t version;int32_t videoTimeScale;int64_t videoTime;uint64_t hostTime;double rateScalar;int64_t videoRefreshPeriod;CVSMPTETime smpteTime;uint64_t flags,reserved;} CVTimeStamp;
typedef CVReturn (*CVDisplayLinkOutputCallback)(CVDisplayLinkRef,const CVTimeStamp*,const CVTimeStamp*,CVOptionFlags,CVOptionFlags*,void*);
typedef CVReturn (^CVDisplayLinkOutputHandler)(CVDisplayLinkRef,const CVTimeStamp*,const CVTimeStamp*,CVOptionFlags,CVOptionFlags*);
typedef void (*CVPixelBufferReleaseBytesCallback)(void*,const void*);
typedef void (*CVPixelBufferReleasePlanarBytesCallback)(void*,const void*,size_t,size_t,const void*[]);
typedef Boolean (*CVFillExtendedPixelsCallBack)(CVPixelBufferRef,void*);
typedef struct {CFIndex version;CVFillExtendedPixelsCallBack fillCallBack;void *refCon;} CVFillExtendedPixelsCallBackData;
typedef struct {int32_t offset;uint32_t rowBytes;} CVPlanarComponentInfo;
typedef struct {CVPlanarComponentInfo componentInfo[1];} CVPlanarPixelBufferInfo;
typedef struct {CVPlanarComponentInfo componentInfoY,componentInfoCb,componentInfoCr;} CVPlanarPixelBufferInfo_YCbCrPlanar;
typedef struct {CVPlanarComponentInfo componentInfoY,componentInfoCbCr;} CVPlanarPixelBufferInfo_YCbCrBiPlanar;
#ifdef __OBJC__
#import <Metal/Metal.h>
#endif
#ifndef SF_MEDIA_PREPROCESS
#include <CoreVideo/Generated.h>
#endif
