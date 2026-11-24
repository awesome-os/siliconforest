/* SPDX-License-Identifier: MIT */
/* Public capability queries; no decode, service mutation or output device. */
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
/* Public declarations written here; no SDK headers are inputs. */
typedef const struct opaqueCMFormatDescription *CMFormatDescriptionRef;
extern OSStatus CMFormatDescriptionCreate(CFAllocatorRef,uint32_t,uint32_t,
                                          CFDictionaryRef,CMFormatDescriptionRef *);
int main(void){
 void *image=dlopen("/System/Library/Frameworks/VideoToolbox.framework/VideoToolbox",RTLD_NOW|RTLD_GLOBAL);
 if(!image){fprintf(stderr,"VT_CAPABILITY_LOAD_FAIL %s\n",dlerror());return 1;}
 OSStatus(*copy)(CMFormatDescriptionRef,CFDictionaryRef*)=dlsym(image,"VTCopyVideoDecoderExtensionProperties");
 Boolean(*stereo)(void)=dlsym(image,"VTIsStereoMVHEVCDecodeSupported");
 if(!copy||!stereo){fputs("VT_CAPABILITY_UNSUPPORTED missing query\n",stderr);return 77;}
 struct{const char *name;uint32_t media,codec;} rows[]={{"h264",0x76696465,0x61766331},{"hevc",0x76696465,0x68766331},{"unknown",0x76696465,0x53463031}};
 for(unsigned n=0;n<sizeof(rows)/sizeof(rows[0]);n++){
  CMFormatDescriptionRef format=NULL;OSStatus status=CMFormatDescriptionCreate(NULL,rows[n].media,rows[n].codec,NULL,&format);
  if(status||!format){printf("VT_CAPABILITY_FORMAT_FAIL %d\n",(int)status);return 1;}
  CFDictionaryRef properties=(CFDictionaryRef)(uintptr_t)1;
  status=copy(format,&properties);
  printf("VT_EXTENSION %s status=%d output-null=%d output-untouched=%d\n",rows[n].name,(int)status,properties==NULL,properties==(CFDictionaryRef)(uintptr_t)1);
  if(!status&&properties&&(uintptr_t)properties!=1)CFRelease(properties);
  CFRelease(format);
 }
 printf("VT_STEREO supported=%d\n",stereo()!=0);return 0;
}
