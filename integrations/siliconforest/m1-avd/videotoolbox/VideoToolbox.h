// VideoToolbox.h - the decompression session interface (and the compression names the tree's
// VideoToolbox.c already has), as applications built against Apple's SDK use it
// SPDX-License-Identifier: MPL-2.0 OR BSD-2-Clause
#ifndef VIDEOTOOLBOX_H
#define VIDEOTOOLBOX_H

#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CMSampleBuffer.h>
#include <CoreVideo/CVPixelBuffer.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef CFTypeRef VTSessionRef;
typedef struct OpaqueVTDecompressionSession *VTDecompressionSessionRef;
typedef uint32_t VTDecodeFrameFlags, VTDecodeInfoFlags;

enum {
    kVTDecodeFrame_EnableAsynchronousDecompression = 1 << 0,
    kVTDecodeFrame_DoNotOutputFrame = 1 << 1,
    kVTDecodeFrame_1xRealTimePlayback = 1 << 2,
    kVTDecodeFrame_EnableTemporalProcessing = 1 << 3,
};
enum {
    kVTDecodeInfo_Asynchronous = 1 << 0,
    kVTDecodeInfo_FrameDropped = 1 << 1,
    kVTDecodeInfo_ImageBufferModifiable = 1 << 2,
};
enum {
    kVTPropertyNotSupportedErr = -12900,
    kVTParameterErr = -12902,
    kVTInvalidSessionErr = -12903,
    kVTAllocationFailedErr = -12904,
    kVTCouldNotFindVideoDecoderErr = -12906,
    kVTCouldNotCreateInstanceErr = -12907,
    kVTVideoDecoderBadDataErr = -12909,
    kVTVideoDecoderUnsupportedDataFormatErr = -12910,
    kVTVideoDecoderMalfunctionErr = -12911,
    kVTFormatDescriptionChangeNotSupportedErr = -12916,
};

typedef void (*VTDecompressionOutputCallback)(void *decompressionOutputRefCon, void *sourceFrameRefCon, OSStatus status,
                                              VTDecodeInfoFlags infoFlags, CVImageBufferRef imageBuffer,
                                              CMTime presentationTimeStamp, CMTime presentationDuration);
typedef struct {
    VTDecompressionOutputCallback decompressionOutputCallback;
    void *decompressionOutputRefCon;
} VTDecompressionOutputCallbackRecord;

extern const CFStringRef kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder;
extern const CFStringRef kVTDecompressionPropertyKey_RealTime;
extern const CFStringRef kVTDecompressionPropertyKey_ThreadCount;
extern const CFStringRef kVTVideoDecoderSpecification_EnableHardwareAcceleratedVideoDecoder;
extern const CFStringRef kVTVideoDecoderSpecification_RequireHardwareAcceleratedVideoDecoder;

CFTypeID VTDecompressionSessionGetTypeID(void);
Boolean VTIsHardwareDecodeSupported(CMVideoCodecType codecType);
void VTRegisterSupplementalVideoDecoderIfAvailable(CMVideoCodecType codecType);
OSStatus VTDecompressionSessionCreate(CFAllocatorRef allocator, CMVideoFormatDescriptionRef videoFormatDescription,
                                      CFDictionaryRef videoDecoderSpecification, CFDictionaryRef destinationImageBufferAttributes,
                                      const VTDecompressionOutputCallbackRecord *outputCallback,
                                      VTDecompressionSessionRef *decompressionSessionOut);
OSStatus VTDecompressionSessionDecodeFrame(VTDecompressionSessionRef session, CMSampleBufferRef sampleBuffer,
                                           VTDecodeFrameFlags decodeFlags, void *sourceFrameRefCon, VTDecodeInfoFlags *infoFlagsOut);
OSStatus VTDecompressionSessionFinishDelayedFrames(VTDecompressionSessionRef session);
OSStatus VTDecompressionSessionWaitForAsynchronousFrames(VTDecompressionSessionRef session);
Boolean VTDecompressionSessionCanAcceptFormatDescription(VTDecompressionSessionRef session, CMFormatDescriptionRef newFormatDesc);
void VTDecompressionSessionInvalidate(VTDecompressionSessionRef session);
OSStatus VTSessionSetProperty(VTSessionRef session, CFStringRef propertyKey, CFTypeRef propertyValue);
OSStatus VTSessionCopyProperty(VTSessionRef session, CFStringRef propertyKey, CFAllocatorRef allocator, void *propertyValueOut);
OSStatus VTSessionCopySupportedPropertyDictionary(VTSessionRef session, CFDictionaryRef *supportedPropertyDictionaryOut);

#ifdef __cplusplus
}
#endif
#endif
