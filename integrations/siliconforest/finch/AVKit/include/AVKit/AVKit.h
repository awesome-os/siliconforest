/* SPDX-License-Identifier: MIT OR Apache-2.0 */
/* Method signatures derived from Finch 2d9b1bb implementations.
 * Type definitions and inheritance supplied by SiliconForest; no SDK headers. */
#pragma once
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>
@class AVPlayerView, AVRoutePickerView, AVPictureInPictureController;
/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AVKit/SFPlayerController.h>
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
/* Our meter drawing; property layouts checked by a Linux-built native probe. */
@interface AVAudioLevelIndicatorView : NSView {
    double _sfLevelValue;
}
@property double doubleValue;
@end

@interface NSView (SFAVKitFullscreenDeclarations)
@property(readonly,getter=isInFullScreenMode) BOOL inFullScreenMode;
- (BOOL)enterFullScreenMode:(NSScreen *)screen withOptions:(NSDictionary *)options;
- (void)exitFullScreenModeWithOptions:(NSDictionary *)options;
- (void)layout;
@end



@interface AVPlayerView : NSView
- (instancetype)initWithFrame:(NSRect)frame;
- (void)dealloc;
- (AVPlayer *)player;
- (void)setPlayer:(AVPlayer *)player;
- (void)layout;
- (void)drawRect:(NSRect)dirtyRect;
- (AVPlayerViewControlsStyle)controlsStyle;
- (void)setControlsStyle:(AVPlayerViewControlsStyle)style;
- (AVLayerVideoGravity)videoGravity;
- (void)setVideoGravity:(AVLayerVideoGravity)gravity;
- (BOOL)isReadyForDisplay;
- (NSRect)videoBounds;
- (NSView *)contentOverlayView;
- (BOOL)updatesNowPlayingInfoCenter;
- (void)setUpdatesNowPlayingInfoCenter:(BOOL)value;
- (id<AVPlayerViewDelegate>)delegate;
- (void)setDelegate:(id<AVPlayerViewDelegate>)delegate;
- (NSArray<AVPlaybackSpeed *> *)speeds;
- (void)setSpeeds:(NSArray<AVPlaybackSpeed *> *)speeds;
- (AVPlaybackSpeed *)selectedSpeed;
- (void)selectSpeed:(AVPlaybackSpeed *)speed;
- (BOOL)allowsVideoFrameAnalysis;
- (void)setAllowsVideoFrameAnalysis:(BOOL)value;
- (AVVideoFrameAnalysisType)videoFrameAnalysisTypes;
- (void)setVideoFrameAnalysisTypes:(AVVideoFrameAnalysisType)value;
- (BOOL)allowsMagnification;
- (void)setAllowsMagnification:(BOOL)value;
- (CGFloat)magnification;
- (void)setMagnification:(CGFloat)value;
- (void)setMagnification:(CGFloat)value centeredAtPoint:(CGPoint)point;
- (AVDisplayDynamicRange)preferredDisplayDynamicRange;
- (void)setPreferredDisplayDynamicRange:(AVDisplayDynamicRange)value;
- (BOOL)showsFrameSteppingButtons;
- (void)setShowsFrameSteppingButtons:(BOOL)value;
- (BOOL)showsSharingServiceButton;
- (void)setShowsSharingServiceButton:(BOOL)value;
- (NSMenu *)actionPopUpButtonMenu;
- (void)setActionPopUpButtonMenu:(NSMenu *)menu;
- (BOOL)showsFullScreenToggleButton;
- (void)setShowsFullScreenToggleButton:(BOOL)value;
- (BOOL)showsTimecodes;
- (void)setShowsTimecodes:(BOOL)value;
- (BOOL)allowsPictureInPicturePlayback;
- (void)setAllowsPictureInPicturePlayback:(BOOL)value;
- (id<AVPlayerViewPictureInPictureDelegate>)pictureInPictureDelegate;
- (void)setPictureInPictureDelegate:(id<AVPlayerViewPictureInPictureDelegate>)delegate;
@end

