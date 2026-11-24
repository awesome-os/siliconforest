/* SPDX-License-Identifier: MIT */
/* Our probe, using types observed in read-only runtime metadata. With one
 * argument, inspect and seek only our own local movie while paused. Never
 * start playback, create a window or modify a service. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
@interface NSObject(SFTimeResolverProbe)
- (double)currentTime;
- (double)currentTimeWithinEndTimes;
- (double)remainingTime;
- (double)remainingTimeWithinEndTimes;
- (double)remainingTargetTimeWithinEndTimes;
- (double)targetTime;
- (double)targetTimeWithinEndTimes;
- (double)minTime;
- (double)maxTime;
- (double)interval;
- (double)resolution;
- (double)seekableTimeRangeDuration;
- (BOOL)isCurrentTimeAtEndOfSeekableTimeRanges;
- (id)playerController;
- (void)setPlayerController:(id)value;
- (id)initWithURL:(NSURL *)url;
- (void)setPlayer:(id)value;
- (BOOL)canPlay;
- (BOOL)hasEnabledVideo;
- (double)contentDuration;
- (id)error;
- (void)setCurrentTime:(double)value;
- (void)setCurrentTimeWithinEndTimes:(double)value;
- (void)setTargetTime:(double)value;
- (void)setTargetTimeWithinEndTimes:(double)value;
- (void)setMinTime:(double)value;
- (void)setMaxTime:(double)value;
- (void)setInterval:(double)value;
- (void)setResolution:(double)value;
@end
static void printState(id r,const char *name,unsigned index) {
 printf("TIME case=%s index=%u min=%.17g max=%.17g interval=%.17g resolution=%.17g current=%.17g current-within=%.17g target=%.17g target-within=%.17g remaining=%.17g remaining-within=%.17g target-remaining=%.17g seekable-duration=%.17g at-end=%d controller-nil=%d controller-current=%.17g\n",
  name,index,[r minTime],[r maxTime],[r interval],[r resolution],
  [r currentTime],[r currentTimeWithinEndTimes],[r targetTime],
  [r targetTimeWithinEndTimes],[r remainingTime],[r remainingTimeWithinEndTimes],
  [r remainingTargetTimeWithinEndTimes],[r seekableTimeRangeDuration],
  [r isCurrentTimeAtEndOfSeekableTimeRanges],[r playerController]==nil,[[r playerController] currentTime]);
}
static void pump(double seconds) {
 struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
 double end=now.tv_sec+now.tv_nsec*1e-9+seconds;
 do {
  [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
  clock_gettime(CLOCK_MONOTONIC,&now);
 }while(now.tv_sec+now.tv_nsec*1e-9<end);
}
int main(int argc,char **argv) {
 @autoreleasepool {
  void *kit=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
  Class cls=NSClassFromString(@"AVPlayerControllerTimeResolver");
  if(!kit||!cls)return 77;
  id r=[[cls alloc] init];if(!r)return 1;
  printState(r,"initial",0);
  /* Distinguish an independent target from the current-time fallback. The
   * original equal-current/target vectors alone could not establish that. */
  [r setCurrentTime:12.5];printState(r,"current-before-target",0);
  [r setTargetTime:3.25];printState(r,"distinct-target",0);
  [r setCurrentTime:5.5];printState(r,"current-after-target",0);
  [r setCurrentTimeWithinEndTimes:8.5];printState(r,"distinct-current-within",0);
  [r setTargetTimeWithinEndTimes:7.5];printState(r,"distinct-target-within",0);
  [r release];r=[[cls alloc] init];if(!r)return 1;
  [r setMinTime:10];[r setMaxTime:30];
  printState(r,"bounds",0);
  const double values[]={-12,0,9.9,10,10.25,17.125,29.9,30,50};
  for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++){
   [r setCurrentTime:values[i]];printState(r,"current",i);
   [r setTargetTime:values[i]];printState(r,"target",i);
   [r setCurrentTimeWithinEndTimes:values[i]];printState(r,"current-within",i);
   [r setTargetTimeWithinEndTimes:values[i]];printState(r,"target-within",i);
  }
  [r setInterval:0.5];[r setResolution:0.25];printState(r,"precision",0);
  [r setMinTime:30];[r setMaxTime:10];printState(r,"inverted",0);
  if(argc==2){
   [r release];r=[[cls alloc] init];
   id c=[[NSClassFromString(@"AVPlayerController") alloc] init];
   id p=[[NSClassFromString(@"AVPlayer") alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]];
   if(!r||!c||!p)return 1;
   [c setPlayer:p];
   for(unsigned n=0;n<800&&(![c canPlay]||![c hasEnabledVideo]||!isfinite([c contentDuration]))&&![c error];n++)pump(.01);
   if(![c canPlay]||![c hasEnabledVideo]||!isfinite([c contentDuration]))return 2;
   [r setPlayerController:c];printState(r,"attached-ready",0);
   [r setCurrentTime:.75];pump(.1);printState(r,"attached-current",0);
   [r setTargetTime:1.375];pump(.1);printState(r,"attached-target",0);
   [r setCurrentTimeWithinEndTimes:2.125];pump(.1);printState(r,"attached-current-within",0);
   [r setTargetTimeWithinEndTimes:3.625];pump(.1);printState(r,"attached-target-within",0);
   const double ends[]={-1,2.9,3,3.001,3.49,3.5,3.51,4,4.9,5,5.1,6};
   for(unsigned n=0;n<sizeof(ends)/sizeof(ends[0]);n++){
    [r setCurrentTime:ends[n]];printState(r,"attached-end-boundary",n);
    pump(.1);printState(r,"attached-end-settled",n);
   }
   [r setInterval:.5];printState(r,"attached-interval",0);
   for(unsigned n=0;n<sizeof(ends)/sizeof(ends[0]);n++){
    [r setCurrentTime:ends[n]];printState(r,"attached-half-interval",n);
    pump(.1);printState(r,"attached-half-settled",n);
   }
   [r setResolution:.25];printState(r,"attached-resolution",0);
   [r setPlayerController:nil];printState(r,"detached",0);
   [c setPlayer:nil];[c release];[p release];
  }
  [r release];return 0;
 }
}
