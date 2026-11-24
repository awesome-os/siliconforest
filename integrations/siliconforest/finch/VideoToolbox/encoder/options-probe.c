/* SPDX-License-Identifier: MIT */
/* Own source-pattern SPS/PPS, public VT calls. No driver or process hooks. */
#include <VideoToolbox/VideoToolbox.h>
#include <stdio.h>
#include <string.h>
static const uint8_t progressive[]={0x67,0x42,0xc0,0x1e,0xda,0x02,0x80,0xbf,0xe5,0xc0,0x44,0,0,3,0,4,0,0,3,0,0xf0,0x3c,0x58,0xba,0x80};
static const uint8_t interlaced[]={0x67,0x64,0,0x15,0xac,0xd9,0x42,8,0xd8,8,0x80,0,0,3,0,0x80,0,0,0x19,0x0f,0x8a,0x14,0xcb};
static const uint8_t pps[]={0x68,0xce,0x0f,0xc8};
int main(int argc,char **argv) {
 int observation=argc==3&&!strcmp(argv[2],"--allow-unavailable-hardware-query");
 if(argc!=2&&!observation)return 2;
 int interlace=!strcmp(argv[1],"interlaced"),unknown=!strcmp(argv[1],"unknown-mode"),
     conflict=!strcmp(argv[1],"deny-required-literal"),
     required=!strcmp(argv[1],"require-literal"),software=!strcmp(argv[1],"software-literal");
 if(!interlace&&!unknown&&!required&&!conflict&&!software&&strcmp(argv[1],"progressive"))return 2;
 const uint8_t *sets[]={interlace?interlaced:progressive,pps};
 size_t lengths[]={interlace?sizeof(interlaced):sizeof(progressive),sizeof(pps)};
 CMVideoFormatDescriptionRef format=NULL;
 OSStatus status=CMVideoFormatDescriptionCreateFromH264ParameterSets(NULL,2,sets,lengths,4,&format);
 if(status||!format){printf("OPTIONS format=%d\n",(int)status);return 3;}
 CFMutableDictionaryRef options=CFDictionaryCreateMutable(NULL,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
 /* Native explicitly refuses contradictory literals. Without Require, false
  * Enable selects software. Actual hardware selection is queried separately. */
 CFDictionarySetValue(options,CFSTR("EnableHardwareAcceleratedVideoDecoder"),required?kCFBooleanTrue:kCFBooleanFalse);
 if(required||conflict)CFDictionarySetValue(options,CFSTR("RequireHardwareAcceleratedVideoDecoder"),kCFBooleanTrue);
 if(!software&&!required&&!conflict){
  CFDictionarySetValue(options,CFSTR("FieldMode"),CFSTR("DeinterlaceFields"));
  CFDictionarySetValue(options,CFSTR("DeinterlaceMode"),unknown?CFSTR("SFUnknownMode"):CFSTR("Temporal"));
 }
 VTDecompressionSessionRef session=NULL;
 status=VTDecompressionSessionCreate(NULL,format,options,NULL,NULL,&session);
 CFRelease(options);CFRelease(format);
 int hardware=-1;OSStatus property=-1;
 if(session){
  CFTypeRef value=NULL;
  property=VTSessionCopyProperty(session,kVTDecompressionPropertyKey_UsingHardwareAcceleratedVideoDecoder,NULL,&value);
  if(!property&&value&&CFGetTypeID(value)==CFBooleanGetTypeID())hardware=CFBooleanGetValue(value);
  if(value)CFRelease(value);
 }
 printf("OPTIONS %s status=%d session=%d hardware=%d property=%d\n",argv[1],(int)status,session!=NULL,hardware,(int)property);
 int good=(interlace||unknown)?status==-12900&&session==NULL:
  conflict?status==-12906&&session==NULL:
  !status&&session&&((hardware==required&&!property)||(observation&&property==-12900));
 if(session){VTDecompressionSessionInvalidate(session);CFRelease(session);}
 return good?0:1;
}
