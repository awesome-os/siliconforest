/* SPDX-License-Identifier: MIT */
/* Own property encodings from public maintainer binding facts (objc2-core-image0.3.2). */
#pragma once
#import <CoreImage/CoreImage.h>
@protocol CIFilterProtocol <NSObject>
@property(readonly) CIImage *outputImage;
@end
@protocol CIAffineClamp <CIFilterProtocol>
@property CIImage * inputImage;
@property CGAffineTransform transform;
@end
@protocol CIAffineTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGAffineTransform transform;
@end
@protocol CIAreaReductionFilter <CIFilterProtocol>
@property CIImage * inputImage;
@property CGRect extent;
@end
@protocol CIAreaAverage <CIAreaReductionFilter>
@property float scale;
@property NSInteger count;
@end
@protocol CIAreaMaximum <CIAreaReductionFilter>
@property CIImage * inputImage;
@property float height;
@property float highLimit;
@property float lowLimit;
@end
@protocol CIAreaMinimum <CIAreaReductionFilter>
@property CIImage * inputImage;
@property float height;
@property float highLimit;
@property float lowLimit;
@end
@protocol CITransitionFilter <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * targetImage;
@property float time;
@end
@protocol CIBarsSwipeTransition <CITransitionFilter>
@property float angle;
@property float width;
@property float barOffset;
@end
@protocol CIBlendWithMask <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * backgroundImage;
@property CIImage * maskImage;
@end
@protocol CIBlurredRectangleGenerator <CIFilterProtocol>
@property CGRect extent;
@property float sigma;
@property CIColor * color;
@end
@protocol CIBlurredRoundedRectangleGenerator <CIFilterProtocol>
@property CGRect extent;
@property float radius;
@property float smoothness;
@property float sigma;
@property CIColor * color;
@end
@protocol CIBoxBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIBumpDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float scale;
@end
@protocol CIBumpDistortionLinear <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float angle;
@property float scale;
@end
@protocol CICMYKHalftone <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float width;
@property float angle;
@property float sharpness;
@property float grayComponentReplacement;
@property float underColorRemoval;
@end
@protocol CICheckerboardGenerator <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color0;
@property CIColor * color1;
@property float width;
@property float sharpness;
@end
@protocol CICircleSplashDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@end
@protocol CICircularScreen <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float width;
@property float sharpness;
@end
@protocol CICircularWrap <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float angle;
@end
@protocol CIColorAbsoluteDifference <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * inputImage2;
@end
@protocol CIColorClamp <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * minComponents;
@property CIVector * maxComponents;
@end
@protocol CIColorControls <CIFilterProtocol>
@property CIImage * inputImage;
@property float saturation;
@property float brightness;
@property float contrast;
@end
@protocol CIColorCrossPolynomial <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * redCoefficients;
@property CIVector * greenCoefficients;
@property CIVector * blueCoefficients;
@end
@protocol CIColorCube <CIFilterProtocol>
@property CIImage * inputImage;
@property float cubeDimension;
@property NSData * cubeData;
@property BOOL extrapolate;
@end
@protocol CIColorCubeWithColorSpace <CIFilterProtocol>
@property CIImage * inputImage;
@property float cubeDimension;
@property NSData * cubeData;
@property BOOL extrapolate;
@property CGColorSpaceRef colorSpace;
@end
@protocol CIColorCubesMixedWithMask <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * maskImage;
@property float cubeDimension;
@property NSData * cube0Data;
@property NSData * cube1Data;
@property CGColorSpaceRef colorSpace;
@property BOOL extrapolate;
@end
@protocol CIColorInvert <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIColorMap <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * gradientImage;
@end
@protocol CIColorMatrix <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * RVector;
@property CIVector * GVector;
@property CIVector * BVector;
@property CIVector * AVector;
@property CIVector * biasVector;
@end
@protocol CIColorMonochrome <CIFilterProtocol>
@property CIImage * inputImage;
@property CIColor * color;
@property float intensity;
@end
@protocol CIColorPolynomial <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * redCoefficients;
@property CIVector * greenCoefficients;
@property CIVector * blueCoefficients;
@property CIVector * alphaCoefficients;
@end
@protocol CIColorPosterize <CIFilterProtocol>
@property CIImage * inputImage;
@property float levels;
@end
@protocol CIColorThreshold <CIFilterProtocol>
@property CIImage * inputImage;
@property float threshold;
@end
@protocol CICompositeOperation <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * backgroundImage;
@end
@protocol CIConvolution <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * weights;
@property float bias;
@end
@protocol CICopyMachineTransition <CITransitionFilter>
@property CGRect extent;
@property CIColor * color;
@property float angle;
@property float width;
@property float opacity;
@end
@protocol CIDiscBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIDisintegrateWithMaskTransition <CITransitionFilter>
@property CIImage * maskImage;
@property float shadowRadius;
@property float shadowDensity;
@property CGPoint shadowOffset;
@end
@protocol CIDisplacementDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * displacementImage;
@property float scale;
@end
@protocol CIDissolveTransition <CITransitionFilter>
@property CGPoint center;
@property CGRect extent;
@property CIColor * color;
@property float maxStriationRadius;
@property float striationStrength;
@property float striationContrast;
@property float fadeThreshold;
@end
@protocol CIDotScreen <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@property float sharpness;
@end
@protocol CIEdgeWork <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIEdges <CIFilterProtocol>
@property CIImage * inputImage;
@property float intensity;
@end
@protocol CIEightfoldReflectedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CIExposureAdjust <CIFilterProtocol>
@property CIImage * inputImage;
@property float EV;
@end
@protocol CIFalseColor <CIFilterProtocol>
@property CIImage * inputImage;
@property CIColor * color0;
@property CIColor * color1;
@end
@protocol CIFlashTransition <CITransitionFilter>
@property CGPoint center;
@property CGRect extent;
@property CIColor * color;
@property float maxStriationRadius;
@property float striationStrength;
@property float striationContrast;
@property float fadeThreshold;
@end
@protocol CIFourfoldReflectedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@property float acuteAngle;
@end
@protocol CIFourfoldRotatedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CIFourfoldTranslatedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@property float acuteAngle;
@end
@protocol CIGammaAdjust <CIFilterProtocol>
@property CIImage * inputImage;
@property float power;
@end
@protocol CIGaussianBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIGaussianGradient <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color0;
@property CIColor * color1;
@property float radius;
@end
@protocol CIGlassDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * textureImage;
@property CGPoint center;
@property float scale;
@end
@protocol CIGlassLozenge <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint point0;
@property CGPoint point1;
@property float radius;
@property float refraction;
@end
@protocol CIGlideReflectedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CIGloom <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@property float intensity;
@end
@protocol CIHatchedScreen <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@property float sharpness;
@end
@protocol CIHighlightShadowAdjust <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@property float shadowAmount;
@property float highlightAmount;
@end
@protocol CIHoleDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@end
@protocol CIHueAdjust <CIFilterProtocol>
@property CIImage * inputImage;
@property float angle;
@end
@protocol CIHueSaturationValueGradient <CIFilterProtocol>
@property float value;
@property float radius;
@property float softness;
@property float dither;
@property CGColorSpaceRef colorSpace;
@end
@protocol CIKaleidoscope <CIFilterProtocol>
@property CIImage * inputImage;
@property NSInteger count;
@property CGPoint center;
@property float angle;
@end
@protocol CILanczosScaleTransform <CIFilterProtocol>
@property CIImage * inputImage;
@property float scale;
@property float aspectRatio;
@end
@protocol CILenticularHaloGenerator <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color;
@property float haloRadius;
@property float haloWidth;
@property float haloOverlap;
@property float striationStrength;
@property float striationContrast;
@property float time;
@end
@protocol CILightTunnel <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float rotation;
@property float radius;
@end
@protocol CILineScreen <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@property float sharpness;
@end
@protocol CILinearGradient <CIFilterProtocol>
@property CGPoint point0;
@property CGPoint point1;
@property CIColor * color0;
@property CIColor * color1;
@end
@protocol CILinearToSRGBToneCurve <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIMaskToAlpha <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIMaskedVariableBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property CIImage * mask;
@property float radius;
@end
@protocol CIMaximumComponent <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIMedian <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIMinimumComponent <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CIModTransition <CITransitionFilter>
@property CGPoint center;
@property float angle;
@property float radius;
@property float compression;
@end
@protocol CIMorphologyGradient <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIMorphologyMaximum <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIMorphologyMinimum <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@end
@protocol CIMorphologyRectangleMaximum <CIFilterProtocol>
@property CIImage * inputImage;
@property float width;
@property float height;
@end
@protocol CIMorphologyRectangleMinimum <CIFilterProtocol>
@property CIImage * inputImage;
@property float width;
@property float height;
@end
@protocol CIMotionBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@property float angle;
@end
@protocol CINinePartStretched <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint breakpoint0;
@property CGPoint breakpoint1;
@property CGPoint growAmount;
@end
@protocol CINinePartTiled <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint breakpoint0;
@property CGPoint breakpoint1;
@property CGPoint growAmount;
@property BOOL flipYTiles;
@end
@protocol CINoiseReduction <CIFilterProtocol>
@property CIImage * inputImage;
@property float noiseLevel;
@property float sharpness;
@end
@protocol CIOpTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float scale;
@property float angle;
@property float width;
@end
@protocol CIParallelogramTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float acuteAngle;
@property float width;
@end
@protocol CIPerspectiveTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint topLeft;
@property CGPoint topRight;
@property CGPoint bottomRight;
@property CGPoint bottomLeft;
@end
@protocol CIPinchDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float scale;
@end
@protocol CIPixellate <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float scale;
@end
@protocol CIRadialGradient <CIFilterProtocol>
@property CGPoint center;
@property float radius0;
@property float radius1;
@property CIColor * color0;
@property CIColor * color1;
@end
@protocol CIRoundedRectangleGenerator <CIFilterProtocol>
@property CGRect extent;
@property float radius;
@property float smoothness;
@property CIColor * color;
@end
@protocol CIRoundedRectangleStrokeGenerator <CIFilterProtocol>
@property CGRect extent;
@property float radius;
@property float smoothness;
@property CIColor * color;
@property float width;
@end
@protocol CISRGBToneCurveToLinear <CIFilterProtocol>
@property CIImage * inputImage;
@end
@protocol CISepiaTone <CIFilterProtocol>
@property CIImage * inputImage;
@property float intensity;
@end
@protocol CISharpenLuminance <CIFilterProtocol>
@property CIImage * inputImage;
@property float sharpness;
@property float radius;
@end
@protocol CISixfoldReflectedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CISixfoldRotatedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CISmoothLinearGradient <CIFilterProtocol>
@property CGPoint point0;
@property CGPoint point1;
@property CIColor * color0;
@property CIColor * color1;
@end
@protocol CISpotColor <CIFilterProtocol>
@property CIImage * inputImage;
@property CIColor * centerColor1;
@property CIColor * replacementColor1;
@property float closeness1;
@property float contrast1;
@property CIColor * centerColor2;
@property CIColor * replacementColor2;
@property float closeness2;
@property float contrast2;
@property CIColor * centerColor3;
@property CIColor * replacementColor3;
@property float closeness3;
@property float contrast3;
@end
@protocol CISpotLight <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * lightPosition;
@property CIVector * lightPointsAt;
@property float brightness;
@property float concentration;
@property CIColor * color;
@end
@protocol CIStarShineGenerator <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color;
@property float radius;
@property float crossScale;
@property float crossAngle;
@property float crossOpacity;
@property float crossWidth;
@property float epsilon;
@end
@protocol CIStripesGenerator <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color0;
@property CIColor * color1;
@property float width;
@property float sharpness;
@end
@protocol CIStraighten <CIFilterProtocol>
@property CIImage * inputImage;
@property float angle;
@end
@protocol CISunbeamsGenerator <CIFilterProtocol>
@property CGPoint center;
@property CIColor * color;
@property float sunRadius;
@property float maxStriationRadius;
@property float striationStrength;
@property float striationContrast;
@property float time;
@end
@protocol CISwipeTransition <CITransitionFilter>
@property CGRect extent;
@property CIColor * color;
@property float angle;
@property float width;
@property float opacity;
@end
@protocol CISystemToneMap <CIFilterProtocol>
@property CIImage * inputImage;
@property float displayHeadroom;
@property NSString * preferredDynamicRange;
@end
@protocol CITemperatureAndTint <CIFilterProtocol>
@property CIImage * inputImage;
@property CIVector * neutral;
@property CIVector * targetNeutral;
@end
@protocol CITextImageGenerator <CIFilterProtocol>
@property NSString * text;
@property NSString * fontName;
@property float fontSize;
@property float scaleFactor;
@property float padding;
@end
@protocol CIToneCurve <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint point0;
@property CGPoint point1;
@property CGPoint point2;
@property CGPoint point3;
@property CGPoint point4;
@property BOOL extrapolate;
@end
@protocol CITorusLensDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float width;
@property float refraction;
@end
@protocol CITriangleKaleidoscope <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint point;
@property float size;
@property float rotation;
@property float decay;
@end
@protocol CITriangleTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CITwelvefoldReflectedTile <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float angle;
@property float width;
@end
@protocol CITwirlDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float angle;
@end
@protocol CIUnsharpMask <CIFilterProtocol>
@property CIImage * inputImage;
@property float radius;
@property float intensity;
@end
@protocol CIVibrance <CIFilterProtocol>
@property CIImage * inputImage;
@property float amount;
@end
@protocol CIVignette <CIFilterProtocol>
@property CIImage * inputImage;
@property float intensity;
@property float radius;
@end
@protocol CIVignetteEffect <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float intensity;
@property float falloff;
@end
@protocol CIVortexDistortion <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float radius;
@property float angle;
@end
@protocol CIWhitePointAdjust <CIFilterProtocol>
@property CIImage * inputImage;
@property CIColor * color;
@end
@protocol CIZoomBlur <CIFilterProtocol>
@property CIImage * inputImage;
@property CGPoint center;
@property float amount;
@end
