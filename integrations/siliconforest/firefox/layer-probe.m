/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <stdio.h>
#include <dlfcn.h>
#import <objc/runtime.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
// SiliconForest public/private drawing ABIs, declared without an Apple SDK.
@interface CALayer : NSObject
+(id)layer;
@property CGRect frame;
@property(retain) id contents;
@property CGColorRef backgroundColor;
@property(copy) NSString *minificationFilter;
@property(copy) NSString *magnificationFilter;
-(void)addSublayer:(CALayer *)layer;
@end
@interface CALayerContext : NSObject
-(id)initWithFrame:(CGRect)frame;
-(void)setLayer:(CALayer *)layer;
-(void)setBackingScale:(CGFloat)value;
-(void)render;
-(CGImageRef)createImage;
-(void)setDelegate:(id)delegate;
@end
@interface CATransaction : NSObject
+(void)begin;
+(void)commit;
@end
@interface FXLayerDelegate : NSObject { @public unsigned requests; }
@end
@implementation FXLayerDelegate
-(void)layerContextNeedsDisplay:(id)context { requests++; }
@end
extern IOSurfaceRef IOSurfaceCreate(CFDictionaryRef);
extern void *IOSurfaceGetBaseAddress(IOSurfaceRef);
extern size_t IOSurfaceGetBytesPerRow(IOSurfaceRef);
extern int IOSurfaceLock(IOSurfaceRef,unsigned,uint32_t *);
extern int IOSurfaceUnlock(IOSurfaceRef,unsigned,uint32_t *);
extern const unsigned char *glGetString(unsigned);
static void pump(double seconds){
 NSDate *end=[NSDate dateWithTimeIntervalSinceNow:seconds];
 while([end timeIntervalSinceNow]>0)
  [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:end];
}
static int transactionCheck(CALayerContext *context,CALayer *root,IOSurfaceRef surface){
 FXLayerDelegate *delegate=[[[FXLayerDelegate alloc] init] autorelease];
 [context setDelegate:delegate];[context render];pump(.03);delegate->requests=0;
 for(int i=0;i<40;i++){[CATransaction begin];[CATransaction commit];pump(.002);}
 unsigned empty=delegate->requests;delegate->requests=0;
 IOSurfaceLock(surface,0,NULL);
 unsigned char *bytes=IOSurfaceGetBaseAddress(surface);
 bytes[0]^=255;bytes[1]^=255;bytes[2]^=255;
 IOSurfaceUnlock(surface,0,NULL);
 [CATransaction begin];[CATransaction commit];pump(.02);
 unsigned changed=delegate->requests;
 [context render];pump(.02);delegate->requests=0;
 CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();CGFloat rgba[]={0,0,1,1};
 CGColorRef color=CGColorCreate(space,rgba);root.backgroundColor=color;
 CGColorRelease(color);CGColorSpaceRelease(space);pump(.02);
 unsigned property=delegate->requests;[context setDelegate:nil];
 printf("FXTRANSACTION empty=%u changed_surface=%u changed_property=%u\n",empty,changed,property);
 return empty==0&&changed>0&&property>0;
}
int main(int argc,char **argv) { @autoreleasepool {
    CGFloat scale=argc>1?atof(argv[1]):1;
    if(!(scale>0)||scale>4)return 2;
    int side=(int)lround(32*scale),half=side/2;
    Dl_info implementation={0};
    dladdr((void *)class_getMethodImplementation(objc_getClass("CALayerContext"),sel_registerName("render")),&implementation);
    printf("FXLAYER implementation=%s\n",implementation.dli_fname?implementation.dli_fname:"unknown");
    CALayer *root=[CALayer layer];root.frame=CGRectMake(0,0,32,32);
    CALayer *green=[CALayer layer];green.frame=CGRectMake(0,0,16,32);
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();CGFloat components[]={0,1,0,1};
    CGColorRef color=CGColorCreate(space,components);green.backgroundColor=color;
    CGColorRelease(color);CGColorSpaceRelease(space);[root addSublayer:green];
    CFMutableDictionaryRef props=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
    const char *names[]={"IOSurfaceWidth","IOSurfaceHeight","IOSurfaceBytesPerElement","IOSurfacePixelFormat"};
    int values[]={half,side,4,0x42475241};
    for(int i=0;i<4;i++){
        CFStringRef key=CFStringCreateWithCString(NULL,names[i],kCFStringEncodingUTF8);
        CFNumberRef number=CFNumberCreate(NULL,kCFNumberIntType,&values[i]);
        CFDictionarySetValue(props,key,number);CFRelease(number);CFRelease(key);
    }
    IOSurfaceRef surface=IOSurfaceCreate(props);CFRelease(props);if(!surface)return 2;
    unsigned char *pixels=IOSurfaceGetBaseAddress(surface);size_t row=IOSurfaceGetBytesPerRow(surface);
    for(int y=0;y<side;y++)for(int x=0;x<half;x++){
        unsigned char *p=pixels+y*row+x*4;unsigned char c=((x+y)&1)?255:0;
        p[0]=p[1]=p[2]=c;p[3]=255;
    }
    CALayer *checker=[CALayer layer];checker.frame=CGRectMake(16,0,16,32);
    checker.contents=(id)surface;checker.minificationFilter=@"nearest";checker.magnificationFilter=@"nearest";
    [root addSublayer:checker];
    CALayerContext *context=[[CALayerContext alloc] initWithFrame:CGRectMake(0,0,32,32)];
    if([context respondsToSelector:@selector(setBackingScale:)])[context setBackingScale:scale];
    [context setLayer:root];[context render];CGImageRef image=[context createImage];if(!image)return 2;
    unsigned char *storage=calloc((size_t)side*side,4);if(!storage)return 2;
    space=CGColorSpaceCreateDeviceRGB();
    CGContextRef output=CGBitmapContextCreate(storage,side,side,8,side*4,space,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
    CGColorSpaceRelease(space);if(!output)return 2;
    CGContextSetInterpolationQuality(output,kCGInterpolationNone);
    CGContextDrawImage(output,CGRectMake(0,0,side,side),image);
    const unsigned char *out=storage;size_t outrow=side*4;
    unsigned goodgreen=0,white=0,black=0,other=0,phase0=0,phase1=0;
    for(int y=0;y<side;y++)for(int x=0;x<side;x++){
        const unsigned char *p=out+y*outrow+x*4;
        if(x<half && p[0]==0 && p[1]==255 && p[2]==0 && p[3]==255)goodgreen++;
        else if(x>=half && p[0]==255 && p[1]==255 && p[2]==255 && p[3]==255)white++;
        else if(x>=half && p[0]==0 && p[1]==0 && p[2]==0 && p[3]==255)black++;
        else other++;
        if(x>=half){
            int actual=p[0]==255&&p[1]==255&&p[2]==255&&p[3]==255;
            int expected=(x-half+y)&1;
            phase0+=(actual!=expected);phase1+=(actual==expected);
        }
    }
    const char *renderer=(const char *)glGetString(0x1f01);
    unsigned mismatches=phase0<phase1?phase0:phase1;
    printf("FXLAYER renderer=%s native_pixels=%zu output=%d scale=%g green=%u white=%u black=%u other=%u pattern_mismatches=%u\n",renderer?renderer:"null",CGImageGetWidth(image),side,scale,goodgreen,white,black,other,mismatches);
    int transactions=argc>2&&!strcmp(argv[2],"transactions")?transactionCheck(context,root,surface):1;
    CGContextRelease(output);free(storage);CGImageRelease(image);[context release];CFRelease(surface);
    return goodgreen==(unsigned)(half*side) && white==(unsigned)(half*side/2) && black==(unsigned)(half*side/2) && other==0 && mismatches==0 && renderer && strstr(renderer,"Apple M1") && transactions?0:1;
} }
