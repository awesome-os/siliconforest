/* SPDX-License-Identifier: MIT */
/* Own declarations from the Finch implementation and the retained PD ABI. */
#include <VideoToolbox/SFDecoder.h>
#include <CoreVideo/CoreVideo.h>
#include <CoreMedia/CoreMedia.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct OpaqueVTCompressionSession *VTCompressionSessionRef;
typedef struct OpaqueVTPixelTransferSession *VTPixelTransferSessionRef;
typedef struct OpaqueVTPixelRotationSession *VTPixelRotationSessionRef;
typedef uint32_t VTEncodeInfoFlags;
typedef void (*VTCompressionOutputCallback)(void *,void *,OSStatus,VTEncodeInfoFlags,CMSampleBufferRef);
enum {kVTCouldNotFindVideoEncoderErr=-12908,kVTVideoEncoderMalfunctionErr=-12912,
 kVTPixelTransferNotSupportedErr=-12905,kVTPixelRotationNotSupportedErr=-12914,
 kVTVideoEncoderNotAvailableNowErr=-12915};
