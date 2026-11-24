/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class CIBarcodeDescriptor, CIColor, CIContext, CIDataMatrixCodeDescriptor, CIDetector, CIFeature, CIFilter, CIFilterGenerator, CIFilterShape, CIImage, CIImageAccumulator, CIImageProcessorKernel, CIKernel, CIPDF417CodeDescriptor, CIPlugIn, CIQRCodeDescriptor, CIQRCodeFeature, CIRAWFilter, CIRectangleFeature, CIRenderDestination, CIRenderInfo, CIRenderTask, CISampler, CITextFeature, CIVector, CIWarpKernel, CIAztecCodeDescriptor, CIColorKernel, CIFaceFeature, CIBlendKernel;
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

extern NSString *const CIDetectorAccuracy;
extern NSString *const CIDetectorAccuracyHigh;
extern NSString *const CIDetectorAccuracyLow;
extern NSString *const CIDetectorAspectRatio;
extern NSString *const CIDetectorBetterEyeLocs;
extern NSString *const CIDetectorDetectDiacritics;
extern NSString *const CIDetectorExtraCharacters;
extern NSString *const CIDetectorEyeBlink;
extern NSString *const CIDetectorFocalLength;
extern NSString *const CIDetectorImageOrientation;
extern NSString *const CIDetectorLanguage;
extern NSString *const CIDetectorLanguageASCII;
extern NSString *const CIDetectorLanguageDanish;
extern NSString *const CIDetectorLanguageDutch;
extern NSString *const CIDetectorLanguageEnglish;
extern NSString *const CIDetectorLanguageFrench;
extern NSString *const CIDetectorLanguageGerman;
extern NSString *const CIDetectorLanguageIcelandic;
extern NSString *const CIDetectorLanguageItalian;
extern NSString *const CIDetectorLanguageNone;
extern NSString *const CIDetectorLanguageNorwegian;
extern NSString *const CIDetectorLanguagePortuguese;
extern NSString *const CIDetectorLanguageSpanish;
extern NSString *const CIDetectorLanguageSwedish;
extern NSString *const CIDetectorMaxFeatureCount;
extern NSString *const CIDetectorMinFeatureSize;
extern NSString *const CIDetectorNumberOfAngles;
extern NSString *const CIDetectorReturnSubFeatures;
extern NSString *const CIDetectorSmile;
extern NSString *const CIDetectorTracking;
extern NSString *const CIDetectorTypeFace;
extern NSString *const CIDetectorTypeQRCode;
extern NSString *const CIDetectorTypeRectangle;
extern NSString *const CIDetectorTypeText;
extern NSString *const CIDualRedEyeRepairMetadataKey_DeviceModelName;
extern NSString *const CIDualRedEyeRepairMetadataKey_ExifOrientation;
extern NSString *const CIDualRedEyeRepairMetadataKey_NormalizedSNR;
extern NSString *const CIDualRedEyeRepairMetadataKey_PortType;
extern NSString *const CIDualRedEyeRepairMetadataKey_TuningParametersByPortType;
extern NSString *const CIDualRedEyeRepairMetadataKey_WouldAutoFlashTurnOn;
extern NSString *const CIFeatureTypeFace;
extern NSString *const CIFeatureTypeQRCode;
extern NSString *const CIFeatureTypeRectangle;
extern NSString *const CIFeatureTypeText;
extern NSString *const CIRAWDecoderVersion6;
extern NSString *const CIRAWDecoderVersion6DNG;
extern NSString *const CIRAWDecoderVersion7;
extern NSString *const CIRAWDecoderVersion7DNG;
extern NSString *const CIRAWDecoderVersion8;
extern NSString *const CIRAWDecoderVersion8DNG;
extern NSString *const CIRAWDecoderVersion9;
extern NSString *const CIRAWDecoderVersion9DNG;
extern NSString *const CIRAWDecoderVersionNone;
extern NSString *const CITextDetectorMinimizeFalseDetections;
extern NSString *const kCIActiveKeys;
extern NSString *const kCIApplyOptionColorSpace;
extern NSString *const kCIApplyOptionDefinition;
extern NSString *const kCIApplyOptionExtent;
extern NSString *const kCIApplyOptionUserInfo;
extern NSString *const kCIAttributeClass;
extern NSString *const kCIAttributeDefault;
extern NSString *const kCIAttributeDescription;
extern NSString *const kCIAttributeDisplayName;
extern NSString *const kCIAttributeFilterAvailable_Mac;
extern NSString *const kCIAttributeFilterAvailable_iOS;
extern NSString *const kCIAttributeFilterCategories;
extern NSString *const kCIAttributeFilterDisplayName;
extern NSString *const kCIAttributeFilterName;
extern NSString *const kCIAttributeIdentity;
extern NSString *const kCIAttributeMax;
extern NSString *const kCIAttributeMin;
extern NSString *const kCIAttributeName;
extern NSString *const kCIAttributeReferenceDocumentation;
extern NSString *const kCIAttributeSliderMax;
extern NSString *const kCIAttributeSliderMin;
extern NSString *const kCIAttributeType;
extern NSString *const kCIAttributeTypeAngle;
extern NSString *const kCIAttributeTypeBoolean;
extern NSString *const kCIAttributeTypeColor;
extern NSString *const kCIAttributeTypeCount;
extern NSString *const kCIAttributeTypeDistance;
extern NSString *const kCIAttributeTypeGradient;
extern NSString *const kCIAttributeTypeImage;
extern NSString *const kCIAttributeTypeInteger;
extern NSString *const kCIAttributeTypeOffset;
extern NSString *const kCIAttributeTypeOpaqueColor;
extern NSString *const kCIAttributeTypePosition;
extern NSString *const kCIAttributeTypePosition3;
extern NSString *const kCIAttributeTypeRectangle;
extern NSString *const kCIAttributeTypeScalar;
extern NSString *const kCIAttributeTypeTime;
extern NSString *const kCIAttributeTypeTransform;
extern NSString *const kCIAutoPerspectiveContext;
extern NSString *const kCIAutoPerspectiveFocalLength;
extern NSString *const kCIAutoPerspectiveGenerateDebugImage;
extern NSString *const kCIAutoPerspectiveLineSearchRangeH;
extern NSString *const kCIAutoPerspectiveLineSearchRangeV;
extern NSString *const kCIAutoPerspectiveMinimumPitchCorrectionArea;
extern NSString *const kCIAutoPerspectiveMinimumYawCorrectionArea;
extern NSString *const kCIAutoPerspectivePitchLimit;
extern NSString *const kCIAutoPerspectiveRollLimit;
extern NSString *const kCIAutoPerspectiveSaliencyHeatMap;
extern NSString *const kCIAutoPerspectiveVersion;
extern NSString *const kCIAutoPerspectiveVersion1;
extern NSString *const kCIAutoPerspectiveVersion2;
extern NSString *const kCIAutoPerspectiveYawLimit;
extern NSString *const kCICCPortraitBinaryArchive;
extern NSString *const kCICategoryApplePrivate;
extern NSString *const kCICategoryBlur;
extern NSString *const kCICategoryBuiltIn;
extern NSString *const kCICategoryColorAdjustment;
extern NSString *const kCICategoryColorEffect;
extern NSString *const kCICategoryCompositeOperation;
extern NSString *const kCICategoryDistortionEffect;
extern NSString *const kCICategoryFilterGenerator;
extern NSString *const kCICategoryGenerator;
extern NSString *const kCICategoryGeometryAdjustment;
extern NSString *const kCICategoryGradient;
extern NSString *const kCICategoryHalftoneEffect;
extern NSString *const kCICategoryHighDynamicRange;
extern NSString *const kCICategoryInterlaced;
extern NSString *const kCICategoryLightingEffect;
extern NSString *const kCICategoryNonSquarePixels;
extern NSString *const kCICategoryReduction;
extern NSString *const kCICategorySharpen;
extern NSString *const kCICategoryStillImage;
extern NSString *const kCICategoryStylize;
extern NSString *const kCICategoryTileEffect;
extern NSString *const kCICategoryTransition;
extern NSString *const kCICategoryVideo;
extern NSString *const kCICategoryXMPSerializable;
extern NSString *const kCIContextAllowClampToAlpha;
extern NSString *const kCIContextAllowHalfPrecision;
extern NSString *const kCIContextAllowLowPower;
extern NSString *const kCIContextCVMetalTextureCache;
extern NSString *const kCIContextCacheIntermediates;
extern NSString *const kCIContextColorCubeSize;
extern NSString *const kCIContextDefaultCGImageFormat;
extern NSString *const kCIContextDisableSoftwareFallback;
extern NSString *const kCIContextEnableBlending;
extern NSString *const kCIContextHLGOpticalScale;
extern NSString *const kCIContextHighQualityDownsample;
extern NSString *const kCIContextIOSurfaceMemoryPoolID;
extern NSString *const kCIContextInlineAffineTransforms;
extern NSString *const kCIContextIntermediateAllocator;
extern NSString *const kCIContextIntermediateMemoryTarget;
extern NSString *const kCIContextLossyCompressedIntermediates;
extern NSString *const kCIContextMemoryLimit;
extern NSString *const kCIContextName;
extern NSString *const kCIContextOpenGLShareContext;
extern NSString *const kCIContextOutputColorSpace;
extern NSString *const kCIContextOutputPremultiplied;
extern NSString *const kCIContextParametricColorMatching;
extern NSString *const kCIContextPriorityRequestHigh;
extern NSString *const kCIContextPriorityRequestLow;
extern NSString *const kCIContextPriorityRequestMedia;
extern NSString *const kCIContextQuality;
extern NSString *const kCIContextUseMetalRenderer;
extern NSString *const kCIContextUseSoftwareRenderer;
extern NSString *const kCIContextWorkingColorSpace;
extern NSString *const kCIContextWorkingFormat;
extern NSString *const kCICoreUIBinaryArchive;
extern NSString *const kCIDynamicRangeConstrainedHigh;
extern NSString *const kCIDynamicRangeHigh;
extern NSString *const kCIDynamicRangeStandard;
extern NSString *const kCIFilterGeneratorExportedKey;
extern NSString *const kCIFilterGeneratorExportedKeyName;
extern NSString *const kCIFilterGeneratorExportedKeyTargetObject;
extern NSString *const kCIImageAVDepthData;
extern NSString *const kCIImageAVPortraitEffectsMatte;
extern NSString *const kCIImageAVSemanticSegmentationMatte;
extern NSString *const kCIImageAccumulatorAbortable;
extern NSString *const kCIImageAccumulatorBlendKernel;
extern NSString *const kCIImageAccumulatorColorSpace;
extern NSString *const kCIImageAlphaOne;
extern NSString *const kCIImageApplyCleanAperture;
extern NSString *const kCIImageApplyColorCorrectionVideoFilter;
extern NSString *const kCIImageApplyColorspaceCorrectionVideoFilter;
extern NSString *const kCIImageApplyGammaCorrectionVideoFilter;
extern NSString *const kCIImageApplyGeometryCorrectionVideoFilter;
extern NSString *const kCIImageApplyOrientationProperty;
extern NSString *const kCIImageAutoAdjustCrop;
extern NSString *const kCIImageAutoAdjustEnhance;
extern NSString *const kCIImageAutoAdjustFeatures;
extern NSString *const kCIImageAutoAdjustLevel;
extern NSString *const kCIImageAutoAdjustLevel_DominantAngleDiffThreshold;
extern NSString *const kCIImageAutoAdjustLevel_MaxAngle;
extern NSString *const kCIImageAutoAdjustLevel_MinAngle;
extern NSString *const kCIImageAutoAdjustLevel_UseVerticalDetector;
extern NSString *const kCIImageAutoAdjustLevel_VerticalAngleThreshold;
extern NSString *const kCIImageAutoAdjustRedEye;
extern NSString *const kCIImageAutoApplyVideoFilter;
extern NSString *const kCIImageAuxiliaryDepth;
extern NSString *const kCIImageAuxiliaryDisparity;
extern NSString *const kCIImageAuxiliaryGainMapPreference;
extern NSString *const kCIImageAuxiliaryHDRGainMap;
extern NSString *const kCIImageAuxiliaryPortraitEffectsMatte;
extern NSString *const kCIImageAuxiliarySemanticSegmentationGlassesMatte;
extern NSString *const kCIImageAuxiliarySemanticSegmentationHairMatte;
extern NSString *const kCIImageAuxiliarySemanticSegmentationSkinMatte;
extern NSString *const kCIImageAuxiliarySemanticSegmentationSkyMatte;
extern NSString *const kCIImageAuxiliarySemanticSegmentationTeethMatte;
extern NSString *const kCIImageCacheHint;
extern NSString *const kCIImageCacheImmediately;
extern NSString *const kCIImageClampToEdge;
extern NSString *const kCIImageColorSpace;
extern NSString *const kCIImageContentAverageLightLevel;
extern NSString *const kCIImageContentDigest;
extern NSString *const kCIImageContentHeadroom;
extern NSString *const kCIImageEdgeRepeat;
extern NSString *const kCIImageEdgesAreClear;
extern NSString *const kCIImageExpandToHDR;
extern NSString *const kCIImageFlipped;
extern NSString *const kCIImageGenerateFlexGTC;
extern NSString *const kCIImageIgnorePixelFormatFor601Fixup;
extern NSString *const kCIImageNearestSampling;
extern NSString *const kCIImageOpaque;
extern NSString *const kCIImagePremultiplied;
extern NSString *const kCIImageProcessorAllowCompressedInputsAndOutputs;
extern NSString *const kCIImageProcessorAllowPartialOutputRegion;
extern NSString *const kCIImageProcessorCanOutputLossyCompressed;
extern NSString *const kCIImageProcessorOnlyUsesMetal;
extern NSString *const kCIImageProcessorSynchronizeInputs;
extern NSString *const kCIImageProperties;
extern NSString *const kCIImageProviderContentDigest;
extern NSString *const kCIImageProviderMaximumTileSize;
extern NSString *const kCIImageProviderName;
extern NSString *const kCIImageProviderTileHeightArray;
extern NSString *const kCIImageProviderTileSize;
extern NSString *const kCIImageProviderTileWidthArray;
extern NSString *const kCIImageProviderUserInfo;
extern NSString *const kCIImageRepresentationAVDepthData;
extern NSString *const kCIImageRepresentationAVPortraitEffectsMatte;
extern NSString *const kCIImageRepresentationAVSemanticSegmentationMattes;
extern NSString *const kCIImageRepresentationDepthImage;
extern NSString *const kCIImageRepresentationDisparityImage;
extern NSString *const kCIImageRepresentationHDRGainMapAsRGB;
extern NSString *const kCIImageRepresentationHDRGainMapHeadroom;
extern NSString *const kCIImageRepresentationHDRGainMapImage;
extern NSString *const kCIImageRepresentationHDRImage;
extern NSString *const kCIImageRepresentationISOGainMapAlternateHeadroom;
extern NSString *const kCIImageRepresentationISOGainMapBaseHeadroom;
extern NSString *const kCIImageRepresentationISOGainMapGamma;
extern NSString *const kCIImageRepresentationISOGainMapImage;
extern NSString *const kCIImageRepresentationISOGainMapMax;
extern NSString *const kCIImageRepresentationISOGainMapMin;
extern NSString *const kCIImageRepresentationISOGainMapOffset;
extern NSString *const kCIImageRepresentationPortraitEffectsMatteImage;
extern NSString *const kCIImageRepresentationSemanticSegmentationGlassesMatteImage;
extern NSString *const kCIImageRepresentationSemanticSegmentationHairMatteImage;
extern NSString *const kCIImageRepresentationSemanticSegmentationSkinMatteImage;
extern NSString *const kCIImageRepresentationSemanticSegmentationSkyMatteImage;
extern NSString *const kCIImageRepresentationSemanticSegmentationTeethMatteImage;
extern NSString *const kCIImageTextPadding;
extern NSString *const kCIImageTextScaleFactor;
extern NSString *const kCIImageTextureFormat;
extern NSString *const kCIImageTextureTarget;
extern NSString *const kCIImageToneMapHDRtoSDR;
extern NSString *const kCIImageYCCMatrix;
extern NSString *const kCIInputAllowDraftModeKey;
extern NSString *const kCIInputAmountKey;
extern NSString *const kCIInputAngleKey;
extern NSString *const kCIInputAspectRatioKey;
extern NSString *const kCIInputBackgroundImageKey;
extern NSString *const kCIInputBacksideImageKey;
extern NSString *const kCIInputBaselineExposureKey;
extern NSString *const kCIInputBiasKey;
extern NSString *const kCIInputBiasVectorKey;
extern NSString *const kCIInputBoostKey;
extern NSString *const kCIInputBoostShadowAmountKey;
extern NSString *const kCIInputBrightnessKey;
extern NSString *const kCIInputCenterKey;
extern NSString *const kCIInputColor0Key;
extern NSString *const kCIInputColor1Key;
extern NSString *const kCIInputColorKey;
extern NSString *const kCIInputColorNoiseReductionAmountKey;
extern NSString *const kCIInputColorSpaceKey;
extern NSString *const kCIInputContrastKey;
extern NSString *const kCIInputCountKey;
extern NSString *const kCIInputDecoderVersionKey;
extern NSString *const kCIInputDepthImageKey;
extern NSString *const kCIInputDisableGamutMapKey;
extern NSString *const kCIInputDisparityImageKey;
extern NSString *const kCIInputEVKey;
extern NSString *const kCIInputEnableChromaticNoiseTrackingKey;
extern NSString *const kCIInputEnableEDRModeKey;
extern NSString *const kCIInputEnableSharpeningKey;
extern NSString *const kCIInputEnableVendorLensCorrectionKey;
extern NSString *const kCIInputExtentKey;
extern NSString *const kCIInputExtrapolateKey;
extern NSString *const kCIInputGradientImageKey;
extern NSString *const kCIInputIgnoreImageOrientationKey;
extern NSString *const kCIInputImageKey;
extern NSString *const kCIInputImageOrientationKey;
extern NSString *const kCIInputIntensityKey;
extern NSString *const kCIInputLinearSpaceFilter;
extern NSString *const kCIInputLocalToneMapAmountKey;
extern NSString *const kCIInputLuminanceNoiseReductionAmountKey;
extern NSString *const kCIInputMaskImageKey;
extern NSString *const kCIInputMatteImageKey;
extern NSString *const kCIInputMoireAmountKey;
extern NSString *const kCIInputNeutralChromaticityXKey;
extern NSString *const kCIInputNeutralChromaticityYKey;
extern NSString *const kCIInputNeutralLocationKey;
extern NSString *const kCIInputNeutralTemperatureKey;
extern NSString *const kCIInputNeutralTintKey;
extern NSString *const kCIInputNoiseReductionAmountKey;
extern NSString *const kCIInputNoiseReductionContrastAmountKey;
extern NSString *const kCIInputNoiseReductionDetailAmountKey;
extern NSString *const kCIInputNoiseReductionSharpnessAmountKey;
extern NSString *const kCIInputPaletteImageKey;
extern NSString *const kCIInputPerceptualKey;
extern NSString *const kCIInputPoint0Key;
extern NSString *const kCIInputPoint1Key;
extern NSString *const kCIInputRadius0Key;
extern NSString *const kCIInputRadius1Key;
extern NSString *const kCIInputRadiusKey;
extern NSString *const kCIInputRefractionKey;
extern NSString *const kCIInputSaturationKey;
extern NSString *const kCIInputScaleFactorKey;
extern NSString *const kCIInputScaleKey;
extern NSString *const kCIInputShadingImageKey;
extern NSString *const kCIInputSharpnessKey;
extern NSString *const kCIInputTargetImageKey;
extern NSString *const kCIInputThresholdKey;
extern NSString *const kCIInputTimeKey;
extern NSString *const kCIInputTransformKey;
extern NSString *const kCIInputVersionKey;
extern NSString *const kCIInputWeightsKey;
extern NSString *const kCIInputWidthKey;
extern NSString *const kCIKernelMessageDescription;
extern NSString *const kCIKernelMessageFilename;
extern NSString *const kCIKernelMessageLineNumber;
extern NSString *const kCIKernelMessageOffset;
extern NSString *const kCIKernelMessageType;
extern NSString *const kCIKernelMessageTypeError;
extern NSString *const kCIKernelMessageTypeFatal;
extern NSString *const kCIKernelMessageTypeFunctionName;
extern NSString *const kCIKernelMessageTypeInternalError;
extern NSString *const kCIKernelMessageTypeNote;
extern NSString *const kCIKernelMessageTypeRemark;
extern NSString *const kCIKernelMessageTypeSyntaxError;
extern NSString *const kCIKernelMessageTypeWarning;
extern NSString *const kCIKernelOutputFormat;
extern NSString *const kCIKernelThreadgroupsPerGrid;
extern NSString *const kCIKernelThreadsPerThreadgroup;
extern NSString *const kCIOutputImageKey;
extern NSString *const kCIOutputNativeSizeKey;
extern NSString *const kCIPhotoFilterBinaryArchive;
extern NSString *const kCIPortraitFilterV2FullSizeBinaryArchive;
extern NSString *const kCIPortraitFilterV2ProxySizeBinaryArchive;
extern NSString *const kCIPropertiesKey;
extern NSString *const kCIRedEyeRepairBinaryArchive;
extern NSString *const kCISamplerAffineMatrix;
extern NSString *const kCISamplerBlur;
extern NSString *const kCISamplerBlurFormat;
extern NSString *const kCISamplerColorSpace;
extern NSString *const kCISamplerFilterLinear;
extern NSString *const kCISamplerFilterMode;
extern NSString *const kCISamplerFilterNearest;
extern NSString *const kCISamplerFilterPoint;
extern NSString *const kCISamplerWrapBlack;
extern NSString *const kCISamplerWrapClamp;
extern NSString *const kCISamplerWrapMode;
extern NSString *const kCISamplerWrapPeriodic;
extern NSString *const kCISupportedDecoderVersionsKey;
extern NSString *const kCIUIParameterSet;
extern NSString *const kCIUISetAdvanced;
extern NSString *const kCIUISetBasic;
extern NSString *const kCIUISetDevelopment;
extern NSString *const kCIUISetIntermediate;

