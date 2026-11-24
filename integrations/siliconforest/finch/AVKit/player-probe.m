/* SPDX-License-Identifier: MIT */
/* Own M1-only public-player lifecycle and CPU view/transport drawing test. */
#import <AVKit/AVKit.h>
#import <AppKit/AppKit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <time.h>
#include <unistd.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
static void need(BOOL condition,const char *message){if(!condition){fprintf(stderr,"AVKIT_PLAYER_FAIL %s\n",message);exit(1);}}
static void pump(double interval){double end=seconds()+interval;while(seconds()<end)[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.005]];}
int main(int argc,char **argv){setvbuf(stdout,NULL,_IOLBF,0);@autoreleasepool{
 if(access("/dev/sfavd",F_OK)!=0){fprintf(stderr,"M1-only output probe; no controller audio or UI\n");return 77;}
 need(argc==2,"usage: avkit-player-probe generated.mov");
 NSString *missingPath=[[NSString stringWithUTF8String:argv[1]] stringByAppendingString:@".missing"];
 AVPlayer *missing=[[AVPlayer alloc] initWithURL:[NSURL fileURLWithPath:missingPath]];
 double missingDeadline=seconds()+5;while(missing.currentItem.status==AVPlayerItemStatusUnknown&&seconds()<missingDeadline)pump(.01);
 [missing play];need(missing.currentItem.status==AVPlayerItemStatusFailed&&missing.currentItem.error&&missing.rate==0&&!missing.currentItem.canPlayReverse&&!missing.currentItem.canPlayFastForward,"missing file fails without simulated playback capabilities");
 AVPlayerController *failedController=[[AVPlayerController alloc] init];failedController.player=missing;
 [failedController play:nil];[failedController seekToTime:1];
 need(!failedController.canPlay&&!failedController.canSeek&&!failedController.isPlaying&&failedController.error!=nil,"controller does not report failed file as playable");
 failedController.player=nil;[failedController release];
 printf("AVKIT_UNAVAILABLE missing-file=failed rate=0 reverse=0 fast-forward=0 controller-play=0 seek=0\n");[missing release];

 AVPlayer *player=[[AVPlayer alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]];
 AVPlayerController *controller=[[AVPlayerController alloc] init];controller.player=player;
 AVPlayerView *view=[[AVPlayerView alloc] initWithFrame:NSMakeRect(0,0,320,218)];view.player=player;
 double deadline=seconds()+15;while(player.currentItem.status==AVPlayerItemStatusUnknown&&seconds()<deadline)pump(.01);
 if(player.currentItem.error)fprintf(stderr,"item-error: %s\n",[[player.currentItem.error description] UTF8String]);
 need(player.currentItem.status==AVPlayerItemStatusReadyToPlay,"real media prepared");
 NSDictionary *prepared=[player sfPlaybackStatistics];printf("AVKIT_PREPARED %s\n",[[prepared description] UTF8String]);
 need([prepared[@"videoFrames"] unsignedIntegerValue]>1&&[prepared[@"videoFrames"] isEqual:prepared[@"videoSamples"]]&&[prepared[@"hardware"] boolValue],"hardware pictures decoded");
 need([prepared[@"audioFrames"] unsignedIntegerValue]>0,"audio PCM decoded");
 NSArray *digests=[player sfPlaybackFrameDigests];for(NSUInteger n=0;n<digests.count;n++)printf("AVKIT_FRAME index=%lu pts=%.9f md5=%s\n",(unsigned long)n,[digests[n][@"pts"] doubleValue],[digests[n][@"md5"] UTF8String]);
 need(controller.hasContent&&controller.canPlay&&controller.canSeek&&controller.hasEnabledVideo&&controller.hasEnabledAudio,"controller reads actual prepared tracks");
 need(fabs(controller.contentDuration-CMTimeGetSeconds(player.currentItem.duration))<.001,"controller reads actual duration");
 controller.volume=.05;[controller play:nil];deadline=seconds()+5;while(CMTimeGetSeconds(player.currentTime)<.45&&seconds()<deadline)pump(.01);
 need(player.rate==1&&CMTimeGetSeconds(player.currentTime)>=.45,"queue-clock playback starts");
 need(controller.isPlaying&&controller.canPause,"controller exposes active transport");
 [controller pause:nil];double paused=CMTimeGetSeconds(player.currentTime);pump(.15);double held=CMTimeGetSeconds(player.currentTime);
 need(fabs(paused-held)<.025&&player.rate==0,"pause holds queue clock");
 [controller seekToTime:.2];pump(.05);
 need(!controller.isPlaying&&fabs(controller.currentTime-.2)<.01,"controller paused seek reports actual position");
 printf("AVKIT_LIFECYCLE paused=%.6f held=%.6f seek=%.6f\n",paused,held,CMTimeGetSeconds(player.currentTime));
 const size_t width=320,height=218;uint8_t *pixels=calloc(width*height,4);CGColorSpaceRef color=CGColorSpaceCreateDeviceRGB();
 CGContextRef bitmap=CGBitmapContextCreate(pixels,width,height,8,width*4,color,kCGImageAlphaPremultipliedLast|kCGBitmapByteOrderDefault);need(bitmap!=NULL,"CPU drawing context");
 NSGraphicsContext *graphics=[NSGraphicsContext graphicsContextWithGraphicsPort:bitmap flipped:NO];
 [NSGraphicsContext saveGraphicsState];[NSGraphicsContext setCurrentContext:graphics];
 [[NSColor blackColor] set];NSRectFill(NSMakeRect(0,0,width,height));uint8_t *blank=malloc(width*height*4);need(blank!=NULL,"blank reference allocation");memcpy(blank,pixels,width*height*4);
 [controller togglePlayback:nil];deadline=seconds()+CMTimeGetSeconds(player.currentItem.duration)+8;
 while(player.rate!=0&&seconds()<deadline){pump(.015);[view drawRect:view.bounds];NSView *transport=view.subviews.lastObject;[transport drawRect:transport.bounds];}
 [view drawRect:view.bounds];NSView *transport=view.subviews.lastObject;[transport drawRect:transport.bounds];
 [NSGraphicsContext restoreGraphicsState];
 NSDictionary *final=[player sfPlaybackStatistics];double duration=CMTimeGetSeconds(player.currentItem.duration);
 size_t colored=0;for(size_t n=0;n<width*height;n++)if(pixels[n*4]>10||pixels[n*4+1]>10||pixels[n*4+2]>10)colored++;
 size_t changedVideo=0;for(size_t y=height*3/10;y<height*7/10;y++)for(size_t x=0;x<width;x++)if(memcmp(pixels+(y*width+x)*4,blank+(y*width+x)*4,4))changedVideo++;
 printf("AVKIT_VIDEO_PIXELS changed_center=%zu\n",changedVideo);
 printf("AVKIT_END time=%.6f duration=%.6f layerReady=%d frameIndex=%lu videoDraws=%lu controlDraws=%lu colored=%zu stats=%s\n",CMTimeGetSeconds(player.currentTime),duration,view.isReadyForDisplay,(unsigned long)[view.sfPlayerLayer sfDisplayedFrameIndex],(unsigned long)view.sfVideoDrawCount,(unsigned long)view.sfTransportDrawCount,colored,[[final description] UTF8String]);
 need(player.rate==0&&fabs(CMTimeGetSeconds(player.currentTime)-duration)<.01,"real item reaches end");
 need(view.isReadyForDisplay&&view.sfVideoDrawCount>1&&view.sfTransportDrawCount>1&&changedVideo>1000,"actual layer pixels and drawn transport");
 need([final[@"error"] intValue]==0&&[final[@"callbacks"] unsignedIntegerValue]>0&&[final[@"audioQueued"] isEqual:final[@"audioFrames"]],"retained queue accepts all remaining audio");
 need([final[@"deviceRate"] doubleValue]>0,"actual device sample rate");
 need([final[@"audioReturned"] unsignedIntegerValue]+(NSUInteger)llround([final[@"clientRate"] doubleValue]/5)==[final[@"audioFrames"] unsignedIntegerValue],"every post-seek source buffer returned");
 need(!controller.isPlaying&&controller.canPause,"controller is stopped and still permits idempotent pause");
 controller.player=nil;need(!controller.hasContent&&!controller.canPlay&&isnan(controller.currentTime),"detached controller advertises no media");
 [controller release];view.player=nil;[view release];[player release];CGContextRelease(bitmap);CGColorSpaceRelease(color);free(pixels);free(blank);
 printf("AVKIT_CONTROLLER_PASS duration/tracks/play/pause/seek/toggle/end/detach over real player\n");
 printf("AVKIT_PLAYER_PASS headless CPU draw only; no window/panel/acoustic claim\n");return 0;
}}
