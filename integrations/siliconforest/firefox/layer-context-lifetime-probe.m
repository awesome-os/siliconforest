/* SPDX-License-Identifier: MIT */
#import <AppKit/AppKit.h>
#import <QuartzCore/CALayer.h>
#import <CoreGraphics/CoreGraphics.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <objc/runtime.h>
@interface NSObject(SFFXContext)
-initWithFrame:(CGRect)frame;
-(void)setLayer:(CALayer *)layer;
-(void)render;
-(CFTypeRef)copySFFXRenderedSurface;
-(void)invalidate;
@end
static unsigned roots;
@interface SFFXTrackedLayer:CALayer @end
@implementation SFFXTrackedLayer
-(void)dealloc {roots++;[super dealloc];}
@end
#define LOAD(ret,name,args) ret (*name) args=dlsym(gl,#name);if(!name)return 2;
int main(void){@autoreleasepool{
 setvbuf(stdout,NULL,_IOLBF,0);
 [NSApplication sharedApplication];
 void *gl=dlopen("/System/Library/Frameworks/OpenGL.framework/Versions/A/OpenGL",RTLD_NOW|RTLD_LOCAL);
 void *io=dlopen("/System/Library/Frameworks/IOSurface.framework/IOSurface",RTLD_NOW|RTLD_LOCAL);
 if(!gl||!io)return 2;
 LOAD(int,CGLChoosePixelFormat,(const int *,void **,int *));
 LOAD(int,CGLCreateContext,(void *,void *,void **));
 LOAD(int,CGLSetCurrentContext,(void *));LOAD(void *,CGLGetCurrentContext,(void));
 LOAD(int,CGLDestroyContext,(void *));LOAD(int,CGLDestroyPixelFormat,(void *));
 void (*stats)(uint64_t[2])=dlsym(RTLD_DEFAULT,"SFFXCGLLifetimeStats");
 void *(*base)(CFTypeRef)=dlsym(io,"IOSurfaceGetBaseAddress");
 size_t (*row)(CFTypeRef)=dlsym(io,"IOSurfaceGetBytesPerRow");
 if(!stats||!base||!row)return 2;
 int attributes[]={99,0x3200,73,96,0},number=0;void *format=NULL,*sentinel=NULL;
 if(CGLChoosePixelFormat(attributes,&format,&number)||CGLCreateContext(format,NULL,&sentinel)||CGLSetCurrentContext(sentinel))return 3;
 uint64_t before[2]={0},after[2]={0};stats(before);
 unsigned mismatch=0,preserved=0,expected=12;
 for(unsigned i=0;i<expected;i++){@autoreleasepool{
  id context=[[objc_getClass("CALayerContext") alloc] initWithFrame:CGRectMake(0,0,32,32)];
  SFFXTrackedLayer *layer=[SFFXTrackedLayer new];layer.frame=CGRectMake(0,0,32,32);
  CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();CGFloat rgba[]={0,1,0,1};
  CGColorRef green=CGColorCreate(space,rgba);layer.backgroundColor=green;
  CGColorRelease(green);CGColorSpaceRelease(space);
  [context setLayer:layer];[layer release];[context render];
  CFTypeRef surface=[context copySFFXRenderedSurface];
  if(!surface){fprintf(stderr,"missing real GPU export\n");return 4;}
  const unsigned char *bytes=base(surface);size_t stride=row(surface);
  if(!bytes||stride<128)return 4;
  for(int y=0;y<32;y++)for(int x=0;x<32;x++){
   const unsigned char *pixel=bytes+y*stride+x*4;
   mismatch+=pixel[0]!=0||pixel[1]!=255||pixel[2]!=0||pixel[3]!=255;
  }
  CFRelease(surface);
  if(CGLSetCurrentContext(sentinel))return 3;
  [context invalidate];[context release];
  preserved+=CGLGetCurrentContext()==sentinel;
 }}
 stats(after);
 int pass=roots==expected&&after[0]-before[0]==expected&&after[1]-before[1]==expected&&preserved==expected&&!mismatch;
 printf("FXLAYERLIFETIME expected=%u created=%llu destroyed=%llu roots_destroyed=%u foreign_context_preserved=%u pixel_mismatch=%u pass=%d\n",expected,(unsigned long long)(after[0]-before[0]),(unsigned long long)(after[1]-before[1]),roots,preserved,mismatch,pass);
 CGLSetCurrentContext(NULL);CGLDestroyContext(sentinel);CGLDestroyPixelFormat(format);
 return pass?0:1;
}}
