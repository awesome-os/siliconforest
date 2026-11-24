/* SPDX-License-Identifier: MIT */
/* Source-derived declarations for Finch's no-device capture objects. No SDK. */
#pragma once
#import <AVFoundation/AVFoundation.h>
typedef NSString *AVCaptureDeviceType, *AVCaptureSessionPreset, *AVVideoCodecType;
typedef NSInteger AVCaptureDevicePosition;
@class AVCaptureDevice, AVCaptureDeviceDiscoverySession, AVCaptureInputPort,
       AVCaptureConnection, AVCaptureControl, AVMetadataObject;
@protocol AVCaptureVideoDataOutputSampleBufferDelegate;
@protocol AVCaptureAudioDataOutputSampleBufferDelegate;
@protocol AVCaptureSessionControlsDelegate;
@protocol AVCaptureSessionDeferredStartDelegate;
@interface AVCaptureDevice : NSObject
+ (NSArray<AVCaptureDevice *> *)devices;
+ (NSArray<AVCaptureDevice *> *)devicesWithMediaType:(AVMediaType)type;
@end
@interface AVCaptureDeviceDiscoverySession : NSObject
+ (instancetype)discoverySessionWithDeviceTypes:(NSArray<AVCaptureDeviceType> *)types mediaType:(AVMediaType)type position:(AVCaptureDevicePosition)position;
- (NSArray<AVCaptureDevice *> *)devices;
@end
@interface AVCaptureInput : NSObject
@end
@interface AVCaptureDeviceInput : AVCaptureInput
- (instancetype)initWithDevice:(AVCaptureDevice *)device error:(NSError **)error;
@end
@interface AVCaptureOutput : NSObject
- (instancetype)_finchInit;
@end
@interface AVCaptureVideoDataOutput : AVCaptureOutput
- (NSDictionary *)recommendedVideoSettingsForVideoCodecType:(AVVideoCodecType)codec assetWriterOutputFileType:(AVFileType)type;
@end
@interface AVCaptureAudioDataOutput : AVCaptureOutput
@end
@interface AVCaptureSession : NSObject
- (BOOL)canAddInput:(AVCaptureInput *)input;
- (BOOL)canAddOutput:(AVCaptureOutput *)output;
- (BOOL)canSetSessionPreset:(AVCaptureSessionPreset)preset;
- (void)startRunning;
- (BOOL)isRunning;
@end
/* Screen capture has no source implementation in Finch and no backend here.
 * Its initializers fail; never return an apparently usable input object. */
@interface AVCaptureScreenInput : AVCaptureInput
- (instancetype)initWithDisplayID:(uint32_t)display;
@end
