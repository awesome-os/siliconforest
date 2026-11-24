/* SPDX-License-Identifier: MIT */
/* Linux-built public-API encoder probe; writes ONLY our generated pattern/video. */
#include <VideoToolbox/VideoToolbox.h>
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>
#define WIDTH 1280
#define HEIGHT 720
#define FRAMES 30
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static FILE *video;
static unsigned frames,errors,header_written;
static uint64_t bytes_written;
static void need(int okay,const char *s){if(!okay){fprintf(stderr,"ENCODE_FAIL %s\n",s);exit(1);}}
static int emit(const void *data,size_t size){if(fwrite(data,1,size,video)!=size){errors++;return 0;}bytes_written+=size;return 1;}
static int nal(const void *data,size_t size){static const unsigned char start[]={0,0,0,1};return emit(start,4)&&emit(data,size);}
static void output(void *ctx,void *ref,OSStatus status,VTEncodeInfoFlags flags,CMSampleBufferRef sample) {
 (void)ctx;(void)flags;pthread_mutex_lock(&lock);
 if(status||!sample||(uintptr_t)ref!=frames+1||CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(sample),CMTimeMake(frames,30))){errors++;goto done;}
 if(!header_written){
  CMVideoFormatDescriptionRef format=(CMVideoFormatDescriptionRef)CMSampleBufferGetFormatDescription(sample);
  for(size_t i=0;i<2;i++){
   const uint8_t *data=NULL;size_t size=0,count=0;int length=0;
   OSStatus e=CMVideoFormatDescriptionGetH264ParameterSetAtIndex(format,i,&data,&size,&count,&length);
   if(e||!data||!size||count<2||length!=4||!nal(data,size)){errors++;goto done;}
  }
  header_written=1;
 }
 {
  CMBlockBufferRef block=CMSampleBufferGetDataBuffer(sample);size_t size=block?CMBlockBufferGetDataLength(block):0;
  if(!size||size>64*1024*1024){errors++;goto done;}
  unsigned char *data=malloc(size);if(!data){errors++;goto done;}
  if(CMBlockBufferCopyDataBytes(block,0,size,data)){errors++;free(data);goto done;}
  size_t offset=0;
  while(offset<size){
   if(size-offset<4){errors++;break;}
   uint32_t n=(uint32_t)data[offset]<<24|(uint32_t)data[offset+1]<<16|(uint32_t)data[offset+2]<<8|data[offset+3];offset+=4;
   if(!n||n>size-offset){errors++;break;}
   if(!nal(data+offset,n))break;
   offset+=n;
  }
  free(data);
  if(offset!=size)goto done;
 }
 frames++;
