/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
typedef NSInteger AVPlayerViewControlsStyle,AVDisplayDynamicRange,AVRoutePickerViewButtonState;
typedef NSUInteger AVVideoFrameAnalysisType;
@class AVPlayerView,AVRoutePickerView,AVPictureInPictureController,AVPictureInPictureControllerContentSource;
@interface AVPlaybackSpeed : NSObject
@property(readonly) float rate;
@end
@protocol AVPlayerViewDelegate <NSObject>
@end
@protocol AVPlayerViewPictureInPictureDelegate <NSObject>
@end
@protocol AVRoutePickerViewDelegate <NSObject>
@optional
- (void)routePickerViewWillBeginPresentingRoutes:(AVRoutePickerView *)view;
- (void)routePickerViewDidEndPresentingRoutes:(AVRoutePickerView *)view;
@end
@protocol AVPictureInPictureControllerDelegate <NSObject>
@end
#define AVPlayerViewControlsStyleNone 0
#define AVPlayerViewControlsStyleInline 1
#define AVPlayerViewControlsStyleFloating 2
#define AVPlayerViewControlsStyleMinimal 3

#define AVRoutePickerViewButtonStateNormal 0
@interface NSView (SFAVKitFullscreenDeclarations)
@property(readonly,getter=isInFullScreenMode) BOOL inFullScreenMode;
- (BOOL)enterFullScreenMode:(NSScreen *)screen withOptions:(NSDictionary *)options;
- (void)exitFullScreenModeWithOptions:(NSDictionary *)options;
- (void)layout;
@end
