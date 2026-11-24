/* SPDX-License-Identifier: MIT */
/* Native CF/AVBuffer ownership control. The consumer usecount is simulated;
 * no decoder work or real GPU fence is claimed by this component test. */
#import <Foundation/Foundation.h>
#include "lease.h"
#include <libavutil/buffer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern void IOSurfaceIncrementUseCount(IOSurfaceRef);
extern void IOSurfaceDecrementUseCount(IOSurfaceRef);
extern int32_t IOSurfaceGetUseCount(IOSurfaceRef);
static unsigned freed;
static void releaseMetadata(void *unused,uint8_t *data){(void)unused;free(data);__atomic_fetch_add(&freed,1,__ATOMIC_RELEASE);}
static void need(int value,const char *why){if(!value){fprintf(stderr,"RETIRE_FAIL %s\n",why);exit(1);}}
static void number(CFMutableDictionaryRef d,CFStringRef key,int64_t value){CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt64Type,&value);CFDictionarySetValue(d,key,n);CFRelease(n);}
int main(void){@autoreleasepool{
 CFMutableDictionaryRef d=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 number(d,kIOSurfaceWidth,4);number(d,kIOSurfaceHeight,4);number(d,kIOSurfaceBytesPerRow,16);
 number(d,kIOSurfaceBytesPerElement,4);number(d,kIOSurfacePixelFormat,0x42475241);
 IOSurfaceRef surface=IOSurfaceCreate(d);CFRelease(d);need(surface!=NULL,"surface");
 need(!IOSurfaceLock(surface,0,NULL),"storage lock");
 memset(IOSurfaceGetBaseAddress(surface),0xa5,64);need(!IOSurfaceUnlock(surface,0,NULL),"storage unlock");
 AVFrame *frame=av_frame_alloc();need(frame!=NULL,"frame metadata");
 uint8_t *metadata=calloc(1,64);need(metadata!=NULL,"metadata allocation");
 frame->buf[0]=av_buffer_create(metadata,64,releaseMetadata,NULL,0);frame->data[0]=metadata;
 need(frame->buf[0]!=NULL,"metadata buffer");
 SFFrameLeaseGroup *group=SFVTFrameLeaseGroupCreate();need(group!=NULL,"group");
 CFTypeRef lease=SFVTCreateGroupedFrameLease(frame,surface,group);need(lease!=NULL,"grouped bridge");
 av_frame_free(&frame);need(!__atomic_load_n(&freed,__ATOMIC_ACQUIRE),"metadata still retained");
 IOSurfaceIncrementUseCount(surface);CFRelease(surface); /* only lease owns the CF storage */
 SFVTFrameLeaseGroupRetire(group);
 need(__atomic_load_n(&freed,__ATOMIC_ACQUIRE)==1,"retire drops metadata pool hold");
 need(IOSurfaceGetUseCount(surface)==2,"producer and simulated consumer storage holds unchanged");
 CFRelease(lease);usleep(30000);
 need(IOSurfaceGetUseCount(surface)==1,"simulated consumer outlives producer object");
 need(!IOSurfaceLock(surface,1,NULL),"retired storage read lock");
 const uint8_t *bytes=IOSurfaceGetBaseAddress(surface);
 for(unsigned i=0;i<64;i++)need(bytes[i]==0xa5,"retained storage bytes intact");
 need(!IOSurfaceUnlock(surface,1,NULL),"retired storage unlock");
 IOSurfaceDecrementUseCount(surface); /* pointer may now be freed: never touch it again */
 uint64_t a=0,b=0,c=0;
 for(unsigned i=0;i<200;i++){SFVTFrameLeaseCounts(&a,&b,&c);if(b==1)break;usleep(5000);}
 need(a==1&&b==1&&c==1,"exactly one deferred storage release");
 need(__atomic_load_n(&freed,__ATOMIC_ACQUIRE)==1,"metadata release once");
 SFVTFrameLeaseGroupRetire(SFVTFrameLeaseGroupCreate());
 puts("RETIRE component=1 metadata-freed=1 storage-retained=1 bytes-intact=64 consumer-simulated=1 actual-decoder=0 GPU-fence=0");
 return 0;
}}