@interface CIBarcodeDescriptor : NSObject
+ (BOOL)supportsSecureCoding;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CIColor : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a;
- (instancetype)initWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b;
- (nullable instancetype)initWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a colorSpace:(CGColorSpaceRef)colorSpace;
- (nullable instancetype)initWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b colorSpace:(CGColorSpaceRef)colorSpace;
- (instancetype)initWithCGColor:(CGColorRef)color;
+ (instancetype)colorWithCGColor:(CGColorRef)color;
+ (instancetype)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a;
+ (instancetype)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b;
+ (nullable instancetype)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b alpha:(CGFloat)a colorSpace:(CGColorSpaceRef)colorSpace;
+ (nullable instancetype)colorWithRed:(CGFloat)r green:(CGFloat)g blue:(CGFloat)b colorSpace:(CGColorSpaceRef)colorSpace;
+ (instancetype)colorWithString:(NSString *)representation;
- (void)dealloc;
- (size_t)numberOfComponents;
- (const CGFloat *)components;
- (CGFloat)red;
- (CGFloat)green;
- (CGFloat)blue;
- (CGFloat)alpha;
- (CGColorSpaceRef)colorSpace;
- (NSString *)stringRepresentation;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CIContext : NSObject
+ (unsigned int)offlineGPUCount;
+ (CIContext *)context;
+ (CIContext *)contextWithOptions:(NSDictionary<CIContextOption, id> *)options;
+ (CIContext *)contextWithCGContext:(CGContextRef)cgctx options:(NSDictionary<CIContextOption, id> *)options;
+ (CIContext *)contextWithMTLDevice:(id)device;
+ (CIContext *)contextWithMTLDevice:(id)device options:(NSDictionary *)options;
+ (CIContext *)contextWithMTLCommandQueue:(id)commandQueue;
+ (CIContext *)contextWithMTLCommandQueue:(id)commandQueue options:(NSDictionary *)options;
+ (CIContext *)contextWithCGLContext:(CGLContextObj)cglctx pixelFormat:(CGLPixelFormatObj)pixelFormat colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options;
+ (CIContext *)contextWithCGLContext:(CGLContextObj)cglctx pixelFormat:(CGLPixelFormatObj)pixelFormat options:(NSDictionary *)options;
- (instancetype)initWithOptions:(NSDictionary<CIContextOption, id> *)options;
- (instancetype)init;
- (void)dealloc;
- (CGColorSpaceRef)workingColorSpace;
- (CIFormat)workingFormat;
- (CGSize)inputImageMaximumSize;
- (CGSize)outputImageMaximumSize;
- (void)reclaimResources;
- (void)clearCaches;
- (NSString *)description;
- (CGImageRef)createCGImage:(CIImage *)image fromRect:(CGRect)fromRect;
- (CGImageRef)createCGImage:(CIImage *)image fromRect:(CGRect)fromRect format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
- (CGImageRef)createCGImage:(CIImage *)image fromRect:(CGRect)fromRect format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace deferred:(BOOL)deferred;
- (CGImageRef)createCGImage:(CIImage *)image fromRect:(CGRect)fromRect format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace deferred:(BOOL)deferred calculateHDRStats:(BOOL)calculateHDRStats;
- (void)render:(CIImage *)image toBitmap:(void *)data rowBytes:(ptrdiff_t)rowBytes bounds:(CGRect)bounds format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
- (void)render:(CIImage *)image toCVPixelBuffer:(CVPixelBufferRef)buffer;
- (void)render:(CIImage *)image toCVPixelBuffer:(CVPixelBufferRef)buffer bounds:(CGRect)bounds colorSpace:(CGColorSpaceRef)colorSpace;
- (void)render:(CIImage *)image toIOSurface:(IOSurfaceRef)surface bounds:(CGRect)bounds colorSpace:(CGColorSpaceRef)colorSpace;
- (void)drawImage:(CIImage *)image inRect:(CGRect)inRect fromRect:(CGRect)fromRect;
- (void)drawImage:(CIImage *)image atPoint:(CGPoint)atPoint fromRect:(CGRect)fromRect;
- (NSData *)TIFFRepresentationOfImage:(CIImage *)image format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options;
- (NSData *)JPEGRepresentationOfImage:(CIImage *)image colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options;
- (NSData *)PNGRepresentationOfImage:(CIImage *)image format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options;
- (NSData *)HEIFRepresentationOfImage:(CIImage *)image format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options;
- (NSData *)HEIF10RepresentationOfImage:(CIImage *)image colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options error:(NSError **)error;
- (BOOL)writeTIFFRepresentationOfImage:(CIImage *)image toURL:(NSURL *)url format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options error:(NSError **)error;
- (BOOL)writePNGRepresentationOfImage:(CIImage *)image toURL:(NSURL *)url format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options error:(NSError **)error;
- (BOOL)writeJPEGRepresentationOfImage:(CIImage *)image toURL:(NSURL *)url colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options error:(NSError **)error;
- (BOOL)writeHEIFRepresentationOfImage:(CIImage *)image toURL:(NSURL *)url format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace options:(NSDictionary *)options error:(NSError **)error;
- (CIRenderTask *)startTaskToRender:(CIImage *)image fromRect:(CGRect)fromRect toDestination:(CIRenderDestination *)destination atPoint:(CGPoint)atPoint error:(NSError **)error;
- (CIRenderTask *)startTaskToRender:(CIImage *)image toDestination:(CIRenderDestination *)destination error:(NSError **)error;
- (BOOL)prepareRender:(CIImage *)image fromRect:(CGRect)fromRect toDestination:(CIRenderDestination *)destination atPoint:(CGPoint)atPoint error:(NSError **)error;
- (CIRenderTask *)startTaskToClear:(CIRenderDestination *)destination error:(NSError **)error;
@end

