/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <dispatch/dispatch.h>
#include <pthread.h>
@interface NSObject (FXAVProbe)
+(NSArray *)speechVoices;
+(NSArray *)devices;
+(NSInteger)authorizationStatusForMediaType:(NSString *)type;
+(void)requestAccessForMediaType:(NSString *)type completionHandler:(void (^)(BOOL))handler;
-(BOOL)isSpeaking;
+(id)URLAssetWithURL:(NSURL *)url options:(NSDictionary *)options;
-(NSArray *)tracks;
-(CMTime)duration;
-(void)setFrame:(CGRect)frame;
-(void)enqueueSampleBuffer:(CMSampleBufferRef)sample;
-(NSInteger)status;
@end
@interface CALayerContext : NSObject
-(id)initWithFrame:(CGRect)frame;
-(void)setLayer:(id)layer;
-(void)render;
-(CGImageRef)createImage;
@end
extern const unsigned char *glGetString(unsigned);
int main(int argc,char **argv){@autoreleasepool{
 if(argc!=2)return 2;
 Class voice=NSClassFromString(@"AVSpeechSynthesisVoice"),speech=NSClassFromString(@"AVSpeechSynthesizer");
 id synth=[[[speech alloc] init] autorelease];
 int noVoices=voice&&speech&&[[voice speechVoices] count]==0&&![synth isSpeaking];
 Class capture=NSClassFromString(@"AVCaptureDevice");
 int noDevices=capture&&[[capture devices] count]==0;
 int restricted=[capture authorizationStatusForMediaType:@"vide"]==1&&
                [capture authorizationStatusForMediaType:@"soun"]==1;
 dispatch_semaphore_t completion=dispatch_semaphore_create(0);
 pthread_t caller=pthread_self();__block int calls=0;__block BOOL granted=YES,sameThread=YES;
 [capture requestAccessForMediaType:@"vide" completionHandler:^(BOOL value){
   calls++;granted=value;sameThread=pthread_equal(caller,pthread_self());
   dispatch_semaphore_signal(completion);
 }];
 long waited=dispatch_semaphore_wait(completion,dispatch_time(DISPATCH_TIME_NOW,2*NSEC_PER_SEC));
 int deniedAsync=waited==0&&calls==1&&!granted&&!sameThread;
 printf("FXAV capture_class=%d no_devices=%d restricted=%d denied_async_once=%d\n",capture!=Nil,noDevices,restricted,deniedAsync);
 dispatch_release(completion);
 Class assetClass=NSClassFromString(@"AVURLAsset");
 id asset=[assetClass URLAssetWithURL:[NSURL fileURLWithPath:[NSString stringWithUTF8String:argv[1]]] options:nil];
 CMTime duration=[asset duration];NSUInteger tracks=[[asset tracks] count];
 double seconds=duration.timescale?(double)duration.value/duration.timescale:NAN;
 int realAsset=asset&&tracks==2&&isfinite(seconds)&&fabs(seconds-10)<.05;
 printf("FXAV no_voices=%d asset_tracks=%lu asset_seconds=%.6f real_asset=%d\n",noVoices,(unsigned long)tracks,seconds,realAsset);
 CFMutableDictionaryRef attributes=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFDictionaryRef properties=CFDictionaryCreate(NULL,NULL,NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 CFDictionarySetValue(attributes,kCVPixelBufferIOSurfacePropertiesKey,properties);CFRelease(properties);
 CVPixelBufferRef pixel=NULL;
 int status=CVPixelBufferCreate(NULL,32,32,kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,attributes,&pixel);CFRelease(attributes);
 if(status||!pixel)return 3;
 CVPixelBufferLockBaseAddress(pixel,0);
 for(size_t plane=0;plane<2;plane++){
  unsigned char *p=CVPixelBufferGetBaseAddressOfPlane(pixel,plane);size_t row=CVPixelBufferGetBytesPerRowOfPlane(pixel,plane);
  for(size_t y=0;y<CVPixelBufferGetHeightOfPlane(pixel,plane);y++)
   for(size_t x=0;x<32;x++)p[y*row+x]=plane?((x&1)?240:90):81;
 }
 CVPixelBufferUnlockBaseAddress(pixel,0);
 CMVideoFormatDescriptionRef format=NULL;CMSampleBufferRef sample=NULL;
 CMSampleTimingInfo timing={{1,30,1,0},{0,30,1,0},{0,0,0,0}};
 status=CMVideoFormatDescriptionCreateForImageBuffer(NULL,pixel,&format);
 if(!status)status=CMSampleBufferCreateReadyWithImageBuffer(NULL,pixel,format,&timing,&sample);
 if(status||!sample)return 4;
 id layer=[[[NSClassFromString(@"AVSampleBufferDisplayLayer") alloc] init] autorelease];
 [layer setFrame:CGRectMake(0,0,32,32)];[layer enqueueSampleBuffer:sample];
 CALayerContext *context=[[CALayerContext alloc] initWithFrame:CGRectMake(0,0,32,32)];[context setLayer:layer];[context render];
 CGImageRef image=[context createImage];if(!image)return 5;
 unsigned char storage[32*32*4]={0};CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
 CGContextRef output=CGBitmapContextCreate(storage,32,32,8,128,space,kCGImageAlphaPremultipliedFirst|kCGBitmapByteOrder32Little);
 CGContextDrawImage(output,CGRectMake(0,0,32,32),image);
 unsigned red=0;for(int i=0;i<1024;i++)if(storage[i*4]<=2&&storage[i*4+1]<=2&&storage[i*4+2]>=253&&storage[i*4+3]==255)red++;
 const char *renderer=(const char *)glGetString(0x1f01);
 printf("FXAV layer_status=%ld NV12_red_pixels=%u/1024 renderer=%s\n",(long)[layer status],red,renderer?renderer:"null");
 CGContextRelease(output);CGColorSpaceRelease(space);CGImageRelease(image);[context release];CFRelease(sample);CFRelease(format);CFRelease(pixel);
 return noDevices&&restricted&&deniedAsync&&noVoices&&realAsset&&red==1024&&renderer&&strstr(renderer,"Apple M1")?0:1;
}}
