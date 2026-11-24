/* SPDX-License-Identifier: MIT */
#import "MediaPlayer.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
static int targetFreed;
@interface SFMPTarget : NSObject
- (NSInteger)command:(id)event;
@end
@implementation SFMPTarget
- (NSInteger)command:(id)event { return 200; }
- (void)dealloc { ++targetFreed; [super dealloc]; }
@end
int main(int argc,char **argv) {
 @autoreleasepool {
  if(argc==2 && !strcmp(argv[1],"--metadata")) {
   /* The native reference is read-only: never set Now Playing state or
    * register with the controller Mac's real command service. */
   NSString *keys[]={MPMediaItemPropertyTitle,MPMediaItemPropertyAlbumTitle,MPMediaItemPropertyArtist,
    MPMediaItemPropertyArtwork,MPMediaItemPropertyPlaybackDuration,MPMediaItemPropertyAlbumTrackNumber,
    MPNowPlayingInfoPropertyElapsedPlaybackTime,MPNowPlayingInfoPropertyIsLiveStream,
    MPNowPlayingInfoPropertyPlaybackProgress,MPNowPlayingInfoPropertyPlaybackRate};
   for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i) {
    if(!keys[i])return 1;
    printf("KEY %u %s\n",i,[keys[i] UTF8String]);
   }
   NSString *classes[] = {@"MPNowPlayingInfoCenter",@"MPRemoteCommandCenter",@"MPRemoteCommand",
    @"MPSkipIntervalCommand",@"MPChangePlaybackPositionCommand",@"MPMediaItemArtwork"};
   for(unsigned i=0;i<sizeof(classes)/sizeof(classes[0]);++i) {
    BOOL found=NSClassFromString(classes[i])!=Nil;
    printf("CLASS %s present=%d\n",[classes[i] UTF8String],found);
    if(!found)return 1;
   }
   return 0;
  }
  MPNowPlayingInfoCenter *info=[MPNowPlayingInfoCenter defaultCenter];
  NSMutableDictionary *input=[NSMutableDictionary dictionaryWithObject:@"original" forKey:MPMediaItemPropertyTitle];
  [info setNowPlayingInfo:input];[input setObject:@"changed" forKey:MPMediaItemPropertyTitle];
  BOOL copied=[[[info nowPlayingInfo] objectForKey:MPMediaItemPropertyTitle] isEqual:@"original"];
  [info setPlaybackState:1];BOOL state=[info playbackState]==1;
  printf("INFO singleton=%d copy=%d state=%d constants=%d\n",info==[MPNowPlayingInfoCenter defaultCenter],copied,state,
   MPMediaItemPropertyTitle&&MPMediaItemPropertyArtwork&&MPNowPlayingInfoPropertyElapsedPlaybackTime&&MPNowPlayingInfoPropertyPlaybackRate);
  [info setNowPlayingInfo:nil];BOOL cleared=[info nowPlayingInfo]==nil;
  MPRemoteCommandCenter *center=[MPRemoteCommandCenter sharedCommandCenter];MPRemoteCommand *play=[center playCommand];
  BOOL stable=play==[center playCommand]&&center==[MPRemoteCommandCenter sharedCommandCenter];
  BOOL initiallyEnabled=[play isEnabled];[play setEnabled:NO];BOOL disabled=![play isEnabled];
  SFMPTarget *target=[SFMPTarget new];[play addTarget:target action:@selector(command:)];
  [play addTarget:target action:@selector(command:)];[play removeTarget:target action:@selector(command:)];[target release];
  __block int events=0;id token=[play addTargetWithHandler:^NSInteger(id event){++events;return 200;}];
  BOOL registered=token!=nil;[play removeTarget:token];[play removeTarget:nil];usleep(20000);
  [center skipForwardCommand].preferredIntervals=@[@15,@30];
  BOOL intervals=[[[center skipForwardCommand] preferredIntervals] isEqual:@[@15,@30]];
  __block int artworkCalls=0;
  MPMediaItemArtwork *art=[[[MPMediaItemArtwork alloc] initWithBoundsSize:CGSizeMake(20,10) requestHandler:^id(CGSize size){++artworkCalls;return @"local-artwork";}] autorelease];
  BOOL artwork=[[art imageWithSize:CGSizeMake(8,4)] isEqual:@"local-artwork"]&&artworkCalls==1&&[art bounds].size.width==20;
  printf("LOCAL commands-stable=%d initially-enabled=%d disable=%d target-freed=%d token=%d no-events=%d intervals=%d artwork=%d clear=%d\n",stable,initiallyEnabled,disabled,targetFreed,registered,events==0,intervals,artwork,cleared);
  return !(copied&&state&&cleared&&stable&&disabled&&targetFreed==1&&registered&&events==0&&intervals&&artwork);
 }
}