@interface CIDataMatrixCodeDescriptor : CIBarcodeDescriptor

@end

@interface CIDetector : NSObject
+ (CIDetector *)detectorOfType:(NSString *)type context:(CIContext *)context options:(NSDictionary<NSString *, id> *)options;
- (void)dealloc;
- (NSArray<CIFeature *> *)featuresInImage:(CIImage *)image;
- (NSArray<CIFeature *> *)featuresInImage:(CIImage *)image options:(NSDictionary *)options;
@end

@interface CIFeature : NSObject
- (NSString *)type;
- (CGRect)bounds;
@end

@interface CIFilter : NSObject
+ (void)load;
+ (BOOL)supportsSecureCoding;
+ (CIFilter *)filterWithName:(NSString *)name;
+ (CIFilter *)filterWithName:(NSString *)name keysAndValues:(id)key0, ...;
+ (CIFilter *)filterWithName:(NSString *)name withInputParameters:(NSDictionary *)params;
+ (CIFilter *)filterWithImageURL:(NSURL *)url options:(NSDictionary *)options;
+ (CIFilter *)filterWithImageData:(NSData *)data options:(NSDictionary *)options;
+ (CIFilter *)filterWithCVPixelBuffer:(CVPixelBufferRef)pixelBuffer properties:(NSDictionary *)properties options:(NSDictionary *)options;
- (instancetype)init;
- (void)dealloc;
- (NSString *)name;
- (void)setName:(NSString *)name;
- (BOOL)isEnabled;
- (void)setEnabled:(BOOL)enabled;
- (NSArray<NSString *> *)inputKeys;
- (NSArray<NSString *> *)outputKeys;
- (void)setDefaults;
- (void)setValue:(id)value forKey:(NSString *)key;
- (id)valueForKey:(NSString *)key;
- (void)setValue:(id)value forUndefinedKey:(NSString *)key;
- (id)valueForUndefinedKey:(NSString *)key;
- (CIImage *)outputImage;
- (NSDictionary<NSString *, id> *)attributes;
- (NSString *)description;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
+ (NSArray<NSString *> *)filterNamesInCategory:(NSString *)category;
+ (NSArray<NSString *> *)filterNamesInCategories:(NSArray<NSString *> *)categories;
+ (void)registerFilterName:(NSString *)name constructor:(id<CIFilterConstructor>)anObject classAttributes:(NSDictionary *)attributes;
+ (NSString *)localizedNameForFilterName:(NSString *)filterName;
+ (NSString *)localizedNameForCategory:(NSString *)category;
+ (NSString *)localizedDescriptionForFilterName:(NSString *)filterName;
+ (NSURL *)localizedReferenceDocumentationForFilterName:(NSString *)filterName;
+ (NSData *)serializedXMPFromFilters:(NSArray<CIFilter *> *)filters inputImageExtent:(CGRect)extent;
+ (NSArray<CIFilter *> *)filterArrayFromSerializedXMP:(NSData *)xmpData inputImageExtent:(CGRect)extent error:(NSError **)outError;
+ (BOOL)resolveClassMethod:(SEL)sel;
+ (BOOL)resolveInstanceMethod:(SEL)sel;
- (CIImage *)apply:(CIKernel *)k arguments:(NSArray *)args options:(NSDictionary *)dict;
@property(readonly,getter=isEnabled) BOOL enabled;
@end

