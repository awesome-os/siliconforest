/* SPDX-License-Identifier: MIT */
/* Own runtime comparison inputs. Calling conventions inferred from the locally
 * held QuickTime26.5.2 arm64e call sites; values below establish behavior
 * separately. No Apple headers, code bytes, assets or files are returned. */
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMedia/CoreMedia.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <dlfcn.h>
typedef struct {double a,b,c,d,tx,ty;} SFAffine;
extern const CFStringRef kCMFormatDescriptionExtension_ColorPrimaries;
extern const CFStringRef kCMFormatDescriptionExtension_TransferFunction;
extern const CFStringRef kCMFormatDescriptionExtension_YCbCrMatrix;
static void time_value(CMTime t){printf("%lld/%d flags=%u epoch=%lld",(long long)t.value,t.timescale,t.flags,(long long)t.epoch);}
static void range_value(CMTimeRange r){printf("start=");time_value(r.start);printf(" duration=");time_value(r.duration);}
int main(void){
 void *kit=dlopen("/System/Library/Frameworks/AVKit.framework/AVKit",RTLD_NOW|RTLD_GLOBAL);
 void *mt=dlopen("/System/Library/Frameworks/MediaToolbox.framework/MediaToolbox",RTLD_NOW|RTLD_GLOBAL);
 if(!kit||!mt){puts("MEDIA_MATH missing framework");return 1;}
 CMTimeRange (*asset)(CMTimeRange,CMTime)=dlsym(kit,"AVAssetSlowMotionTimeRangeFromNormalizedSlowMotionTimeRange");
 CMTimeRange (*normal)(CMTimeRange,CMTime)=dlsym(kit,"AVNormalizedSlowMotionTimeRangeFromAssetSlowMotionTimeRange");
 int32_t (*rotation)(SFAffine,uint8_t*,uint8_t*)=dlsym(mt,"FigGetRotationAngleAndFlipsFromCGAffineTransform");
 CFStringRef (*dynamic)(uint32_t)=dlsym(mt,"MTCopyLocalizedStringForVideoDynamicRange");
 CFStringRef (*color)(uint32_t,CMFormatDescriptionRef)=dlsym(mt,"MTCopyStringForColorInfo");
 if(!asset||!normal||!rotation||!dynamic||!color){puts("MEDIA_MATH missing operation");return 2;}
 const int cases[][6]={{1,4,1,2,10,1},{0,1,1,1,10,1},{-1,4,3,2,10,1},{1,3,1,3,7,3},{0,1,0,1,10,1},{1,2,1,4,0,1},{1,2,1,4,-10,1},{0,600,0,600,10,1},{0,600,1,600,7,3},{1,2,1,4,1,2},{1,3,2,3,3,2}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
  CMTimeRange input={CMTimeMake(cases[i][0],cases[i][1]),CMTimeMake(cases[i][2],cases[i][3])};
  CMTime duration=CMTimeMake(cases[i][4],cases[i][5]);
  CMTimeRange converted=asset(input,duration),restored=normal(converted,duration);
  printf("MEDIA_RANGE case=%u asset ",i);range_value(converted);printf("\n");
  printf("MEDIA_RANGE case=%u restored ",i);range_value(restored);printf("\n");
 }
 const SFAffine matrices[]={
  {1,0,0,1,0,0},{0,1,-1,0,0,0},{-1,0,0,-1,0,0},{0,-1,1,0,0,0},
  {-1,0,0,1,0,0},{1,0,0,-1,0,0},{0,1,1,0,0,0},{0,-1,-1,0,0,0},
  {2,0,0,3,10,20},{.7071067811865476,.7071067811865476,-.7071067811865476,.7071067811865476,0,0},
  {0,0,0,0,0,0},{1,.2,.1,1,0,0},{0,2,-3,0,0,0},{1e-10,1,-1,1e-10,0,0},{1e-4,1,-1,1e-4,0,0}
 };
 for(unsigned i=0;i<sizeof(matrices)/sizeof(matrices[0]);i++){
  uint8_t horizontal[8],vertical[8];memset(horizontal,0xa5,sizeof(horizontal));memset(vertical,0xa5,sizeof(vertical));
  horizontal[2]=vertical[2]=0;
  int32_t angle=rotation(matrices[i],horizontal+2,vertical+2);int intact=1;
  for(unsigned j=0;j<8;j++)if(j!=2&&(horizontal[j]!=0xa5||vertical[j]!=0xa5))intact=0;
  printf("MEDIA_ROTATION case=%u angle=%d first-flip=%u second-flip=%u one-byte-guards=%d\n",i,angle,horizontal[2],vertical[2],intact);
  if(!intact)return 3;
 }
 for(uint32_t value=0;value<=8;value++){
  CFStringRef label=dynamic(value);char text[512]={0};
  if(label)CFStringGetCString(label,text,sizeof(text),kCFStringEncodingUTF8);
  printf("MEDIA_DYNAMIC value=%u name=%s\n",value,label?text:"NULL");if(label)CFRelease(label);
 }
 const CFStringRef colors[][3]={
  {NULL,NULL,NULL},{CFSTR("ITU_R_709_2"),CFSTR("ITU_R_709_2"),CFSTR("ITU_R_709_2")},
  {CFSTR("ITU_R_2020"),CFSTR("SMPTE_ST_2084_PQ"),CFSTR("ITU_R_2020")},
  {CFSTR("ITU_R_2020"),CFSTR("ITU_R_2100_HLG"),CFSTR("ITU_R_2020")},
  {CFSTR("SMPTE_C"),CFSTR("ITU_R_709_2"),CFSTR("ITU_R_601_4")},
  {CFSTR("SFUnknownPrimaries"),CFSTR("SFUnknownTransfer"),CFSTR("SFUnknownMatrix")}
 };
 const CFStringRef keys[]={kCMFormatDescriptionExtension_ColorPrimaries,kCMFormatDescriptionExtension_TransferFunction,kCMFormatDescriptionExtension_YCbCrMatrix};
 for(unsigned i=0;i<sizeof(colors)/sizeof(colors[0]);i++){
  CFMutableDictionaryRef extensions=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  for(unsigned j=0;j<3;j++)if(colors[i][j])CFDictionarySetValue(extensions,keys[j],colors[i][j]);
  CMVideoFormatDescriptionRef format=NULL;
  OSStatus status=CMVideoFormatDescriptionCreate(NULL,0x61766331,1280,720,extensions,&format);CFRelease(extensions);
  if(status||!format){printf("MEDIA_COLOR case=%u format-error=%d\n",i,(int)status);return 4;}
  CFStringRef label=color(0x76696465,format);char text[512]={0};
  if(label)CFStringGetCString(label,text,sizeof(text),kCFStringEncodingUTF8);
  printf("MEDIA_COLOR case=%u name=%s\n",i,label?text:"NULL");if(label)CFRelease(label);CFRelease(format);
 }
 return 0;
}
