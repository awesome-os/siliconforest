/* SPDX-License-Identifier: MIT */
/* Own declarations from Apple's public API documentation, no SDK headers.
 * https://developer.apple.com/documentation/mediatoolbox/mtcopylocalizednameformediatype(_:)
 * https://developer.apple.com/documentation/mediatoolbox/mtcopylocalizednameformediasubtype(_:_:)
 */
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <stdio.h>
#include <dlfcn.h>
static uint32_t fourcc(const char *s){return (uint32_t)(uint8_t)s[0]<<24|(uint32_t)(uint8_t)s[1]<<16|(uint32_t)(uint8_t)s[2]<<8|(uint8_t)s[3];}
static void string(CFStringRef value){
 char text[1024];
 if(!value)printf("NULL\n");
 else if(CFStringGetCString(value,text,sizeof(text),kCFStringEncodingUTF8))printf("%s\n",text);
 else printf("UNREPRESENTABLE\n");
 if(value)CFRelease(value);
}
int main(void){
 void *image=dlopen("/System/Library/Frameworks/MediaToolbox.framework/MediaToolbox",RTLD_NOW);
 if(!image){fprintf(stderr,"MT_NAMES missing: %s\n",dlerror());return 1;}
 CFStringRef (*type)(uint32_t)=dlsym(image,"MTCopyLocalizedNameForMediaType");
 CFStringRef (*sub)(uint32_t,uint32_t)=dlsym(image,"MTCopyLocalizedNameForMediaSubType");
 if(!type||!sub)return 2;
 const char *types[]={"vide","soun","text","clcp","sbtl","tmcd","meta","muxx","odsm","sdsm","XXXX"};
 const char *subtypes[]={"avc1","hvc1","hev1","vp09","av01","jpeg","apcn","lpcm","aac ","mp4a",".mp3","alac","ac-3","ec-3","stpp","tx3g","text","zzzz"};
 for(unsigned i=0;i<sizeof(types)/sizeof(types[0]);i++){
  printf("MT_TYPE %.4s ",types[i]);string(type(fourcc(types[i])));
  for(unsigned j=0;j<sizeof(subtypes)/sizeof(subtypes[0]);j++){
   printf("MT_SUBTYPE %.4s %.4s ",types[i],subtypes[j]);string(sub(fourcc(types[i]),fourcc(subtypes[j])));
  }
 }
 printf("MT_TYPE zero ");string(type(0));
 printf("MT_SUBTYPE zero zero ");string(sub(0,0));
 return 0;
}
