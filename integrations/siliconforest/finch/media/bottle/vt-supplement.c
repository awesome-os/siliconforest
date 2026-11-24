/* SPDX-License-Identifier: MIT */
/* Public surface needed by FFmpeg's optional encoder/pixel filters. There is no
   encoder or pixel session in this retained provider: creation fails explicitly,
   no object or supported-property dictionary is fabricated. Decoder untouched. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include "VideoToolbox.h"
/* Retained PD error fact; encoder is deliberately absent. */
#define kVTCouldNotFindVideoEncoderErr (-12908)
typedef struct OpaqueVTCompressionSession *SFCompressionSession;
typedef struct OpaqueVTPixelTransferSession *SFTransferSession;
typedef struct OpaqueVTPixelRotationSession *SFRotationSession;
CVPixelBufferPoolRef VTCompressionSessionGetPixelBufferPool(SFCompressionSession session) {
    (void)session; return NULL; /* no compression session can be created */
}
OSStatus VTCompressionSessionPrepareToEncodeFrames(SFCompressionSession session) {
    (void)session; return kVTCouldNotFindVideoEncoderErr;
}
OSStatus VTCopySupportedPropertyDictionaryForEncoder(int32_t width,int32_t height,CMVideoCodecType type,
    CFDictionaryRef specification,CFStringRef *id,CFDictionaryRef *properties) {
    (void)type;(void)specification;if(id)*id=NULL;if(properties)*properties=NULL;
    return width<=0 || height<=0 || (!id && !properties) ? kVTParameterErr : kVTCouldNotFindVideoEncoderErr;
}
OSStatus VTPixelTransferSessionCreate(CFAllocatorRef allocator,SFTransferSession *out) {
    (void)allocator;if(!out)return kVTParameterErr;*out=NULL;return kVTPropertyNotSupportedErr;
}
void VTPixelTransferSessionInvalidate(SFTransferSession session) {(void)session;}
OSStatus VTPixelTransferSessionTransferImage(SFTransferSession session,CVPixelBufferRef source,CVPixelBufferRef destination) {
    (void)session;(void)source;(void)destination;return kVTPropertyNotSupportedErr;
}
OSStatus VTPixelRotationSessionCreate(CFAllocatorRef allocator,SFRotationSession *out) {
    (void)allocator;if(!out)return kVTParameterErr;*out=NULL;return kVTPropertyNotSupportedErr;
}
void VTPixelRotationSessionInvalidate(SFRotationSession session) {(void)session;}
OSStatus VTPixelRotationSessionRotateImage(SFRotationSession session,CVPixelBufferRef source,CVPixelBufferRef destination) {
    (void)session;(void)source;(void)destination;return kVTPropertyNotSupportedErr;
}
/* The provider has no additional professional workflow codecs to register. */
void VTRegisterProfessionalVideoWorkflowVideoDecoders(void) {}
void VTRegisterProfessionalVideoWorkflowVideoEncoders(void) {}
