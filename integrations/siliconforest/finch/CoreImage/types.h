/* SPDX-License-Identifier: MIT */
#import <CoreGraphics/CoreGraphics.h>
#import <CoreVideo/CoreVideo.h>
#import <IOSurface/IOSurface.h>
#import <CoreImage/Numeric.h>
#import <CoreImage/BuiltinProtocols.h>
typedef int32_t CIFormat;
typedef NSString *CIImageOption,*CIImageRepresentationOption,*CIContextOption,*CIRAWFilterOption,*CIRAWDecoderVersion;
typedef NSUInteger CIRenderDestinationAlphaMode;
typedef NSInteger CIQRCodeErrorCorrectionLevel,CIDataMatrixCodeECCVersion;
#import <OpenGL/CGLTypes.h>
typedef uint32_t CGImagePropertyOrientation;
@class IOSurface;
typedef CGRect (^CIKernelROICallback)(int,CGRect);
@protocol CIImageProcessorInput <NSObject>
@end
@protocol CIImageProcessorOutput <NSObject>
@end
@protocol CIFilterConstructor <NSObject>
- (CIFilter *)filterWithName:(NSString *)name;
@end
@protocol MTLTexture,MTLDevice,MTLCommandBuffer;
@class AVDepthData,AVPortraitEffectsMatte,AVSemanticSegmentationMatte;
