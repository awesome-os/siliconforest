/* SPDX-License-Identifier: MIT */
/* Own public API declarations for the deliberately service-less media facade. */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CGGeometry.h>

extern NSString *const MPMediaItemPropertyTitle, *const MPMediaItemPropertyAlbumTitle;
extern NSString *const MPMediaItemPropertyArtist, *const MPMediaItemPropertyArtwork;
extern NSString *const MPMediaItemPropertyPlaybackDuration, *const MPMediaItemPropertyAlbumTrackNumber;
extern NSString *const MPNowPlayingInfoPropertyElapsedPlaybackTime, *const MPNowPlayingInfoPropertyIsLiveStream;
extern NSString *const MPNowPlayingInfoPropertyPlaybackProgress, *const MPNowPlayingInfoPropertyPlaybackRate;

@interface MPNowPlayingInfoCenter : NSObject
+ (instancetype)defaultCenter;
- (NSDictionary *)nowPlayingInfo;
- (void)setNowPlayingInfo:(NSDictionary *)info;
- (NSUInteger)playbackState;
- (void)setPlaybackState:(NSUInteger)state;
@end

@interface MPRemoteCommand : NSObject
- (BOOL)isEnabled;
- (void)setEnabled:(BOOL)enabled;
- (void)addTarget:(id)target action:(SEL)action;
- (id)addTargetWithHandler:(NSInteger (^)(id event))handler;
- (void)removeTarget:(id)target;
- (void)removeTarget:(id)target action:(SEL)action;
@end
@interface MPSkipIntervalCommand : MPRemoteCommand
- (NSArray *)preferredIntervals;
- (void)setPreferredIntervals:(NSArray *)intervals;
@end
@interface MPChangePlaybackPositionCommand : MPRemoteCommand
@end
@interface MPRemoteCommandCenter : NSObject
+ (instancetype)sharedCommandCenter;
- (MPRemoteCommand *)pauseCommand;
- (MPRemoteCommand *)playCommand;
- (MPRemoteCommand *)stopCommand;
- (MPRemoteCommand *)togglePlayPauseCommand;
- (MPRemoteCommand *)nextTrackCommand;
- (MPRemoteCommand *)previousTrackCommand;
- (MPRemoteCommand *)changeRepeatModeCommand;
- (MPRemoteCommand *)changeShuffleModeCommand;
- (MPRemoteCommand *)changePlaybackRateCommand;
- (MPRemoteCommand *)seekBackwardCommand;
- (MPRemoteCommand *)seekForwardCommand;
- (MPSkipIntervalCommand *)skipForwardCommand;
- (MPSkipIntervalCommand *)skipBackwardCommand;
- (MPChangePlaybackPositionCommand *)changePlaybackPositionCommand;
- (MPRemoteCommand *)ratingCommand;
- (MPRemoteCommand *)likeCommand;
- (MPRemoteCommand *)dislikeCommand;
- (MPRemoteCommand *)bookmarkCommand;
- (MPRemoteCommand *)enableLanguageOptionCommand;
- (MPRemoteCommand *)disableLanguageOptionCommand;
@end
@interface MPMediaItemArtwork : NSObject
- (instancetype)initWithBoundsSize:(CGSize)size requestHandler:(id (^)(CGSize size))handler;
- (CGRect)bounds;
- (id)imageWithSize:(CGSize)size;
@end
