/* SPDX-License-Identifier: MIT */
/* Our runtime-derived declarations. Build on Linux, inspect our own paused
 * fixture and menu objects only. No menu is displayed and no input is sent. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
#include <math.h>
#include <time.h>
@interface NSObject(SFMenuProbe)
- (id)initWithTitle:(NSString *)title;
- (id)initWithURL:(NSURL *)url;
- (void)setPlayer:(id)player;
- (BOOL)canPlay;
- (BOOL)hasEnabledAudio;
- (BOOL)hasEnabledVideo;
- (NSArray *)audioMediaSelectionOptions;
- (NSArray *)legibleMediaSelectionOptions;
- (void)startMediaSelectionInspection;
- (void)startInspectionIfNeeded;
- (NSArray *)availableMediaCharacteristicsWithMediaSelectionOptions;
- (id)mediaSelectionGroupForMediaCharacteristic:(id)characteristic;
- (NSArray *)options;
- (NSArray *)audioOptionsForAsset:(id)asset;
- (NSArray *)_playableLegibleMediaSelectionOptionsForAsset:(id)asset;
- (id)currentAssetForAudioAndLegibleOptions;
- (double)contentDuration;
- (id)error;
- (BOOL)hasAudioMediaSelectionOptions;
- (BOOL)hasLegibleMediaSelectionOptions;
- (NSArray *)itemArray;
- (NSString *)title;
- (NSString *)keyEquivalent;
- (id)target;
- (SEL)action;
- (id)representedObject;
- (BOOL)isEnabled;
- (BOOL)isSeparatorItem;
- (NSInteger)state;
- (NSInteger)tag;
- (NSString *)displayName;
@end
@interface SFMenuProbeTarget:NSObject
@end
@implementation SFMenuProbeTarget
- (void)chooseOwnMedia:(id)sender { (void)sender; }
@end
typedef void (*SFAddMediaMenu)(id,id,id,SEL);
static int inspect(void *kit,id controller,const char *symbol,const char *phase) {
 SFAddMediaMenu add=(SFAddMediaMenu)dlsym(kit,symbol);if(!add)return 77;
 id menu=[[NSClassFromString(@"NSMenu") alloc] initWithTitle:@"Our non-displayed media menu"];
 id target=[[SFMenuProbeTarget alloc] init];if(!menu||!target)return 1;
 SEL action=@selector(chooseOwnMedia:);
 @try {
  add(menu,controller,target,action);
  NSArray *items=[menu itemArray];
  printf("MENU phase=%s function=%s count=%lu audio=%d legible=%d\n",phase,symbol,(unsigned long)items.count,[controller hasAudioMediaSelectionOptions],[controller hasLegibleMediaSelectionOptions]);
  unsigned index=0;
  for(id item in items){
   id represented=[item representedObject];
   NSString *display=[represented respondsToSelector:@selector(displayName)]?[represented displayName]:nil;
   NSDictionary *row=@{@"index":@(index++),@"title":[item title]?:@"",@"key":[item keyEquivalent]?:@"",
    @"state":@([item state]),@"tag":@([item tag]),@"enabled":@([item isEnabled]),
    @"separator":@([item isSeparatorItem]),@"target-is-ours":@([item target]==target),
    @"action-is-ours":@([item action]==action),@"represented-nil":@(represented==nil),
    @"represented-class":represented?NSStringFromClass([represented class]):@"",@"display":display?:@""};
   NSData *json=[NSJSONSerialization dataWithJSONObject:row options:NSJSONWritingSortedKeys error:NULL];
   if(!json)return 2;
   printf("MENU_ITEM %.*s\n",(int)json.length,(const char *)json.bytes);
  }
 }@catch(NSException *exception){
  fprintf(stderr,"MENU_EXCEPTION %s %s\n",exception.name.UTF8String,exception.reason.UTF8String);
  [menu release];[target release];return 3;
 }
 [menu release];[target release];return 0;
}
int main(int argc,char **argv){
 @autoreleasepool {
  void *kit=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
  Class cls=NSClassFromString(@"AVPlayerController");if(!kit||!cls)return 77;
  id c=[[cls alloc] init];if(!c)return 1;
  int result=inspect(kit,c,"AVAddAudibleMediaSelectionOptionsToMenu","no-player");
  if(!result)result=inspect(kit,c,"AVAddLegibleMediaSelectionOptionsToMenu","no-player");
  if(!result&&argc==2){
   id p=[[NSClassFromString(@"AVPlayer") alloc] initWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]]];
   if(!p)return 1;[c setPlayer:p];[c startInspectionIfNeeded];
   struct timespec start,now;clock_gettime(CLOCK_MONOTONIC,&start);
   do {
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    clock_gettime(CLOCK_MONOTONIC,&now);
   }while((![c canPlay]||![c hasEnabledVideo]||![c hasEnabledAudio]||!isfinite([c contentDuration]))&&![c error]&&now.tv_sec-start.tv_sec<8);
   if(![c canPlay]||![c hasEnabledVideo]||![c hasEnabledAudio]||!isfinite([c contentDuration]))return 2;
   /* Track inspection can precede media-selection inspection. Preserve the
    * original early snapshot; this second probe waits independently. */
   [c startMediaSelectionInspection];
   clock_gettime(CLOCK_MONOTONIC,&start);
   do {
    [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
    clock_gettime(CLOCK_MONOTONIC,&now);
   }while((![c hasAudioMediaSelectionOptions]||![c hasLegibleMediaSelectionOptions])&&![c error]&&now.tv_sec-start.tv_sec<3);
   printf("MENU_READY duration=%.17g audio-options=%lu legible-options=%lu enabled-audio=%d enabled-video=%d\n",[c contentDuration],(unsigned long)[[c audioMediaSelectionOptions] count],(unsigned long)[[c legibleMediaSelectionOptions] count],[c hasEnabledAudio],[c hasEnabledVideo]);
   id asset=[[p valueForKey:@"currentItem"] valueForKey:@"asset"];
   printf("MENU_ASSET controller-asset-is-own=%d characteristics=%lu computed-audio=%lu computed-legible=%lu\n",[c currentAssetForAudioAndLegibleOptions]==asset,(unsigned long)[[asset availableMediaCharacteristicsWithMediaSelectionOptions] count],(unsigned long)[[c audioOptionsForAsset:asset] count],(unsigned long)[[c _playableLegibleMediaSelectionOptionsForAsset:asset] count]);
   for(id characteristic in [asset availableMediaCharacteristicsWithMediaSelectionOptions])
    printf("MENU_GROUP characteristic=%s options=%lu\n",[characteristic UTF8String],(unsigned long)[[[asset mediaSelectionGroupForMediaCharacteristic:characteristic] options] count]);
   result=inspect(kit,c,"AVAddAudibleMediaSelectionOptionsToMenu","own-file-paused");
   if(!result)result=inspect(kit,c,"AVAddLegibleMediaSelectionOptionsToMenu","own-file-paused");
   [c setPlayer:nil];[p release];
  }
  [c release];return result;
 }
}