@interface CIFilterGenerator : NSObject
+ (CIFilterGenerator *)filterGenerator;
+ (CIFilterGenerator *)filterGeneratorWithContentsOfURL:(NSURL *)aURL;
- (instancetype)initWithContentsOfURL:(NSURL *)aURL;
- (instancetype)init;
- (void)dealloc;
- (void)connectObject:(id)sourceObject withKey:(NSString *)sourceKey toObject:(id)targetObject withKey:(NSString *)targetKey;
- (void)disconnectObject:(id)sourceObject withKey:(NSString *)key toObject:(id)targetObject withKey:(NSString *)targetKey;
- (void)exportKey:(NSString *)key fromObject:(id)targetObject withName:(NSString *)exportedKeyName;
- (void)removeExportedKey:(NSString *)exportedKeyName;
- (NSDictionary *)exportedKeys;
- (void)setAttributes:(NSDictionary *)attributes forExportedKey:(NSString *)key;
- (NSDictionary *)classAttributes;
- (void)setClassAttributes:(NSDictionary *)attributes;
- (CIFilter *)filter;
- (void)registerFilterName:(NSString *)name;
- (BOOL)writeToURL:(NSURL *)aURL atomically:(BOOL)flag;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CIFilterShape : NSObject
+ (instancetype)shapeWithRect:(CGRect)r;
- (instancetype)initWithRect:(CGRect)r;
- (CGRect)extent;
- (CIFilterShape *)transformBy:(CGAffineTransform)m interior:(BOOL)flag;
- (CIFilterShape *)insetByX:(int)dx Y:(int)dy;
- (CIFilterShape *)unionWith:(CIFilterShape *)s2;
- (CIFilterShape *)unionWithRect:(CGRect)r;
- (CIFilterShape *)intersectWith:(CIFilterShape *)s2;
- (CIFilterShape *)intersectWithRect:(CGRect)r;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CIImage : NSObject
+ (BOOL)supportsSecureCoding;
- (void)dealloc;
- (instancetype)initWithCGImage:(CGImageRef)image options:(NSDictionary<CIImageOption, id> *)options;
- (instancetype)initWithCGImage:(CGImageRef)image;
+ (CIImage *)imageWithCGImage:(CGImageRef)image;
+ (CIImage *)imageWithCGImage:(CGImageRef)image options:(NSDictionary *)options;
- (instancetype)initWithCGImageSource:(CGImageSourceRef)source index:(size_t)index options:(NSDictionary *)options;
+ (CIImage *)imageWithCGImageSource:(CGImageSourceRef)source index:(size_t)index options:(NSDictionary *)options;
- (instancetype)initWithData:(NSData *)data options:(NSDictionary *)options;
- (instancetype)initWithData:(NSData *)data;
+ (CIImage *)imageWithData:(NSData *)data;
+ (CIImage *)imageWithData:(NSData *)data options:(NSDictionary *)options;
- (instancetype)initWithContentsOfURL:(NSURL *)url options:(NSDictionary *)options;
- (instancetype)initWithContentsOfURL:(NSURL *)url;
+ (CIImage *)imageWithContentsOfURL:(NSURL *)url;
+ (CIImage *)imageWithContentsOfURL:(NSURL *)url options:(NSDictionary *)options;
- (instancetype)initWithBitmapData:(NSData *)data bytesPerRow:(size_t)bytesPerRow size:(CGSize)size format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
+ (CIImage *)imageWithBitmapData:(NSData *)data bytesPerRow:(size_t)bytesPerRow size:(CGSize)size format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
- (instancetype)initWithColor:(CIColor *)color;
+ (CIImage *)imageWithColor:(CIColor *)color;
+ (CIImage *)emptyImage;
+(CIImage *)name;
+ (CIImage *)imageWithIOSurface:(IOSurfaceRef)surface;
+ (CIImage *)imageWithIOSurface:(IOSurfaceRef)surface options:(NSDictionary *)options;
+ (CIImage *)imageWithCVImageBuffer:(CVImageBufferRef)imageBuffer;
+ (CIImage *)imageWithCVImageBuffer:(CVImageBufferRef)imageBuffer options:(NSDictionary *)options;
+ (CIImage *)imageWithCVPixelBuffer:(CVPixelBufferRef)pixelBuffer;
+ (CIImage *)imageWithCVPixelBuffer:(CVPixelBufferRef)pixelBuffer options:(NSDictionary *)options;
- (CGRect)extent;
- (NSDictionary *)properties;
- (NSURL *)url;
- (CGColorSpaceRef)colorSpace;
- (CGImageRef)CGImage;
- (CVPixelBufferRef)pixelBuffer;
- (BOOL)isOpaque;
- (CIFilterShape *)definition;
- (NSString *)description;
- (id)copyWithZone:(NSZone *)zone;
- (CGRect)regionOfInterestForImage:(CIImage *)image inRect:(CGRect)rect;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (CIImage *)imageByApplyingTransform:(CGAffineTransform)t;
- (CIImage *)imageByApplyingTransform:(CGAffineTransform)matrix highQualityDownsample:(BOOL)highQualityDownsample;
- (CGAffineTransform)imageTransformForOrientation:(int)orientation;
- (CIImage *)imageByApplyingOrientation:(int)orientation;
- (CGAffineTransform)imageTransformForCGOrientation:(CGImagePropertyOrientation)orientation;
- (CIImage *)imageByApplyingCGOrientation:(CGImagePropertyOrientation)orientation;
- (CIImage *)imageByCroppingToRect:(CGRect)rect;
- (CIImage *)imageByClampingToRect:(CGRect)rect;
- (CIImage *)imageByClampingToExtent;
- (CIImage *)imageByCompositingOverImage:(CIImage *)dest;
- (CIImage *)imageByApplyingFilter:(NSString *)filterName withInputParameters:(NSDictionary *)params;
- (CIImage *)imageByApplyingFilter:(NSString *)filterName;
- (CIImage *)imageByApplyingGaussianBlurWithSigma:(double)sigma;
- (CIImage *)imageByColorMatchingColorSpaceToWorkingSpace:(CGColorSpaceRef)colorSpace;
- (CIImage *)imageByColorMatchingWorkingSpaceToColorSpace:(CGColorSpaceRef)colorSpace;
- (CIImage *)imageByPremultiplyingAlpha;
- (CIImage *)imageByUnpremultiplyingAlpha;
- (CIImage *)imageBySettingAlphaOneInExtent:(CGRect)extent;
- (CIImage *)imageBySettingProperties:(NSDictionary *)properties;
- (CIImage *)imageBySamplingLinear;
- (CIImage *)imageBySamplingNearest;
- (CIImage *)imageByInsertingIntermediate;
- (CIImage *)imageByInsertingIntermediate:(BOOL)cache;
@property(readonly,getter=isOpaque) BOOL opaque;
@end

