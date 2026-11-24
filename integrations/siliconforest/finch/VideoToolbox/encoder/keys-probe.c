/* SPDX-License-Identifier: MIT */
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
 const CFStringRef keys[]={kVTDecompressionPropertyKey_DeinterlaceMode,kVTDecompressionPropertyKey_FieldMode,
   kVTDecompressionProperty_DeinterlaceMode_Temporal,kVTDecompressionProperty_FieldMode_DeinterlaceFields};
 for(unsigned i=0;i<4;i++) {
  char value[128];if(!keys[i]||!CFStringGetCString(keys[i],value,sizeof(value),kCFStringEncodingUTF8))return 1;
  puts(value);
 }
 if(argc>1&&!strcmp(argv[1],"--unsupported-deinterlace")) {
  CMVideoFormatDescriptionRef format=NULL;VTDecompressionSessionRef session=NULL;
  if(CMVideoFormatDescriptionCreate(NULL,kCMVideoCodecType_H264,1280,720,NULL,&format))return 2;
  const void *k[]={keys[0],keys[1]},*v[]={keys[2],keys[3]};
  CFDictionaryRef options=CFDictionaryCreate(NULL,k,v,2,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  OSStatus status=VTDecompressionSessionCreate(NULL,format,options,NULL,NULL,&session);
  CFRelease(options);CFRelease(format);
  printf("deinterlace-create status=%d session-null=%d\n",(int)status,session==NULL);
  if(session){VTDecompressionSessionInvalidate(session);CFRelease(session);}
  return status==-12900&&session==NULL?0:3;
 }
 return 0;
}
