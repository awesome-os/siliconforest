/* SPDX-License-Identifier: MIT */
/* Scalar/model observations only, with our own runtime-derived declarations.
 * Never start playback, open a device/file, create a view or change services. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <time.h>
#include <math.h>
@interface NSObject(SFControllerValues)
- (id)player;
- (id)initWithURL:(NSURL *)url;
- (void)setPlayer:(id)player;
- (double)contentDuration;
- (double)contentDurationWithinEndTimes;
- (double)currentTime;
- (double)currentTimeWithinEndTimes;
- (double)minTime;
- (double)maxTime;
- (double)volume;
- (void)setVolume:(double)value;
- (BOOL)isMuted;
- (void)setMuted:(BOOL)value;
- (double)rate;
- (void)setRate:(double)value;
- (double)defaultPlaybackRate;
- (void)setDefaultPlaybackRate:(double)value;
- (BOOL)canPlay;
- (BOOL)canPause;
- (BOOL)canSeek;
- (BOOL)hasContent;
- (BOOL)hasEnabledAudio;
- (BOOL)hasEnabledVideo;
- (BOOL)hasVideo;
- (BOOL)isPlaying;
- (NSInteger)status;
- (id)error;
@end
static void state(id c,const char *name) {
 printf("CONTROLLER case=%s player-nil=%d duration=%.17g bounded-duration=%.17g current=%.17g bounded-current=%.17g min=%.17g max=%.17g volume=%.17g muted=%d rate=%.17g default-rate=%.17g play=%d pause=%d seek=%d content=%d audio=%d video-enabled=%d video=%d playing=%d status=%ld error-nil=%d\n",name,[c player]==nil,
 [c contentDuration],[c contentDurationWithinEndTimes],[c currentTime],
 [c currentTimeWithinEndTimes],[c minTime],[c maxTime],[c volume],[c isMuted],
 [c rate],[c defaultPlaybackRate],[c canPlay],[c canPause],[c canSeek],
 [c hasContent],[c hasEnabledAudio],[c hasEnabledVideo],[c hasVideo],
 [c isPlaying],(long)[c status],[c error]==nil);
}
int main(int argc,char **argv) {
 @autoreleasepool {
  void *kit=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
  Class cls=NSClassFromString(@"AVPlayerController");
  if(!kit||!cls)return 77;
  id c=[[cls alloc] init];if(!c)return 1;
  state(c,"initial");
  [c setVolume:0.25];[c setMuted:YES];[c setDefaultPlaybackRate:1.5];
  state(c,"values-without-player");
  Class player=NSClassFromString(@"AVPlayer");id p=[[player alloc] init];if(!p)return 77;
  [c setPlayer:p];state(c,"empty-player");
  [c setVolume:0.25];[c setMuted:YES];[c setDefaultPlaybackRate:1.5];state(c,"empty-player-values");
  [p setValue:@0.75 forKey:@"volume"];[p setValue:@NO forKey:@"muted"];
  [p setValue:@1.25 forKey:@"defaultRate"];state(c,"external-player-values");
  [c setPlayer:nil];state(c,"detached");
  if(argc==2){
   id filePlayer=[[player alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]];
   if(!filePlayer)return 1;
   [c setPlayer:filePlayer];
   struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
   do {
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    clock_gettime(CLOCK_MONOTONIC,&now);
   }while((![c canPlay]||![c hasEnabledVideo]||!isfinite([c contentDuration]))&&![c error]&&now.tv_sec-start.tv_sec<8);
   state(c,"own-local-file-paused");
   [c setPlayer:nil];[filePlayer release];
  }
  [c release];[p release];return 0;
 }
}
