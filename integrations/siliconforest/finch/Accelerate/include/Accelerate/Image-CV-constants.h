/* SPDX-License-Identifier: MIT */
/* Our declarations of public numeric facts absent from PD headers. */
#pragma once
enum {
    kCGColorSpaceModelXYZ=7,
    kCGImagePixelFormatMask=983040,
    kCGImagePixelFormatRGB101010=196608,
    kCGImagePixelFormatRGB555=65536,
    kCGImagePixelFormatRGB565=131072,
    kCGImagePixelFormatRGBCIF10=262144,
};
/* CoreVideo/Numeric.h provides these as macros when available. */
#ifndef kCVPixelFormatType_16BE555
enum { kCVPixelFormatType_16BE555=16 };
#endif
#ifndef kCVPixelFormatType_16BE565
enum { kCVPixelFormatType_16BE565=1110783541 };
#endif
#ifndef kCVPixelFormatType_16Gray
enum { kCVPixelFormatType_16Gray=1647392359 };
#endif
#ifndef kCVPixelFormatType_16LE555
enum { kCVPixelFormatType_16LE555=1278555445 };
#endif
#ifndef kCVPixelFormatType_16LE5551
enum { kCVPixelFormatType_16LE5551=892679473 };
#endif
#ifndef kCVPixelFormatType_16LE565
enum { kCVPixelFormatType_16LE565=1278555701 };
#endif
#ifndef kCVPixelFormatType_24BGR
enum { kCVPixelFormatType_24BGR=842285639 };
#endif
#ifndef kCVPixelFormatType_32AlphaGray
enum { kCVPixelFormatType_32AlphaGray=1647522401 };
#endif
#ifndef kCVPixelFormatType_422YpCbCr10
enum { kCVPixelFormatType_422YpCbCr10=1983000880 };
#endif
#ifndef kCVPixelFormatType_422YpCbCr16
enum { kCVPixelFormatType_422YpCbCr16=1983000886 };
#endif
#ifndef kCVPixelFormatType_422YpCbCr8FullRange
enum { kCVPixelFormatType_422YpCbCr8FullRange=2037741158 };
#endif
#ifndef kCVPixelFormatType_422YpCbCr_4A_8BiPlanar
enum { kCVPixelFormatType_422YpCbCr_4A_8BiPlanar=1630697081 };
#endif
#ifndef kCVPixelFormatType_4444AYpCbCr16
enum { kCVPixelFormatType_4444AYpCbCr16=2033463606 };
#endif
#ifndef kCVPixelFormatType_4444AYpCbCr8
enum { kCVPixelFormatType_4444AYpCbCr8=2033463352 };
#endif
#ifndef kCVPixelFormatType_4444YpCbCrA8
enum { kCVPixelFormatType_4444YpCbCrA8=1983131704 };
#endif
#ifndef kCVPixelFormatType_4444YpCbCrA8R
enum { kCVPixelFormatType_4444YpCbCrA8R=1916022840 };
#endif
#ifndef kCVPixelFormatType_444YpCbCr10
enum { kCVPixelFormatType_444YpCbCr10=1983131952 };
#endif
#ifndef kCVPixelFormatType_444YpCbCr8
enum { kCVPixelFormatType_444YpCbCr8=1983066168 };
#endif
#ifndef kCVPixelFormatType_48RGB
enum { kCVPixelFormatType_48RGB=1647589490 };
#endif
#ifndef kCVPixelFormatType_OneComponent32Float
enum { kCVPixelFormatType_OneComponent32Float=1278226534 };
#endif
#ifndef kCVPixelFormatType_TwoComponent16Half
enum { kCVPixelFormatType_TwoComponent16Half=843264104 };
#endif
#ifndef kCVPixelFormatType_TwoComponent32Float
enum { kCVPixelFormatType_TwoComponent32Float=843264102 };
#endif
