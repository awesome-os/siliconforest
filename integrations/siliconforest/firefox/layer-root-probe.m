/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <objc/runtime.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
@interface NSApplication : NSObject
+(id)sharedApplication;
@end
@interface NSView : NSObject
-(id)initWithFrame:(NSRect)frame;
-(void)setLayer:(id)layer;
@end
@interface CALayer : NSObject
+(id)layer;
@property CGRect frame;
@property CGColorRef backgroundColor;
@end
@interface CALayerContext : NSObject
-(void)render;
-(CGImageRef)createImage;
@end
extern const unsigned char *glGetString(unsigned);
static int pixel(CALayerContext *context,int blue){
 [context render];CGImageRef image=[context createImage];if(!image)return 0;
 unsigned char pixels[32*32*4]={0};
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
 CGContextRef out=CGBitmapContextCreate(pixels,32,32,8,32*4,space,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
 CGColorSpaceRelease(space);
 if(!out){CGImageRelease(image);return 0;}
 CGContextDrawImage(out,CGRectMake(0,0,32,32),image);
 unsigned char *p=pixels+(16*32+16)*4;
 int good=blue?(p[0]==255&&p[1]==0&&p[2]==0&&p[3]==255):(p[0]==0&&p[1]==255&&p[2]==0&&p[3]==255);
 printf("FXROOT expected=%s actualBGRA=%u,%u,%u,%u passed=%d\n",blue?"blue":"green",p[0],p[1],p[2],p[3],good);
 CGContextRelease(out);CGImageRelease(image);return good;
}
int main(void){@autoreleasepool{
 [NSApplication sharedApplication];
 NSView *view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,32,32)];
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
 CALayer *layers[2];
 for(int i=0;i<2;i++){
  layers[i]=[CALayer layer];layers[i].frame=CGRectMake(0,0,32,32);
  CGFloat components[]={0,i?0:1,i?1:0,1};CGColorRef color=CGColorCreate(space,components);
  layers[i].backgroundColor=color;CGColorRelease(color);
 }
 CGColorSpaceRelease(space);[view setLayer:layers[0]];
 Ivar field=class_getInstanceVariable([NSView class],"_layerContext");
 CALayerContext *context=field?object_getIvar(view,field):nil;
 if(!context){puts("FXROOT no real NSView layer context");return 2;}
 int initial=pixel(context,0);[view setLayer:layers[1]];int replaced=pixel(context,1);
 Dl_info info={0};dladdr((void *)class_getMethodImplementation([NSView class],@selector(setLayer:)),&info);
 const char *renderer=(const char *)glGetString(0x1f01);
 printf("FXROOT implementation=%s renderer=%s initial=%d replaced=%d\n",info.dli_fname?info.dli_fname:"unknown",renderer?renderer:"null",initial,replaced);
 [view release];return initial&&replaced&&renderer&&strstr(renderer,"Apple M1")?0:1;
}}
