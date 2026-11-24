/* SPDX-License-Identifier: MIT */
/* M1-only failure contract. Never invoke the native capture UI on the Mac. */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CGGeometry.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
@interface NSObject(SFUnavailableCaptureProbe)
- (id)initWithFrame:(CGRect)frame;
- (id)initWithCoder:(id)coder;
@end
int main(void){
 const char *mode=getenv("SF_AVKIT_PD_ONLY");
 if(!mode||strcmp(mode,"1")||access("/dev/sfavd",F_OK))return 77;
 @autoreleasepool {
  void *image=dlopen("/System/Library/Frameworks/AVKit.framework/Versions/A/AVKit",RTLD_NOW|RTLD_GLOBAL);
  Class cls=NSClassFromString(@"AVCaptureView");
  if(!image||!cls)return 1;
  id a=[[[cls alloc] init] autorelease];
  id b=[[[cls alloc] initWithFrame:CGRectMake(0,0,80,40)] autorelease];
  id c=[[[cls alloc] initWithCoder:nil] autorelease];
  printf("CAPTURE_UNAVAILABLE class=1 default-nil=%d frame-nil=%d coder-nil=%d\n",a==nil,b==nil,c==nil);
  return a||b||c;
 }
}
