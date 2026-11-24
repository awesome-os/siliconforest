/* SPDX-License-Identifier: MIT */
/* MediaPlayer for macOS applications (TERM8 test fixture, proposed to CODEX, the media owner): what VLC 3's interface
   imports weakly. SiliconForest has no system "Now Playing" service and routes no media keys to applications: the
   now-playing information an application sets is kept and shown nowhere, and remote commands never fire. Names and
   string values are the public API's. */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CGGeometry.h>
#import <dispatch/dispatch.h>

#define EXPORT __attribute__((visibility("default")))

EXPORT NSString *const MPMediaItemPropertyTitle = @"title";
EXPORT NSString *const MPMediaItemPropertyAlbumTitle = @"albumTitle";
EXPORT NSString *const MPMediaItemPropertyArtist = @"artist";
EXPORT NSString *const MPMediaItemPropertyArtwork = @"artwork";
EXPORT NSString *const MPMediaItemPropertyPlaybackDuration = @"playbackDuration";
EXPORT NSString *const MPMediaItemPropertyAlbumTrackNumber = @"albumTrackNumber";
EXPORT NSString *const MPNowPlayingInfoPropertyElapsedPlaybackTime = @"MPNowPlayingInfoPropertyElapsedPlaybackTime";
EXPORT NSString *const MPNowPlayingInfoPropertyIsLiveStream = @"MPNowPlayingInfoPropertyIsLiveStream";
EXPORT NSString *const MPNowPlayingInfoPropertyPlaybackProgress = @"MPNowPlayingInfoPropertyPlaybackProgress";
EXPORT NSString *const MPNowPlayingInfoPropertyPlaybackRate = @"MPNowPlayingInfoPropertyPlaybackRate";

EXPORT @interface MPNowPlayingInfoCenter : NSObject {
    NSDictionary *_info;
    NSUInteger _state;
}
@end

@implementation MPNowPlayingInfoCenter
+ (MPNowPlayingInfoCenter *)defaultCenter {
    static MPNowPlayingInfoCenter *center;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ center = [[MPNowPlayingInfoCenter alloc] init]; });
    return center;
}
- (NSDictionary *)nowPlayingInfo { @synchronized(self) { return [[_info retain] autorelease]; } }
- (void)setNowPlayingInfo:(NSDictionary *)info { @synchronized(self) { [_info release]; _info = [info copy]; } }
- (NSUInteger)playbackState { @synchronized(self) { return _state; } }
- (void)setPlaybackState:(NSUInteger)state { @synchronized(self) { _state = state; } }
- (void)dealloc { [_info release]; [super dealloc]; }
@end

/* Local registrations only. Target/action registrations do not retain targets;
 * opaque handler tokens own copied blocks. No system event source is invented. */
@interface SFMPRegistration : NSObject {
@public
    void *target;
    SEL action;
    id handler;
}
@end
@implementation SFMPRegistration
- (void)dealloc { [handler release]; [super dealloc]; }
@end

EXPORT @interface MPRemoteCommand : NSObject {
    BOOL _enabled;
    NSMutableArray *_targets;
}
@end

@implementation MPRemoteCommand
- (instancetype)init {
    if ((self = [super init]) != nil) {
        _enabled = YES;
        _targets = [NSMutableArray new];
    }
    return self;
}
- (void)dealloc { [_targets release]; [super dealloc]; }
- (BOOL)isEnabled { return _enabled; }
- (void)setEnabled:(BOOL)enabled { _enabled = enabled; }
- (void)addTarget:(id)target action:(SEL)action {
    if (!target || !action) return;
    @synchronized(self) {
        for (SFMPRegistration *r in _targets)
            if (r->target == (void *)target && r->action == action) return;
        SFMPRegistration *r = [SFMPRegistration new];
        r->target = (void *)target; r->action = action;
        [_targets addObject:r]; [r release];
    }
}
- (id)addTargetWithHandler:(id)handler {
    if (!handler) return nil;
    SFMPRegistration *r = [SFMPRegistration new];
    r->handler = [handler copy];
    @synchronized(self) { [_targets addObject:r]; }
    return [r autorelease];
}
- (void)removeTarget:(id)target { [self removeTarget:target action:NULL]; }
- (void)removeTarget:(id)target action:(SEL)action {
    @synchronized(self) {
        for (NSUInteger i = [_targets count]; i > 0; --i) {
            SFMPRegistration *r = [_targets objectAtIndex:i - 1];
            BOOL matchesTarget = !target || target == r || (void *)target == r->target;
            if (matchesTarget && (!action || action == r->action))
                [_targets removeObjectAtIndex:i - 1];
        }
    }
}
@end