@interface CIImageAccumulator : NSObject
+ (instancetype)imageAccumulatorWithExtent:(CGRect)extent format:(CIFormat)format;
+ (instancetype)imageAccumulatorWithExtent:(CGRect)extent format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
- (instancetype)initWithExtent:(CGRect)extent format:(CIFormat)format;
- (instancetype)initWithExtent:(CGRect)extent format:(CIFormat)format colorSpace:(CGColorSpaceRef)colorSpace;
- (void)dealloc;
- (CGRect)extent;
- (CIFormat)format;
- (CIImage *)image;
- (void)setImage:(CIImage *)image dirtyRect:(CGRect)dirtyRect;
- (void)setImage:(CIImage *)image;
- (void)clear;
@end

@interface CIImageProcessorKernel : NSObject
+ (CIImage *)applyWithExtent:(CGRect)extent inputs:(NSArray<CIImage *> *)inputs arguments:(NSDictionary *)args error:(NSError **)error;
+ (BOOL)processWithInputs:(NSArray *)inputs arguments:(NSDictionary *)arguments output:(id)output error:(NSError **)error;
+ (CGRect)roiForInput:(int)input arguments:(NSDictionary *)arguments outputRect:(CGRect)outputRect;
+ (CIFormat)formatForInputAtIndex:(int)input;
+ (CIFormat)outputFormat;
+ (BOOL)synchronizeInputs;
+ (BOOL)outputIsOpaque;
@end

