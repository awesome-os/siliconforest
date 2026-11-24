/* SPDX-License-Identifier: MIT */
/* Source-derived declarations; no SDK. These enums also appear in public objc2 bindings. */
@protocol MTLDrawable;
#import <QuartzCore/QuartzCore.h>
#import <AVFoundation/SFAssetCore.h>
typedef NSInteger AVPlayerItemStatus,AVPlayerStatus,AVPlayerTimeControlStatus,AVPlayerActionAtItemEnd,AVPlayerLooperStatus,AVPlayerLooperItemOrdering,AVContentAuthorizationStatus;
typedef NSString *AVLayerVideoGravity,*AVPlayerWaitingReason;
@class AVMediaPresentationSelector,AVMediaPresentationSetting;
@class AVPlayerItemAccessLog,AVPlayerItemErrorLog,AVPlayerItemMediaDataCollector,AVPlayerItemMetadataCollector,AVVideoComposition;
@protocol AVPlayerItemOutputPullDelegate <NSObject>
@optional
- (void)outputMediaDataWillChange:(id)output;
@end
@protocol AVPlayerItemMetadataCollectorPushDelegate <NSObject>
@end
#define AVPlayerItemStatusUnknown 0
#define AVPlayerItemStatusReadyToPlay 1
#define AVPlayerItemStatusFailed 2
#define AVPlayerStatusUnknown 0
#define AVPlayerStatusReadyToPlay 1
#define AVPlayerTimeControlStatusPaused 0
#define AVPlayerTimeControlStatusWaitingToPlayAtSpecifiedRate 1
#define AVPlayerTimeControlStatusPlaying 2
#define AVPlayerActionAtItemEndAdvance 0
#define AVPlayerActionAtItemEndPause 1
#define AVPlayerLooperStatusReady 1
#define AVContentAuthorizationNotAvailable 0
extern NSString *const AVLayerVideoGravityResize,*const AVLayerVideoGravityResizeAspect,*const AVLayerVideoGravityResizeAspectFill;
extern NSString *const AVPlayerItemDidPlayToEndTimeNotification,*const AVPlayerWaitingToMinimizeStallsReason;

/* Source uses these optional model properties; their classes are not supplied here. */
@interface AVCustomMediaSelectionScheme : NSObject
@property(readonly) NSArray<NSString *> *availableLanguages;
@property(readonly) NSArray<AVMediaPresentationSelector *> *selectors;
@end
@interface AVMediaPresentationSelector : NSObject
@property(readonly) NSArray<AVMediaPresentationSetting *> *settings;
@end
@interface AVMediaPresentationSetting : NSObject
@property(readonly) AVMediaCharacteristic mediaCharacteristic;
@end
@interface CALayer (SFPlayerLayerCopyDeclaration)
- (id)initWithLayer:(id)layer;
@end
CGRect AVMakeRectWithAspectRatioInsideRect(CGSize aspectRatio,CGRect boundingRect);
