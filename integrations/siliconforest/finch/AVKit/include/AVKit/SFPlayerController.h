/* SPDX-License-Identifier: MIT */
/* Bounded transport controller over our real AVPlayer. These are our own
 * declarations from runtime method types; the private UI family is absent. */
#pragma once
#import <Foundation/Foundation.h>
@class AVPlayer;
@interface AVPlayerController : NSObject {
    AVPlayer *_sfControllerPlayer;
    BOOL _sfControllerMuted;
}
@property(retain) AVPlayer *player;
@property double volume,rate,defaultPlaybackRate;
@property(getter=isMuted) BOOL muted;
@property(readonly) double currentTime,currentTimeWithinEndTimes;
@property(readonly) double contentDuration,contentDurationWithinEndTimes;
@property(readonly) double minTime,maxTime;
@property(readonly) BOOL canPlay,canPause,canSeek,hasContent;
@property(readonly) BOOL hasEnabledAudio,hasEnabledVideo,hasVideo;
@property(readonly,getter=isPlaying) BOOL playing;
@property(readonly) NSInteger status;
@property(readonly) NSError *error;
- (void)play:(id)sender;
- (void)pause:(id)sender;
- (void)togglePlayback:(id)sender;
- (void)seekToTime:(double)seconds;
@end
