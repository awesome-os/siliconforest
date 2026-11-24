/* SPDX-License-Identifier: MIT */
/* Own declarations from read-only native method metadata. A value-object
 * probe only: never opens a device, session, view, touch bar or recording. */
#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <stdio.h>
@interface NSObject(SFCaptureItemProbe)
- (id)initWithTitle:(NSString *)title;
- (id)initWithTitle:(NSString *)title builtIn:(BOOL)builtIn noAudioItem:(BOOL)noAudio enabled:(BOOL)enabled;
- (NSString *)title;
- (BOOL)isBuiltIn;
- (BOOL)isNoAudioItem;
- (BOOL)isEnabled;
- (id)representedObject;
- (void)setRepresentedObject:(id)value;
@end
static int freed;
@interface SFRepresented : NSObject @end
@implementation SFRepresented
- (void)dealloc { ++freed;[super dealloc]; }
@end
static id item(Class cls,NSString *title,BOOL builtIn,BOOL noAudio,BOOL enabled) {
 return [[[cls alloc] initWithTitle:title builtIn:builtIn noAudioItem:noAudio enabled:enabled] autorelease];
}
int main(void){
 @autoreleasepool {
  void *image=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
  Class cls=NSClassFromString(@"AVTouchBarCaptureInputSourceItem");
  if(!image||!cls){fputs("CAPTURE_ITEM unavailable\n",stderr);return 77;}
  id a=item(cls,@"A",YES,NO,YES),same=item(cls,@"A",YES,NO,YES);
  if(!a||!same)return 1;
  printf("FULL title=%s built-in=%d no-audio=%d enabled=%d represented-nil=%d\n",[[a title] UTF8String],[a isBuiltIn],[a isNoAudioItem],[a isEnabled],[a representedObject]==nil);
  id simple=[[[cls alloc] initWithTitle:@"simple"] autorelease];
  if(!simple)return 1;
  printf("SIMPLE title=%s built-in=%d no-audio=%d enabled=%d\n",[[simple title] UTF8String],[simple isBuiltIn],[simple isNoAudioItem],[simple isEnabled]);
  id alternatives[]={same,item(cls,@"B",YES,NO,YES),item(cls,@"A",NO,NO,YES),item(cls,@"A",YES,YES,YES),item(cls,@"A",YES,NO,NO),@"A"};
  for(unsigned i=0;i<sizeof(alternatives)/sizeof(alternatives[0]);i++){
   id other=alternatives[i];if(!other)return 1;
   printf("EQUALITY case=%u equal=%d symmetric=%d equal-hash=%d\n",i,[a isEqual:other],[a isEqual:other]==[other isEqual:a],[a hash]==[other hash]);
  }
  NSMutableString *title=[NSMutableString stringWithString:@"before"];
  id mutable=item(cls,title,NO,YES,NO);[title setString:@"after"];
  printf("TITLE after-mutation=%s\n",[[mutable title] UTF8String]);
  @autoreleasepool {
   SFRepresented *model=[SFRepresented new];[a setRepresentedObject:model];[model release];
   printf("REPRESENTED freed-after-release=%d getter-nil=%d equality-unchanged=%d\n",freed,[a representedObject]==nil,[a isEqual:same]);
   [a setRepresentedObject:nil];printf("REPRESENTED freed-after-clear=%d getter-nil=%d\n",freed,[a representedObject]==nil);
  }
  printf("REPRESENTED freed-after-pool=%d\n",freed);
  [a setRepresentedObject:[NSMutableString stringWithString:@"model"]];
  [same setRepresentedObject:[NSMutableString stringWithString:@"model"]];
  printf("MODEL equal-values=%d equal-hash=%d\n",[a isEqual:same],[a hash]==[same hash]);
  [same setRepresentedObject:@"different"];
  printf("MODEL different-values=%d\n",[a isEqual:same]);
  return 0;
 }
}
