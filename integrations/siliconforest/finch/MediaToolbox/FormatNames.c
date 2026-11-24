/* SPDX-License-Identifier: MIT */
/* Our documented format-name API. English fallback names for known types;
 * unknown nonzero codes retain their FourCC, zero returns NULL. This does not register codecs or read assets. */
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
struct name {uint32_t code;const char *text;};
static CFStringRef copy_name(uint32_t code,const struct name *table,size_t count){
 for(size_t i=0;i<count;i++)if(table[i].code==code)
  return CFStringCreateWithCString(NULL,table[i].text,kCFStringEncodingUTF8);
 if(!code)return NULL;
 const UInt8 bytes[]={(UInt8)(code>>24),(UInt8)(code>>16),(UInt8)(code>>8),(UInt8)code};
 return CFStringCreateWithBytes(NULL,bytes,4,kCFStringEncodingMacRoman,false);
}
CFStringRef MTCopyLocalizedNameForMediaType(uint32_t type){
 static const struct name names[]={
  {0x76696465,"Video"},{0x736f756e,"Sound"},{0x74657874,"Text"},
  {0x636c6370,"Closed Caption"},{0x7362746c,"Subtitle"},{0x746d6364,"Timecode"},
  {0x6d657461,"Metadata"},{0x6d757878,"Muxed"},
  {0x6f64736d,"MPEG-4 ODSM"},{0x7364736d,"MPEG-4 SDSM"}
 };
 return copy_name(type,names,sizeof(names)/sizeof(names[0]));
}
CFStringRef MTCopyLocalizedNameForMediaSubType(uint32_t type,uint32_t sub){
 if(type==0x76696465){
  static const struct name names[]={
   {0x61766331,"H.264"},{0x68766331,"HEVC"},{0x68657631,"HEVC (Parameter Sets in Bitstream)"},
   {0x6a706567,"Photo - JPEG"},
   {0x6170636e,"Apple ProRes 422"}
  };
  return copy_name(sub,names,sizeof(names)/sizeof(names[0]));
 }
 if(type==0x736f756e){
  static const struct name names[]={
   {0x6c70636d,"Linear PCM"},{0x61616320,"MPEG-4 AAC"},
   {0x2e6d7033,"MPEG Layer 3"},{0x616c6163,"Apple Lossless"},
   {0x61632d33,"Dolby Digital"},{0x65632d33,"Dolby Digital Plus"}
  };
  return copy_name(sub,names,sizeof(names)/sizeof(names[0]));
 }
 if(type==0x74657874){
  static const struct name names[]={{0x74783367,"3GPP Text"},{0x74657874,"QuickTime Text"}};
  return copy_name(sub,names,sizeof(names)/sizeof(names[0]));
 }
 if(type==0x7362746c&&sub==0x74783367)return CFStringCreateWithCString(NULL,"Subtitle",kCFStringEncodingUTF8);
 return copy_name(sub,NULL,0);
}