@interface CIKernel : NSObject
+ (NSArray<CIKernel *> *)kernelsWithString:(NSString *)string;
+ (NSArray<CIKernel *> *)kernelsWithMetalString:(NSString *)source error:(NSError **)error;
+ (instancetype)kernelWithString:(NSString *)string;
+ (instancetype)kernelWithFunctionName:(NSString *)name fromMetalLibraryData:(NSData *)data error:(NSError **)error;
+ (instancetype)kernelWithFunctionName:(NSString *)name fromMetalLibraryData:(NSData *)data outputPixelFormat:(CIFormat)format error:(NSError **)error;
+ (NSArray<NSString *> *)kernelNamesFromMetalLibraryData:(NSData *)data;
- (void)dealloc;
- (NSString *)name;
- (void)setROISelector:(SEL)method;
- (CIImage *)applyWithExtent:(CGRect)extent roiCallback:(CIKernelROICallback)callback arguments:(NSArray *)args;
@end

@interface CIPDF417CodeDescriptor : CIBarcodeDescriptor

@end

@interface CIPlugIn : NSObject
+ (void)loadAllPlugIns;
+ (void)loadNonExecutablePlugIns;
+ (void)loadNonExecutablePlugIn:(NSURL *)url;
+ (void)loadPlugIn:(NSURL *)url allowNonExecutable:(BOOL)allowNonExecutable;
+ (void)loadPlugIn:(NSURL *)url allowExecutableCode:(BOOL)allowExecutableCode;
@end