@interface AVRoutePickerView : NSView
- (instancetype)initWithFrame:(NSRect)frame;
- (void)dealloc;
- (id<AVRoutePickerViewDelegate>)delegate;
- (void)setDelegate:(id<AVRoutePickerViewDelegate>)delegate;
- (AVPlayer *)player;
- (void)setPlayer:(AVPlayer *)player;
- (NSColor *)routePickerButtonColorForState:(AVRoutePickerViewButtonState)state;
- (void)setRoutePickerButtonColor:(NSColor *)color forState:(AVRoutePickerViewButtonState)state;
- (BOOL)isRoutePickerButtonBordered;
- (void)setRoutePickerButtonBordered:(BOOL)value;
- (NSColor *)activeTintColor;
- (void)setActiveTintColor:(NSColor *)color;
- (NSSize)intrinsicContentSize;
- (NSString *)accessibilityLabel;
- (void)drawRect:(NSRect)dirtyRect;
@end

@interface AVPictureInPictureController : NSObject
+ (BOOL)isPictureInPictureSupported;
+ (NSImage *)pictureInPictureButtonStartImage;
+ (NSImage *)pictureInPictureButtonStopImage;
- (instancetype)init;
- (instancetype)initWithPlayerLayer:(AVPlayerLayer *)playerLayer;
- (instancetype)initWithContentSource:(AVPictureInPictureControllerContentSource *)contentSource;
- (void)dealloc;
- (AVPlayerLayer *)playerLayer;
- (AVPictureInPictureControllerContentSource *)contentSource;
- (void)setContentSource:(AVPictureInPictureControllerContentSource *)source;
- (id<AVPictureInPictureControllerDelegate>)delegate;
- (void)setDelegate:(id<AVPictureInPictureControllerDelegate>)delegate;
- (void)startPictureInPicture;
- (void)stopPictureInPicture;
- (BOOL)isPictureInPicturePossible;
- (BOOL)isPictureInPictureActive;
- (BOOL)isPictureInPictureSuspended;
- (BOOL)requiresLinearPlayback;
- (void)setRequiresLinearPlayback:(BOOL)value;
- (void)invalidatePlaybackState;
@end

/* Own source-observed range/duration ABI; no decoder or player side effects. */
CMTimeRange AVAssetSlowMotionTimeRangeFromNormalizedSlowMotionTimeRange(CMTimeRange range,CMTime duration);
CMTimeRange AVNormalizedSlowMotionTimeRangeFromAssetSlowMotionTimeRange(CMTimeRange range,CMTime duration);

/* Snapshot metadata identifiers. On PD assetURL is a usable path URL; persistent
 * file-reference URL creation/resolution is not available in Foundation. */
@interface AVAssetIdentifier : NSObject<NSCopying>
+ (instancetype)identifierForURL:(NSURL *)url;
+ (instancetype)identifierForAsset:(id)asset;
+ (instancetype)invalidIdentifier;
- (instancetype)initWithURL:(NSURL *)url;
- (instancetype)initWithAsset:(id)asset;
- (NSURL *)assetURL;
- (BOOL)isValid;
- (BOOL)matchesAsset:(id)asset;
@end
typedef struct {int16_t subframes,divisor;uint32_t counter,type,flags;int16_t hours,minutes,seconds,frames;} SFAVKitSMPTETime;
/* Native stores these independent values; changing frameNumber does not derive
 * SMPTE components or frameDuration. The string initializer populates components. */
@interface AVTimecode : NSObject
- (instancetype)initWithTimecodeString:(NSString *)string;
- (BOOL)stringIsValidNumericValue:(NSString *)string;
@property CMTime frameDuration;
@property int64_t frameNumber;
@property uint32_t tc_flags;
@property SFAVKitSMPTETime timecodeStruct;
@end

/* Source-owned stage diagnostics; CPU drawing is not panel presentation. */
@interface AVPlayerView (SiliconForestPlaybackDiagnostics)
- (AVPlayerLayer *)sfPlayerLayer;
- (NSUInteger)sfVideoDrawCount;
- (NSUInteger)sfTransportDrawCount;
@end
