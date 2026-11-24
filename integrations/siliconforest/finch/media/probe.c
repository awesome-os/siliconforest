/* SPDX-License-Identifier: MIT */
#include <CoreMedia/CoreMedia.h>
#include <CoreVideo/CoreVideo.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s\n",#x);return 1;}}while(0)
int main(void) {
    _Static_assert(sizeof(CMTime)==24 && _Alignof(CMTime)==4,"CMTime public ABI");
    _Static_assert(sizeof(CMBlockBufferCustomBlockSource)==28 && _Alignof(CMBlockBufferCustomBlockSource)==4,"custom block source public ABI");
    _Static_assert(sizeof(CMSampleTimingInfo)==72,"sample timing public ABI");
    _Static_assert(sizeof(CMTag)==16,"tag public ABI");
    CMTime a=CMTimeMake(1,3),b=CMTimeMake(1,6);
    CHECK(CMTimeCompare(CMTimeSubtract(a,b),b)==0);
    CMBlockBufferRef data=NULL;CHECK(CMBlockBufferCreateWithMemoryBlock(NULL,NULL,32,NULL,NULL,0,32,0,&data)==0);
    CHECK(CMBlockBufferFillDataBytes(0x2a,data,0,32)==0);
    unsigned char bytes[32]={0};CHECK(CMBlockBufferCopyDataBytes(data,0,32,bytes)==0);
    for(int i=0;i<32;i++)CHECK(bytes[i]==0x2a);
    CMVideoFormatDescriptionRef format=NULL;CHECK(CMVideoFormatDescriptionCreate(NULL,kCMVideoCodecType_H264,64,32,NULL,&format)==0);
    CMVideoDimensions size=CMVideoFormatDescriptionGetDimensions(format);CHECK(size.width==64&&size.height==32);
    CMSampleTimingInfo timing={CMTimeMake(1,30),CMTimeMake(7,30),kCMTimeInvalid};
    CMSampleBufferRef sample=NULL;size_t length=32;
    CHECK(CMSampleBufferCreateReady(NULL,data,format,1,1,&timing,1,&length,&sample)==0);
    CHECK(CMTimeCompare(CMSampleBufferGetPresentationTimeStamp(sample),timing.presentationTimeStamp)==0);
    CHECK(CMTimeCompare(CMSampleBufferGetDuration(sample),timing.duration)==0);
    CFRelease(data);data=CMSampleBufferGetDataBuffer(sample);CHECK(CMBlockBufferGetDataLength(data)==32);
    CFRelease(sample);CFRelease(format);
    CVPixelBufferRef image=NULL;CHECK(CVPixelBufferCreate(NULL,64,32,kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange,NULL,&image)==0);
    CHECK(CVPixelBufferIsPlanar(image)&&CVPixelBufferGetPlaneCount(image)==2);
    CHECK(CVPixelBufferGetWidthOfPlane(image,1)==32&&CVPixelBufferGetHeightOfPlane(image,1)==16);
    CHECK(CVPixelBufferLockBaseAddress(image,0)==0);
    CHECK(CVPixelBufferGetBytesPerRowOfPlane(image,0)>=64);
    CHECK(CVPixelBufferGetBaseAddressOfPlane(image,0)!=NULL&&CVPixelBufferGetBaseAddressOfPlane(image,1)!=NULL);
    memset(CVPixelBufferGetBaseAddressOfPlane(image,0),0x31,CVPixelBufferGetBytesPerRowOfPlane(image,0)*32);
    CHECK(CVPixelBufferUnlockBaseAddress(image,0)==0);
    CVBufferSetAttachment(image,kCVImageBufferColorPrimariesKey,kCVImageBufferColorPrimaries_ITU_R_709_2,kCVAttachmentMode_ShouldPropagate);
    CHECK(CMVideoFormatDescriptionCreateForImageBuffer(NULL,image,&format)==0);
    CHECK(CMSampleBufferCreateReadyWithImageBuffer(NULL,image,format,&timing,&sample)==0);
    CFRelease(image);CHECK(CMSampleBufferGetImageBuffer(sample)!=NULL);
    CHECK(CVPixelBufferGetWidth(CMSampleBufferGetImageBuffer(sample))==64);
    CFRelease(sample);CFRelease(format);
    puts("PASS media time/block/sample lifetime/NV12 layout/metadata/image ownership");return 0;
}
