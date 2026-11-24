/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Adapted from Finch 2d9b1bb VTDecompression.c's two capability queries.
 * Own declarations and current retained-decoder admission; no SDK headers.
 * Native built-in H264/HEVC query observed -19510 with output untouched.
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
typedef const struct opaqueCMFormatDescription *CMFormatDescriptionRef;
extern CFTypeID CMFormatDescriptionGetTypeID(void);
extern uint32_t CMFormatDescriptionGetMediaType(CMFormatDescriptionRef);
extern uint32_t CMFormatDescriptionGetMediaSubType(CMFormatDescriptionRef);
enum { SFVTParameterErr=-12902,SFVTCouldNotFindVideoDecoderErr=-12906,
       SFVTCouldNotFindExtensionErr=-19510 };
Boolean VTIsStereoMVHEVCDecodeSupported(void) { return false; }
OSStatus VTCopyVideoDecoderExtensionProperties(CMFormatDescriptionRef format,
                                               CFDictionaryRef *propertiesOut) {
    if (!format || !propertiesOut || CFGetTypeID(format)!=CMFormatDescriptionGetTypeID() ||
        CMFormatDescriptionGetMediaType(format)!=0x76696465) return SFVTParameterErr;
    /* The retained decoder admits H264(avc1/avc3), HEVC(hvc1/hev1), VP9.
     * None is a MediaExtension. No dictionary is produced on failure. */
    switch(CMFormatDescriptionGetMediaSubType(format)) {
    case 0x61766331: case 0x61766333: case 0x68766331: case 0x68657631: case 0x76703039:
        return SFVTCouldNotFindExtensionErr;
    default:
        return SFVTCouldNotFindVideoDecoderErr;
    }
}
