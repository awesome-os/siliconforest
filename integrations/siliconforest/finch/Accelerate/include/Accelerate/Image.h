/* SPDX-License-Identifier: MIT */
/* Own vImage declarations for Finch's source. Public field/width facts checked
 * against Apple documentation and the maintainer's apple-accelerate bindings. */
#pragma once
#include <CoreGraphics/CoreGraphics.h>
#include <CoreVideo/CoreVideo.h>
#include <Accelerate/Image-CV-constants.h>
typedef const struct __CGColorConversionInfo *CGColorConversionInfoRef;
CGColorSpaceRef CGColorSpaceCreateWithICCData(CFDataRef);
CGColorSpaceRef CGColorSpaceGetBaseColorSpace(CGColorSpaceRef);
size_t CGColorSpaceGetColorTableCount(CGColorSpaceRef);
void CGColorSpaceGetColorTable(CGColorSpaceRef,uint8_t *);
typedef void (*KernelFunc)(const float *,float *,unsigned long,void *);
#include <stdint.h>
#include <stddef.h>
#include <Accelerate/Image-constants.h>
typedef unsigned long vImagePixelCount;
typedef long vImage_Error;
typedef uint32_t vImage_Flags, vImageBufferTypeCode, vImageMatrixType;
typedef unsigned vImageARGBType, vImageYpCbCrType, vImageMDTableUsageHint, vImage_InterpolationMethod;
typedef int vImage_WarpInterpolation;
typedef struct { void *data; vImagePixelCount height,width; size_t rowBytes; } vImage_Buffer;
typedef uint8_t Pixel_8;
typedef float Pixel_F;
typedef uint16_t Pixel_16U, Pixel_16F;
typedef int16_t Pixel_16S, Pixel_16Q12;
typedef uint32_t Pixel_32U;
typedef Pixel_8 Pixel_88[2], Pixel_8888[4];
typedef Pixel_F Pixel_FFFF[4];
typedef Pixel_16U Pixel_16U16U[2], Pixel_ARGB_16U[4];
typedef Pixel_16S Pixel_16S16S[2], Pixel_ARGB_16S[4];
typedef Pixel_16F Pixel_16F16F[2], Pixel_ARGB_16F[4];
typedef void *ResamplingFilter, *GammaFunction;
typedef struct vImageConverter *vImageConverterRef;
typedef struct vImageCVImageFormat *vImageCVImageFormatRef;
typedef const struct vImageCVImageFormat *vImageConstCVImageFormatRef;
typedef struct vImage_MultidimensionalTableData *vImage_MultidimensionalTable;
typedef struct { float a,b,c,d,tx,ty; } vImage_AffineTransform;
typedef struct { double a,b,c,d,tx,ty; } vImage_AffineTransform_Double;
typedef vImage_AffineTransform_Double vImage_CGAffineTransform;
typedef struct { float a,b,c,d,tx,ty,vx,vy,v; } vImage_PerpsectiveTransform;
typedef struct { float Yp,Cr_R,Cr_G,Cb_G,Cb_B; } vImage_YpCbCrToARGBMatrix;
typedef struct { float R_Yp,G_Yp,B_Yp,R_Cb,G_Cb,B_Cb_R_Cr,G_Cr,B_Cr; } vImage_ARGBToYpCbCrMatrix;
typedef struct { uint8_t opaque[128]; } vImage_YpCbCrToARGB, vImage_ARGBToYpCbCr;
typedef struct { int32_t Yp_bias,CbCr_bias,YpRangeMax,CbCrRangeMax,YpMax,YpMin,CbCrMax,CbCrMin; } vImage_YpCbCrPixelRange;
typedef struct {
    uint32_t bitsPerComponent,bitsPerPixel;
    CGColorSpaceRef colorSpace;
    CGBitmapInfo bitmapInfo;
    uint32_t version;
    const CGFloat *decode;
    CGColorRenderingIntent renderingIntent;
} vImage_CGImageFormat;
typedef struct { CGFloat min,zero,full,max; } vImageChannelDescription;
typedef struct { CGFloat c0,c1,c2,c3,gamma,cutoff,c4,c5; } vImageTransferFunction;
typedef struct { float red_x,green_x,blue_x,white_x,red_y,green_y,blue_y,white_y; } vImageRGBPrimaries;
typedef struct { float white_x,white_y; } vImageWhitePoint;
extern const CGFloat *kvImageDecodeArray_16Q12Format, *kvImageConstDecodeArray0;
extern const vImage_YpCbCrToARGBMatrix *kvImage_YpCbCrToARGBMatrix_ITU_R_601_4;
extern const vImage_YpCbCrToARGBMatrix *kvImage_YpCbCrToARGBMatrix_ITU_R_709_2;
extern const vImage_ARGBToYpCbCrMatrix *kvImage_ARGBToYpCbCrMatrix_ITU_R_601_4;
extern const vImage_ARGBToYpCbCrMatrix *kvImage_ARGBToYpCbCrMatrix_ITU_R_709_2;