EXPORT @interface MPSkipIntervalCommand : MPRemoteCommand {
    NSArray *_intervals;
}
@end

@implementation MPSkipIntervalCommand
- (void)dealloc { [_intervals release]; [super dealloc]; }
- (NSArray *)preferredIntervals { return _intervals; }
- (void)setPreferredIntervals:(NSArray *)intervals { [_intervals release]; _intervals = [intervals copy]; }
@end

EXPORT @interface MPChangePlaybackPositionCommand : MPRemoteCommand
@end
@implementation MPChangePlaybackPositionCommand
@end

EXPORT @interface MPRemoteCommandCenter : NSObject {
    NSMutableDictionary *_commands;
}
@end

@implementation MPRemoteCommandCenter
+ (MPRemoteCommandCenter *)sharedCommandCenter {
    static MPRemoteCommandCenter *center;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        center = [[MPRemoteCommandCenter alloc] init];
        NSLog(@"MediaPlayer: no system Now Playing or remote-command event service; local state only");
    });
    return center;
}
- (instancetype)init {
    if ((self = [super init]) != nil)
        _commands = [NSMutableDictionary new];
    return self;
}
- (void)dealloc { [_commands release]; [super dealloc]; }
- (id)_command:(NSString *)name class:(Class)cls {
    @synchronized(self) {
        id command = [_commands objectForKey:name];
        if (command == nil) {
            command = [[[cls alloc] init] autorelease];
            [_commands setObject:command forKey:name];
        }
        return command;
    }
}
#define COMMAND(name, cls) - (id)name { return [self _command:@#name class:[cls class]]; }
COMMAND(pauseCommand, MPRemoteCommand)
COMMAND(playCommand, MPRemoteCommand)
COMMAND(stopCommand, MPRemoteCommand)
COMMAND(togglePlayPauseCommand, MPRemoteCommand)
COMMAND(nextTrackCommand, MPRemoteCommand)
COMMAND(previousTrackCommand, MPRemoteCommand)
COMMAND(changeRepeatModeCommand, MPRemoteCommand)
COMMAND(changeShuffleModeCommand, MPRemoteCommand)
COMMAND(changePlaybackRateCommand, MPRemoteCommand)
COMMAND(seekBackwardCommand, MPRemoteCommand)
COMMAND(seekForwardCommand, MPRemoteCommand)
COMMAND(skipForwardCommand, MPSkipIntervalCommand)
COMMAND(skipBackwardCommand, MPSkipIntervalCommand)
COMMAND(changePlaybackPositionCommand, MPChangePlaybackPositionCommand)
COMMAND(ratingCommand, MPRemoteCommand)
COMMAND(likeCommand, MPRemoteCommand)
COMMAND(dislikeCommand, MPRemoteCommand)
COMMAND(bookmarkCommand, MPRemoteCommand)
COMMAND(enableLanguageOptionCommand, MPRemoteCommand)
COMMAND(disableLanguageOptionCommand, MPRemoteCommand)
@end

EXPORT @interface MPMediaItemArtwork : NSObject {
    CGRect _bounds;
    id _handler;
}
@end

@implementation MPMediaItemArtwork
- (instancetype)initWithBoundsSize:(CGSize)size requestHandler:(id (^)(CGSize))handler {
    if ((self = [super init]) != nil) {
        _bounds = CGRectMake(0, 0, size.width, size.height);
        _handler = [handler copy];
    }
    return self;
}
- (void)dealloc { [_handler release]; [super dealloc]; }
- (CGRect)bounds { return _bounds; }
- (id)imageWithSize:(CGSize)size { return _handler ? ((id (^)(CGSize))_handler)(size) : nil; }
@end