@interface CIQRCodeDescriptor : CIBarcodeDescriptor

@end

@interface CIQRCodeFeature : CIFeature
+ (BOOL)supportsSecureCoding;
- (NSString *)type;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CIRAWFilter : CIFilter
+ (NSArray<NSString *> *)supportedCameraModels;
+ (instancetype)filterWithImageURL:(NSURL *)url;
+ (instancetype)filterWithImageData:(NSData *)data identifierHint:(NSString *)identifierHint;
+ (instancetype)filterWithCVPixelBuffer:(CVPixelBufferRef)buffer properties:(NSDictionary *)properties;
@end

@interface CIRectangleFeature : CIFeature
- (NSString *)type;
@end

@interface CIRenderDestination : NSObject
- (instancetype)initWithBitmapData:(void *)data width:(NSUInteger)width height:(NSUInteger)height bytesPerRow:(NSUInteger)bytesPerRow format:(CIFormat)format;
- (instancetype)initWithPixelBuffer:(CVPixelBufferRef)pixelBuffer;
- (instancetype)initWithIOSurface:(IOSurface *)surface;
- (instancetype)initWithMTLTexture:(id)texture commandBuffer:(id)commandBuffer;
- (instancetype)initWithWidth:(NSUInteger)width height:(NSUInteger)height pixelFormat:(MTLPixelFormat)pixelFormat commandBuffer:(id)commandBuffer mtlTextureProvider:(id<MTLTexture> (^)(void))block;
- (instancetype)initWithGLTexture:(unsigned int)texture target:(unsigned int)target width:(NSUInteger)width height:(NSUInteger)height;
- (void)dealloc;
- (NSUInteger)width;
- (NSUInteger)height;
- (CIRenderDestinationAlphaMode)alphaMode;
- (void)setAlphaMode:(CIRenderDestinationAlphaMode)mode;
- (BOOL)isFlipped;
- (void)setFlipped:(BOOL)flipped;
- (BOOL)isDithered;
- (void)setDithered:(BOOL)dithered;
- (BOOL)isClamped;
- (void)setClamped:(BOOL)clamped;
- (CGColorSpaceRef)colorSpace;
- (void)setColorSpace:(CGColorSpaceRef)space;
- (CIBlendKernel *)blendKernel;
- (void)setBlendKernel:(CIBlendKernel *)k;
- (BOOL)blendsInDestinationColorSpace;
- (void)setBlendsInDestinationColorSpace:(BOOL)b;
@property(readonly,getter=isFlipped) BOOL flipped;
@property(readonly,getter=isDithered) BOOL dithered;
@property(readonly,getter=isClamped) BOOL clamped;
@end

@interface CIRenderInfo : NSObject
- (NSTimeInterval)kernelExecutionTime;
- (NSTimeInterval)kernelCompileTime;
- (NSInteger)passCount;
- (NSInteger)pixelsProcessed;
@end

@interface CIRenderTask : NSObject
- (CIRenderInfo *)waitUntilCompletedAndReturnError:(NSError **)error;
@end

@interface CISampler : NSObject
+ (instancetype)samplerWithImage:(CIImage *)im;
+ (instancetype)samplerWithImage:(CIImage *)im options:(NSDictionary *)dict;
+ (instancetype)samplerWithImage:(CIImage *)im keysAndValues:(id)key0, ...;
- (instancetype)initWithImage:(CIImage *)im;
- (instancetype)initWithImage:(CIImage *)im keysAndValues:(id)key0, ...;
- (instancetype)initWithImage:(CIImage *)im options:(NSDictionary *)dict;
- (void)dealloc;
- (CIFilterShape *)definition;
- (CGRect)extent;
- (id)copyWithZone:(NSZone *)zone;
@end

@interface CITextFeature : CIFeature
- (NSString *)type;
@end

@interface CIVector : NSObject
+ (BOOL)supportsSecureCoding;
- (instancetype)initWithValues:(const CGFloat *)values count:(size_t)count;
- (instancetype)initWithX:(CGFloat)x;
- (instancetype)initWithX:(CGFloat)x Y:(CGFloat)y;
- (instancetype)initWithX:(CGFloat)x Y:(CGFloat)y Z:(CGFloat)z;
- (instancetype)initWithX:(CGFloat)x Y:(CGFloat)y Z:(CGFloat)z W:(CGFloat)w;
- (instancetype)initWithCGPoint:(CGPoint)p;
- (instancetype)initWithCGRect:(CGRect)r;
- (instancetype)initWithCGAffineTransform:(CGAffineTransform)t;
- (instancetype)initWithString:(NSString *)representation;
+ (instancetype)vectorWithValues:(const CGFloat *)values count:(size_t)count;
+ (instancetype)vectorWithX:(CGFloat)x;
+ (instancetype)vectorWithX:(CGFloat)x Y:(CGFloat)y;
+ (instancetype)vectorWithX:(CGFloat)x Y:(CGFloat)y Z:(CGFloat)z;
+ (instancetype)vectorWithX:(CGFloat)x Y:(CGFloat)y Z:(CGFloat)z W:(CGFloat)w;
+ (instancetype)vectorWithCGPoint:(CGPoint)p;
+ (instancetype)vectorWithCGRect:(CGRect)r;
+ (instancetype)vectorWithCGAffineTransform:(CGAffineTransform)t;
+ (instancetype)vectorWithString:(NSString *)representation;
- (void)dealloc;
- (CGFloat)valueAtIndex:(size_t)index;
- (size_t)count;
- (CGFloat)X;
- (CGFloat)Y;
- (CGFloat)Z;
- (CGFloat)W;
- (CGPoint)CGPointValue;
- (CGRect)CGRectValue;
- (CGAffineTransform)CGAffineTransformValue;
- (NSString *)stringRepresentation;
- (NSString *)description;
- (BOOL)isEqual:(id)other;
- (NSUInteger)hash;
- (id)copyWithZone:(NSZone *)zone;
- (void)encodeWithCoder:(NSCoder *)coder;
- (instancetype)initWithCoder:(NSCoder *)coder;
@end

@interface CIWarpKernel : CIKernel
- (CIImage *)applyWithExtent:(CGRect)extent roiCallback:(CIKernelROICallback)callback inputImage:(CIImage *)image arguments:(NSArray *)args;
@end

@interface CIAztecCodeDescriptor : CIBarcodeDescriptor

@end

