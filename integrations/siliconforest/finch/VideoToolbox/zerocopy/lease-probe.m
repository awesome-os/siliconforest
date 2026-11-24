/* SPDX-License-Identifier: MIT */
#import <Foundation/Foundation.h>
#include "lease.h"
#include <libavutil/buffer.h>
#include <CoreVideo/CoreVideo.h>
#include <dlfcn.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
extern void IOSurfaceIncrementUseCount(IOSurfaceRef);
extern void IOSurfaceDecrementUseCount(IOSurfaceRef);
extern int32_t IOSurfaceGetUseCount(IOSurfaceRef);
static unsigned freed;
#define freeCount() __atomic_load_n(&freed,__ATOMIC_ACQUIRE)
static void releaseBytes(void *opaque,uint8_t *data){(void)opaque;free(data);__atomic_fetch_add(&freed,1,__ATOMIC_RELEASE);}
static void need(int b,const char *where){if(!b){fprintf(stderr,"LEASE_FAIL %s\n",where);exit(1);}}
static void number(CFMutableDictionaryRef d,CFStringRef key,int64_t value){CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&value);CFDictionarySetValue(d,key,n);CFRelease(n);}
int main(int argc,char **argv){@autoreleasepool{
 CFMutableDictionaryRef p=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 number(p,kIOSurfaceWidth,4);number(p,kIOSurfaceHeight,4);number(p,kIOSurfaceBytesPerRow,16);number(p,kIOSurfaceBytesPerElement,4);number(p,kIOSurfacePixelFormat,0x42475241);
 IOSurfaceRef surface=IOSurfaceCreate(p);CFRelease(p);need(surface!=NULL,"source-owned surface");
 need(IOSurfaceGetUseCount(surface)==0,"initial usecount0");
 AVFrame *frame=av_frame_alloc();need(frame!=NULL,"frame");uint8_t *bytes=calloc(1,64);need(bytes!=NULL,"payload");
 frame->buf[0]=av_buffer_create(bytes,64,releaseBytes,NULL,0);frame->data[0]=bytes;need(frame->buf[0]!=NULL,"buffer");
 CFTypeRef lease=SFVTCreateFrameLease(frame,surface);need(lease!=NULL,"producer lease");av_frame_free(&frame);
 need(IOSurfaceGetUseCount(surface)==1&&!freeCount(),"producer bridge retains AVBuffer");
 IOSurfaceIncrementUseCount(surface);CFRelease(lease);
 need(IOSurfaceGetUseCount(surface)==1&&!freeCount(),"consumer keeps frame after producer release");
 usleep(30000);need(!freeCount(),"deferred frame still owned");
 IOSurfaceDecrementUseCount(surface);
 uint64_t a=0,b=0,c=0;
 for(unsigned i=0;i<200;i++){SFVTFrameLeaseCounts(&a,&b,&c);if(b==1)break;usleep(5000);}
 need(a==1&&b==1&&c==1&&freeCount()==1,"release exactly once after consumer count0");
 printf("LEASE producer-bridge=1 consumer-hold=1 deferred=1 AVBuffer-released-once=1 final-usecount=%d\n",IOSurfaceGetUseCount(surface));
 if(argc>1 && !strcmp(argv[1],"--require-private-cv")) {
  CVReturn(*setLifetime)(CVPixelBufferRef,CFTypeRef)=dlsym(RTLD_DEFAULT,"CVPixelBufferSFSetLifetimeObject");
  need(setLifetime!=NULL,"private CV lifetime API present");
  frame=av_frame_alloc();bytes=calloc(1,64);need(frame&&bytes,"second frame");
  frame->buf[0]=av_buffer_create(bytes,64,releaseBytes,NULL,0);frame->data[0]=bytes;
  lease=SFVTCreateFrameLease(frame,surface);av_frame_free(&frame);need(lease!=NULL,"second lease");
  CVPixelBufferRef pixel=NULL;need(!CVPixelBufferCreateWithIOSurface(NULL,surface,NULL,&pixel)&&pixel,"CV surface wrapper");
  need(!setLifetime(pixel,lease),"one-shot set");
  need(setLifetime(pixel,lease)!=0,"reject overwrite");CFRelease(lease);
  CVBufferRemoveAllAttachments(pixel);need(freeCount()==1,"public attachment removal cannot drop frame");
  IOSurfaceIncrementUseCount(surface);CVPixelBufferRelease(pixel);
  need(IOSurfaceGetUseCount(surface)==1&&freeCount()==1,"consumer survives CV destruction");
  IOSurfaceDecrementUseCount(surface);
  for(unsigned i=0;i<200;i++){SFVTFrameLeaseCounts(&a,&b,&c);if(b==2)break;usleep(5000);}
  need(a==2&&b==2&&c==2&&freeCount()==2,"second release exactly once");
  puts("LEASE private-CV opaque-lifetime=1 attachment-removal-safe=1 overwrite-refused=1 consumer-deferred=1");
 }
 CFRelease(surface);return 0;
}}
