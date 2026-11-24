/* SPDX-License-Identifier: MIT */
/* Own declarations from the selected PD implementations and our authorization
 * category. No Apple SDK headers. Include explicitly for this added surface. */
#pragma once
#import <AVFoundation/AVFoundation.h>
#import <QuartzCore/CALayer.h>
#import "AVCaptureCLI.h"
typedef NSInteger AVAuthorizationStatus;
enum { AVAuthorizationStatusNotDetermined=0, AVAuthorizationStatusRestricted=1,
       AVAuthorizationStatusDenied=2, AVAuthorizationStatusAuthorized=3 };
@interface AVCaptureDevice (SFRestrictedAuthorization)
+ (AVAuthorizationStatus)authorizationStatusForMediaType:(NSString *)type;
+ (void)requestAccessForMediaType:(NSString *)type completionHandler:(void (^)(BOOL))handler;
@end
@interface AVSpeechSynthesisVoice : NSObject
+ (NSArray *)speechVoices;
+ (NSString *)currentLanguageCode;
+ (instancetype)voiceWithLanguage:(NSString *)code;
+ (instancetype)voiceWithIdentifier:(NSString *)identifier;
- (NSString *)identifier;
- (NSString *)name;
- (NSString *)language;
@end
@interface AVSpeechUtterance : NSObject
+ (instancetype)speechUtteranceWithString:(NSString *)string;
- (instancetype)initWithString:(NSString *)string;
@property(readonly) NSString *speechString;
@property(retain) AVSpeechSynthesisVoice *voice;
@property float rate,pitchMultiplier,volume;
@end
/* No synthesizer engine: voices are empty and speech cancels asynchronously. */
@interface AVSpeechSynthesizer : NSObject
@property(assign) id delegate;
@property(readonly,getter=isSpeaking) BOOL speaking;
@property(readonly,getter=isPaused) BOOL paused;
- (void)speakUtterance:(AVSpeechUtterance *)utterance;
- (BOOL)stopSpeakingAtBoundary:(NSInteger)boundary;
- (BOOL)pauseSpeakingAtBoundary:(NSInteger)boundary;
- (BOOL)continueSpeaking;
@end
/* Immediate decoded-image layer; not a compressed sample decoder or scheduler.
 * NV12/P010 requires the qualified QuartzCore IOSurface/CGL rendering path. */
@interface AVSampleBufferDisplayLayer : CALayer
@property CMTimebaseRef controlTimebase;
@property(copy) NSString *videoGravity;
@property(readonly) NSInteger status;
@property(readonly) NSError *error;
@property(readonly,getter=isReadyForMoreMediaData) BOOL readyForMoreMediaData;
@property(readonly) BOOL requiresFlushToResumeDecoding;
@property BOOL preventsCapture;
- (void)enqueueSampleBuffer:(CMSampleBufferRef)sample;
- (void)flush;
- (void)flushAndRemoveImage;
- (void)requestMediaDataWhenReadyOnQueue:(dispatch_queue_t)queue usingBlock:(void (^)(void))block;
- (void)stopRequestingMediaData;
@end