done:pthread_mutex_unlock(&lock);
}
static int32_t set_i32(VTCompressionSessionRef session,CFStringRef key,int32_t value){
 CFNumberRef n=CFNumberCreate(NULL,kCFNumberSInt32Type,&value);if(!n)return -1;
 OSStatus status=VTSessionSetProperty(session,key,n);CFRelease(n);return status;
}
static double seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static double cpu(void){struct rusage r;getrusage(RUSAGE_SELF,&r);return r.ru_utime.tv_sec+r.ru_utime.tv_usec*1e-6+r.ru_stime.tv_sec+r.ru_stime.tv_usec*1e-6;}
int main(void) {
 video=fopen("encoded.h264","wb");FILE *source=fopen("source.nv12","wb");need(video&&source,"create own output files");
 const void *keys[]={kVTVideoEncoderSpecification_EnableHardwareAcceleratedVideoEncoder};
 const void *values[]={kCFBooleanFalse};
 CFDictionaryRef spec=CFDictionaryCreate(NULL,keys,values,1,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 int32_t dimensions[]={WIDTH,HEIGHT,kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange};
 const void *pixelKeys[]={kCVPixelBufferWidthKey,kCVPixelBufferHeightKey,kCVPixelBufferPixelFormatTypeKey};
 CFNumberRef numbers[3];for(unsigned i=0;i<3;i++)numbers[i]=CFNumberCreate(NULL,kCFNumberSInt32Type,&dimensions[i]);
 CFDictionaryRef attributes=CFDictionaryCreate(NULL,pixelKeys,(const void **)numbers,3,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 for(unsigned i=0;i<3;i++)CFRelease(numbers[i]);
 VTCompressionSessionRef session=NULL;
 need(VTCompressionSessionCreate(NULL,WIDTH,HEIGHT,kCMVideoCodecType_H264,spec,attributes,NULL,output,NULL,&session)==0&&session,"create H264 session");CFRelease(attributes);CFRelease(spec);
 need(VTSessionSetProperty(session,kVTCompressionPropertyKey_AllowFrameReordering,kCFBooleanFalse)==0,"IPPP policy");
 need(set_i32(session,kVTCompressionPropertyKey_AverageBitRate,8000000)==0,"bitrate");
 need(set_i32(session,kVTCompressionPropertyKey_ExpectedFrameRate,30)==0,"rate");
 need(set_i32(session,kVTCompressionPropertyKey_MaxKeyFrameInterval,30)==0,"GOP");
 need(VTCompressionSessionPrepareToEncodeFrames(session)==0,"prepare");
 CVPixelBufferPoolRef pool=VTCompressionSessionGetPixelBufferPool(session);need(pool!=NULL,"pixel pool");
 double began=seconds(),used=cpu();
 for(unsigned index=0;index<FRAMES;index++){
  CVPixelBufferRef frame=NULL;need(CVPixelBufferPoolCreatePixelBuffer(NULL,pool,&frame)==0&&frame,"allocate frame");
  need(CVPixelBufferGetPixelFormatType(frame)==kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange&&CVPixelBufferGetPlaneCount(frame)==2,"NV12 pool");
  need(CVPixelBufferLockBaseAddress(frame,0)==0,"lock frame");
  uint8_t *y=CVPixelBufferGetBaseAddressOfPlane(frame,0),*uv=CVPixelBufferGetBaseAddressOfPlane(frame,1);
  size_t ys=CVPixelBufferGetBytesPerRowOfPlane(frame,0),us=CVPixelBufferGetBytesPerRowOfPlane(frame,1);need(y&&uv&&ys>=WIDTH&&us>=WIDTH,"planes/strides");
  for(unsigned row=0;row<HEIGHT;row++){
   for(unsigned col=0;col<WIDTH;col++)y[row*ys+col]=(uint8_t)(32+((col/32+row/32+index)%8)*24);
   need(fwrite(y+row*ys,1,WIDTH,source)==WIDTH,"write source Y");
  }
  for(unsigned row=0;row<HEIGHT/2;row++){memset(uv+row*us,128,WIDTH);need(fwrite(uv+row*us,1,WIDTH,source)==WIDTH,"write source UV");}
  need(CVPixelBufferUnlockBaseAddress(frame,0)==0,"unlock frame");
  VTEncodeInfoFlags flags=0;OSStatus status=VTCompressionSessionEncodeFrame(session,frame,CMTimeMake(index,30),CMTimeMake(1,30),NULL,(void *)(uintptr_t)(index+1),&flags);
  CFRelease(frame);need(status==0,"encode frame");
 }
 need(VTCompressionSessionCompleteFrames(session,kCMTimeInvalid)==0,"complete");
 double elapsed=seconds()-began,cpu_time=cpu()-used;
 need(frames==FRAMES&&!errors&&header_written,"30 ordered callbacks with SPS/PPS and bounded samples");
 need(fclose(source)==0&&fclose(video)==0,"flush output");
 CFTypeRef hw=NULL;OSStatus query=VTSessionCopyProperty(session,kVTCompressionPropertyKey_UsingHardwareAcceleratedVideoEncoder,NULL,&hw);
 int hardware=query==0&&hw?(hw==kCFBooleanTrue):-1;if(hw)CFRelease(hw);
 need(hardware!=1,"software selection honored when reported");
 printf("ENCODER codec=H264 frames=%u width=%d height=%d callbacks-ordered=1 software-requested=1 hardware=%d query-status=%d\n",frames,WIDTH,HEIGHT,hardware,query);
 printf("ENCODER bytes=%llu fps=%.2f cpu_seconds=%.6f wall_seconds=%.6f\n",(unsigned long long)bytes_written,frames/elapsed,cpu_time,elapsed);
 VTCompressionSessionInvalidate(session);CFRelease(session);return 0;
}