@interface CIColorKernel : CIKernel
- (CIImage *)applyWithExtent:(CGRect)extent arguments:(NSArray *)args;
@end

@interface CIFaceFeature : CIFeature
- (NSString *)type;
@end

@interface CIBlendKernel : CIColorKernel
- (CIImage *)applyWithForeground:(CIImage *)foreground background:(CIImage *)background;
- (CIImage *)applyWithForeground:(CIImage *)foreground background:(CIImage *)background colorSpace:(CGColorSpaceRef)colorSpace;
+(CIBlendKernel *)sel;
@end

extern const int kCIFormat420f;
extern const int kCIFormat420f10;
extern const int kCIFormat420f10packed;
extern const int kCIFormat420f601;
extern const int kCIFormat420f709;
extern const int kCIFormat420fh;
extern const int kCIFormat420v;
extern const int kCIFormat420v10;
extern const int kCIFormat420v10packed;
extern const int kCIFormat420v601;
extern const int kCIFormat420v709;
extern const int kCIFormat422f;
extern const int kCIFormat422v;
extern const int kCIFormat444f601;
extern const int kCIFormat444f709;
extern const int kCIFormat444v601;
extern const int kCIFormat444v709;
extern const int kCIFormatA16;
extern const int kCIFormatA2BGR10;
extern const int kCIFormatA2RGB10;
extern const int kCIFormatA8;
extern const int kCIFormatABGR8;
extern const int kCIFormatARGB10WideGamut;
extern const int kCIFormatARGB10WideLinear;
extern const int kCIFormatARGB16;
extern const int kCIFormatARGB8;
extern const int kCIFormatARGBf;
extern const int kCIFormatAf;
extern const int kCIFormatAh;
extern const int kCIFormatAllowSRGB;
extern const int kCIFormatBGRA8;
extern const int kCIFormatBGRX8;
extern const int kCIFormatCbYCrY8;
extern const int kCIFormatCbYCrY8f;
extern const int kCIFormatI16;
extern const int kCIFormatI8;
extern const int kCIFormatIf;
extern const int kCIFormatIh;
extern const int kCIFormatL10;
extern const int kCIFormatL12;
extern const int kCIFormatL16;
extern const int kCIFormatL8;
extern const int kCIFormatLA16;
extern const int kCIFormatLA8;
extern const int kCIFormatLAf;
extern const int kCIFormatLAh;
extern const int kCIFormatLf;
extern const int kCIFormatLh;
extern const int kCIFormatNil;
extern const int kCIFormatR10;
extern const int kCIFormatR10packed;
extern const int kCIFormatR12;
extern const int kCIFormatR16;
extern const int kCIFormatR8;
extern const int kCIFormatRG10;
extern const int kCIFormatRG10packed;
extern const int kCIFormatRG12;
extern const int kCIFormatRG16;
extern const int kCIFormatRG8;
extern const int kCIFormatRGB10;
extern const int kCIFormatRGB10WideGamut;
extern const int kCIFormatRGB10WideLinear;
extern const int kCIFormatRGB16;
extern const int kCIFormatRGB8;
extern const int kCIFormatRGBA14;
extern const int kCIFormatRGBA16;
extern const int kCIFormatRGBA8;
extern const int kCIFormatRGBAf;
extern const int kCIFormatRGBAh;
extern const int kCIFormatRGBX16;
extern const int kCIFormatRGBX8;
extern const int kCIFormatRGBXf;
extern const int kCIFormatRGBXh;
extern const int kCIFormatRGBf;
extern const int kCIFormatRGBh;
extern const int kCIFormatRGf;
extern const int kCIFormatRGh;
extern const int kCIFormatRf;
extern const int kCIFormatRh;
extern const int kCIFormatXBGR8;
extern const int kCIFormatXRGB16;
extern const int kCIFormatXRGB8;
extern const int kCIFormatXRGBf;
extern const int kCIFormatYCbYCr8;
extern const int kCIFormatYCbYCr8f;

@interface CIColor (SFblackColor)
+ (CIColor *)blackColor;
@end

@interface CIColor (SFwhiteColor)
+ (CIColor *)whiteColor;
@end

@interface CIColor (SFgrayColor)
+ (CIColor *)grayColor;
@end

@interface CIColor (SFredColor)
+ (CIColor *)redColor;
@end

@interface CIColor (SFgreenColor)
+ (CIColor *)greenColor;
@end

@interface CIColor (SFblueColor)
+ (CIColor *)blueColor;
@end

@interface CIColor (SFcyanColor)
+ (CIColor *)cyanColor;
@end

@interface CIColor (SFmagentaColor)
+ (CIColor *)magentaColor;
@end

@interface CIColor (SFyellowColor)
+ (CIColor *)yellowColor;
@end

@interface CIColor (SFclearColor)
+ (CIColor *)clearColor;
@end

@interface CIBlendKernel (SFNamedKernels)
+ (CIBlendKernel *)componentAdd;
+ (CIBlendKernel *)componentMultiply;
+ (CIBlendKernel *)componentMin;
+ (CIBlendKernel *)componentMax;
+ (CIBlendKernel *)clear;
+ (CIBlendKernel *)source;
+ (CIBlendKernel *)destination;
+ (CIBlendKernel *)sourceOver;
+ (CIBlendKernel *)destinationOver;
+ (CIBlendKernel *)sourceIn;
+ (CIBlendKernel *)destinationIn;
+ (CIBlendKernel *)sourceOut;
+ (CIBlendKernel *)destinationOut;
+ (CIBlendKernel *)sourceAtop;
+ (CIBlendKernel *)destinationAtop;
+ (CIBlendKernel *)exclusiveOr;
+ (CIBlendKernel *)multiply;
+ (CIBlendKernel *)screen;
+ (CIBlendKernel *)overlay;
+ (CIBlendKernel *)darken;
+ (CIBlendKernel *)lighten;
+ (CIBlendKernel *)colorDodge;
+ (CIBlendKernel *)colorBurn;
+ (CIBlendKernel *)hardLight;
+ (CIBlendKernel *)softLight;
+ (CIBlendKernel *)difference;
+ (CIBlendKernel *)exclusion;
+ (CIBlendKernel *)hue;
+ (CIBlendKernel *)saturation;
+ (CIBlendKernel *)color;
+ (CIBlendKernel *)luminosity;
+ (CIBlendKernel *)subtract;
+ (CIBlendKernel *)divide;
+ (CIBlendKernel *)linearBurn;
+ (CIBlendKernel *)linearDodge;
+ (CIBlendKernel *)vividLight;
+ (CIBlendKernel *)linearLight;
+ (CIBlendKernel *)pinLight;
+ (CIBlendKernel *)hardMix;
+ (CIBlendKernel *)darkerColor;
+ (CIBlendKernel *)lighterColor;
@end
#import <CoreImage/CIFilterFactories.h>
